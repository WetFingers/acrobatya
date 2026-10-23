// engine.folder_search.* — «Расширенный поиск» по папке (эталон 3.2, план 1.4): папка из 30 PDF (с подпапками
// и кириллицей в именах) — «булок» есть в трёх, один файл зашифрован, рядом не-PDF; поиск находит три
// страницы и пропускает зашифрованный с пометкой; повторный поиск берёт всё из индекса (ничего не извлекает);
// смена mtime — файл извлекается заново; удалённый файл уходит из индекса; нерекурсивный поиск — только
// верхняя папка; отмена останавливает обход. Файлы — свои фикстуры (tests/data/mini, tests/fixtures/text),
// корпус не нужен.
#include <gtest/gtest.h>

#include <QString>

#include <process.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/util/features.h"
#include "services/folder_search.h"
#include "services/state_db.h"

namespace fs = std::filesystem;
using ot::FolderSearch;
using ot::FolderSearchResult;
using ot::StateDb;

namespace {

fs::path source() { return fs::path(OTTISK_SOURCE_DIR); }

QString qpath(const fs::path& p) { return QString::fromStdWString(p.generic_wstring()); }

// Папка теста: 30 файлов (3 с «булок», 1 зашифрованный, 26 других) и readme.txt; своя state.sqlite.
class Folder {
public:
    Folder() {
        static int counter = 0;
        root_ = fs::temp_directory_path() /
                (L"оттиск-папка-" + std::to_wstring(_getpid()) + L"-" + std::to_wstring(++counter));
        fs::remove_all(root_);
        fs::create_directories(root_ / L"вложенная" / L"глубже");
        const fs::path mini = source() / "tests" / "data" / "mini";
        const fs::path text = source() / "tests" / "fixtures" / "text";
        copy(mini / "cyr-cid.pdf", root_ / L"с булками 1.pdf");
        copy(mini / "cyr-cid.pdf", root_ / L"вложенная" / L"с булками 2.pdf");
        copy(mini / "cyr-cid.pdf", root_ / L"вложенная" / L"глубже" / L"с булками 3.pdf");
        copy(mini / "enc-r6.pdf", root_ / L"вложенная" / L"зашифрован.pdf");
        const fs::path others[] = {mini / "blank-a4.pdf", mini / "type1-diff.pdf", text / "search.pdf",
                                   text / "vectors.pdf", text / "truetype-nonembedded.pdf"};
        for (int i = 0; i < 26; ++i) {
            const fs::path dir =
                i % 3 == 0 ? root_ : (i % 3 == 1 ? root_ / L"вложенная" : root_ / L"вложенная" / L"глубже");
            copy(others[i % 5], dir / (L"другой " + std::to_wstring(i) + L".pdf"));
        }
        std::ofstream(root_ / "readme.txt") << "булок булок булок";
    }
    ~Folder() {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }
    const fs::path& root() const { return root_; }
    QString db() const { return qpath(root_.parent_path() / (root_.filename().wstring() + L".sqlite")); }

private:
    static void copy(const fs::path& from, const fs::path& to) {
        fs::copy_file(from, to, fs::copy_options::overwrite_existing);
    }
    fs::path root_;
};

std::vector<std::wstring> names(const FolderSearchResult& r) {
    std::vector<std::wstring> out;
    for (const auto& f : r.files) {
        out.push_back(fs::path(f.path.toStdWString()).filename().wstring());
    }
    return out;
}

}  // namespace

TEST(folder_search, finds_three_and_skips_encrypted) {
    OT_FEATURE("view-19");
    Folder folder;
    StateDb db(folder.db());
    ASSERT_TRUE(db.isOpen()) << db.errorString().toStdString();
    FolderSearch search(db);
    std::vector<ot::FolderSearchProgress> progress;
    int found = 0;
    QObject::connect(
        &search, &FolderSearch::progress, &search,
        [&](const ot::FolderSearchProgress& p) { progress.push_back(p); }, Qt::DirectConnection);
    QObject::connect(
        &search, &FolderSearch::fileFound, &search, [&](const ot::FolderFileHits&) { ++found; },
        Qt::DirectConnection);

    const FolderSearchResult r = search.run(qpath(folder.root()), QStringLiteral("булок"));
    EXPECT_FALSE(r.cancelled);
    EXPECT_EQ(r.scanned, 30);    // readme.txt — не PDF
    EXPECT_EQ(r.extracted, 30);  // всё новое
    EXPECT_EQ(r.hitCount(), 3);
    // По пути: «…/вложенная/глубже/…» < «…/вложенная/…» < «…/с булками 1.pdf».
    EXPECT_EQ(names(r),
              (std::vector<std::wstring>{L"с булками 3.pdf", L"с булками 2.pdf", L"с булками 1.pdf"}));
    for (const auto& f : r.files) {
        ASSERT_EQ(f.pages.size(), 1);
        EXPECT_EQ(f.pages[0].page, 0);
        EXPECT_TRUE(f.pages[0].snippet.contains(QStringLiteral("\x02") + QStringLiteral("булок") +
                                                QStringLiteral("\x03")))
            << f.pages[0].snippet.toStdString();
    }
    EXPECT_EQ(found, 3);
    ASSERT_EQ(r.encrypted.size(), 1);
    EXPECT_TRUE(r.encrypted[0].endsWith(QStringLiteral("вложенная/зашифрован.pdf")))
        << r.encrypted[0].toStdString();
    EXPECT_TRUE(r.failed.isEmpty());
    ASSERT_FALSE(progress.empty());
    EXPECT_EQ(progress.back().filesDone, 30);
    EXPECT_EQ(progress.back().filesTotal, 30);
}

TEST(folder_search, index_follows_mtime_and_deletions) {
    OT_FEATURE("view-19");
    Folder folder;
    StateDb db(folder.db());
    FolderSearch search(db);
    const QString root = qpath(folder.root());
    ASSERT_EQ(search.run(root, QStringLiteral("булок")).hitCount(), 3);

    // Ничего не менялось — всё из индекса.
    FolderSearchResult again = search.run(root, QStringLiteral("булок"));
    EXPECT_EQ(again.extracted, 0);
    EXPECT_EQ(again.hitCount(), 3);
    EXPECT_EQ(again.encrypted.size(), 1);  // пометка из индекса

    // Файл заменён (новый mtime) — извлекается заново и находится.
    const fs::path changed = folder.root() / L"другой 0.pdf";
    fs::copy_file(source() / "tests" / "data" / "mini" / "cyr-cid.pdf", changed,
                  fs::copy_options::overwrite_existing);
    fs::last_write_time(changed, fs::last_write_time(changed) + std::chrono::seconds(5));
    const FolderSearchResult updated = search.run(root, QStringLiteral("булок"));
    EXPECT_EQ(updated.extracted, 1);
    EXPECT_EQ(updated.hitCount(), 4);

    // Удалённый с диска — уходит из индекса.
    fs::remove(folder.root() / L"вложенная" / L"с булками 2.pdf");
    const FolderSearchResult removed = search.run(root, QStringLiteral("булок"));
    EXPECT_EQ(removed.extracted, 0);
    EXPECT_EQ(removed.hitCount(), 3);
    EXPECT_EQ(removed.scanned, 29);
}

TEST(folder_search, non_recursive_and_cancel) {
    Folder folder;
    StateDb db(folder.db());
    FolderSearch search(db);
    ot::FolderSearchOptions flat;
    flat.recursive = false;
    const FolderSearchResult top = search.run(qpath(folder.root()), QStringLiteral("булок"), flat);
    EXPECT_EQ(top.scanned, 10);  // «с булками 1» и 9 «других» в корне
    EXPECT_EQ(names(top), (std::vector<std::wstring>{L"с булками 1.pdf"}));
    EXPECT_TRUE(top.encrypted.isEmpty());

    // Отмена после первого файла: обход останавливается, результат помечен.
    Folder other;
    StateDb db2(other.db());
    FolderSearch cancelled(db2);
    ot::FolderSearchOptions one;
    one.threads = 1;
    QObject::connect(
        &cancelled, &FolderSearch::progress, &cancelled,
        [&](const ot::FolderSearchProgress& p) {
            if (p.filesDone >= 1) {
                cancelled.cancel();
            }
        },
        Qt::DirectConnection);
    const FolderSearchResult r = cancelled.run(qpath(other.root()), QStringLiteral("булок"), one);
    EXPECT_TRUE(r.cancelled);
    EXPECT_LT(r.extracted, 30);
}
