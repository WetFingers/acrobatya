// unit.index.* — индекс поиска по папкам (эталон 3.2, 4.7; план 1.4): sqlite FTS5 (fts_pages, токенизатор
// unicode61 remove_diacritics 2, без стемминга) поверх своего подключения: upsert страниц файла по ключу путь
// + mtime, свежесть, пометка «зашифрован», запрос фразой без учёта регистра и диакритики («оттиск» =
// «отти́ск», но не «оттиска»), ограничение папкой, контекст с метками совпадения, замена страниц при новом
// mtime.
#include <gtest/gtest.h>

#include <sqlite3.h>

#include <memory>
#include <string>
#include <vector>

#include "core/text/index.h"
#include "core/util/features.h"

using ot::TextIndex;

namespace {

struct Db {
    Db() {
        EXPECT_EQ(sqlite3_open(":memory:", &db), SQLITE_OK);
        TextIndex::createSchema(db);
    }
    ~Db() { sqlite3_close(db); }
    sqlite3* db = nullptr;
};

std::vector<std::string> paths(const std::vector<ot::IndexHit>& hits) {
    std::vector<std::string> out;
    for (const auto& h : hits) {
        out.push_back(h.path + "#" + std::to_string(h.page));
    }
    return out;
}

}  // namespace

TEST(index, sqlite_has_fts5) { EXPECT_EQ(sqlite3_compileoption_used("ENABLE_FTS5"), 1); }

TEST(index, upsert_query_and_freshness) {
    OT_FEATURE("view-19");
    Db d;
    TextIndex index(d.db);
    EXPECT_FALSE(index.isFresh("C:/док/a.pdf", 100));
    index.upsert("C:/док/a.pdf", 100,
                 {"Первая страница про оттиск.",
                  "Вторая: ОТТИСК и Отти\xCC\x81"
                  "ск."});
    index.upsert("C:/док/b.pdf", 200, {"Здесь только оттиска и оттиски."});
    index.upsert("C:/другое/c.pdf", 300, {"оттиск в другой папке"});
    EXPECT_TRUE(index.isFresh("C:/док/a.pdf", 100));
    EXPECT_FALSE(index.isFresh("C:/док/a.pdf", 101));
    EXPECT_EQ(index.fileCount(), 3);
    EXPECT_EQ(index.pageCount(), 4);

    // Без регистра и диакритики, но без стемминга: «оттиска» не находится по «оттиск».
    // Порядок — путь (байты UTF-8), затем страница.
    EXPECT_EQ(paths(index.query("оттиск")),
              (std::vector<std::string>{"C:/док/a.pdf#0", "C:/док/a.pdf#1", "C:/другое/c.pdf#0"}));
    EXPECT_EQ(paths(index.query("Отти\xCC\x81"
                                "ск",
                                "C:/док/")),
              (std::vector<std::string>{"C:/док/a.pdf#0", "C:/док/a.pdf#1"}));
    EXPECT_EQ(paths(index.query("оттиска")), (std::vector<std::string>{"C:/док/b.pdf#0"}));
    // Фраза: слова подряд.
    EXPECT_EQ(paths(index.query("другой папке")), (std::vector<std::string>{"C:/другое/c.pdf#0"}));
    EXPECT_TRUE(index.query("папке другой").empty());
    EXPECT_TRUE(index.query("").empty());
    EXPECT_TRUE(index.query("\"").empty());  // кавычки в запросе не ломают синтаксис FTS5

    // Контекст: совпадение между метками \x02…\x03.
    const auto hits = index.query("ОТТИСК", "C:/док/");
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_NE(hits[1].snippet.find("\x02ОТТИСК\x03"), std::string::npos) << hits[1].snippet;
}

TEST(index, new_mtime_replaces_pages_and_remove) {
    Db d;
    TextIndex index(d.db);
    index.upsert("C:/a.pdf", 1, {"старый текст оттиск", "вторая страница"});
    index.upsert("C:/a.pdf", 2, {"новый текст"});
    EXPECT_TRUE(index.isFresh("C:/a.pdf", 2));
    EXPECT_EQ(index.pageCount(), 1);
    EXPECT_TRUE(index.query("оттиск").empty());
    EXPECT_EQ(index.query("новый").size(), 1u);
    const auto file = index.file("C:/a.pdf");
    ASSERT_TRUE(file.has_value());
    EXPECT_EQ(file->mtime, 2);
    EXPECT_EQ(file->pages, 1);
    EXPECT_FALSE(file->encrypted);
    index.remove("C:/a.pdf");
    EXPECT_FALSE(index.file("C:/a.pdf").has_value());
    EXPECT_EQ(index.pageCount(), 0);
}

TEST(index, encrypted_files_are_marked_without_text) {
    Db d;
    TextIndex index(d.db);
    index.markEncrypted("C:/секрет.pdf", 7);
    const auto file = index.file("C:/секрет.pdf");
    ASSERT_TRUE(file.has_value());
    EXPECT_TRUE(file->encrypted);
    EXPECT_TRUE(index.isFresh("C:/секрет.pdf", 7));
    EXPECT_EQ(index.pageCount(), 0);
    EXPECT_EQ(index.encryptedFiles("C:/"), (std::vector<std::string>{"C:/секрет.pdf"}));
    // Снятый пароль (новый mtime) — файл снова «несвежий».
    EXPECT_FALSE(index.isFresh("C:/секрет.pdf", 8));
}

TEST(index, folder_prefix_is_literal) {
    Db d;
    TextIndex index(d.db);
    index.upsert("C:/a_b/x.pdf", 1, {"слово"});
    index.upsert("C:/aXb/y.pdf", 1, {"слово"});
    index.upsert("C:/a%b/z.pdf", 1, {"слово"});
    // «_» и «%» в пути — буквы, а не шаблон LIKE.
    EXPECT_EQ(paths(index.query("слово", "C:/a_b/")), (std::vector<std::string>{"C:/a_b/x.pdf#0"}));
    EXPECT_EQ(paths(index.query("слово", "C:/a%b/")), (std::vector<std::string>{"C:/a%b/z.pdf#0"}));
    EXPECT_EQ(index.query("слово").size(), 3u);
}

// Подсказки поля «Расширенного поиска» (2.5): термины индекса по префиксу — без регистра и диакритики (как их
// хранит токенизатор: «Отти́ск» → «оттиск»), сначала встречающиеся на большем числе страниц, затем по
// алфавиту.
TEST(index, suggest_terms_by_prefix) {
    OT_FEATURE("view-19");
    Db d;
    TextIndex index(d.db);
    index.upsert("C:/док/a.pdf", 1, {"Оттиск и оттиска, отчёт", "оттиск снова"});
    index.upsert("C:/док/b.pdf", 2, {"Отчёт о работе"});
    index.upsert("C:/другое/c.pdf", 3, {"оттепель"});
    EXPECT_EQ(index.suggest("отт"), (std::vector<std::string>{"оттиск", "оттепель", "оттиска"}));
    EXPECT_EQ(index.suggest("отт", 1), (std::vector<std::string>{"оттиск"}));
    EXPECT_EQ(index.suggest("отч"), (std::vector<std::string>{"отчёт"}));  // «ё» — своя буква
    EXPECT_TRUE(index.suggest("щ").empty());
    EXPECT_TRUE(index.suggest("").empty());
}

// Файлы без текстового слоя (сканы без OCR): «нет текста — распознать?» в результатах по папке (2.5).
TEST(index, files_without_text) {
    Db d;
    TextIndex index(d.db);
    index.upsert("C:/док/scan.pdf", 1, {"", "  \n"});
    index.upsert("C:/док/text.pdf", 2, {"текст"});
    index.markEncrypted("C:/док/enc.pdf", 3);
    index.upsert("C:/другое/scan2.pdf", 4, {""});
    EXPECT_EQ(index.emptyFiles("C:/док/"), (std::vector<std::string>{"C:/док/scan.pdf"}));
    EXPECT_EQ(index.emptyFiles(), (std::vector<std::string>{"C:/док/scan.pdf", "C:/другое/scan2.pdf"}));
}
