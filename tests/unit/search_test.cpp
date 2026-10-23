// unit.search.* — поиск по документу (эталон 3.2, план 1.4) на tests/fixtures/text/search.pdf: «отти́ск» с
// ударением и без, с учётом и без учёта диакритики; «целое слово» не находит «оттиска»; регистр; регулярное
// выражение \d{4}; лигатура «ﬁ» находится как «fi»; квады накрывают слово; SearchSession — через страницы,
// счётчик «N из M», переходы F3/Shift+F3 по кругу, прогресс и отмена; в потоке документа — с кешем слоёв.
#include <gtest/gtest.h>

#include <QFuture>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/error.h"
#include "core/text/search.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "services/document_session.h"
#include "services/task_context.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::SearchHit;
using ot::SearchOptions;
using ot::SearchSession;
using ot::TextPage;

namespace fs = std::filesystem;

namespace {

const char* const kAccented =
    "отти\xCC\x81"
    "ск";  // «отти́ск»: и + U+0301

fs::path searchPdf() { return ot::test::sourceDir() / "tests" / "fixtures" / "text" / "search.pdf"; }

class SearchTest : public ::testing::Test {
protected:
    void SetUp() override {
        doc_ = Document::openOrThrow(searchPdf());
        for (int p = 0; p < doc_->pageCount(); ++p) {
            pages_.push_back(TextPage::build(*doc_, p, SearchSession::textOptions()));
        }
    }

    std::vector<SearchHit> onPage(int page, const std::string& needle, const SearchOptions& o = {}) {
        return ot::searchPage(*pages_[page], needle, o);
    }
    std::vector<std::string> texts(int page, const std::vector<SearchHit>& hits) {
        std::vector<std::string> out;
        for (const auto& h : hits) {
            out.push_back(pages_[page]->text(h.firstChar, h.lastChar));
        }
        return out;
    }
    SearchSession::PageSource source() {
        return [this](int p) { return pages_.at(p); };
    }

    std::unique_ptr<Document> doc_;
    std::vector<std::shared_ptr<const TextPage>> pages_;
};

SearchOptions opts(bool caseSensitive, bool wholeWord, bool regex, bool ignoreDiacritics) {
    SearchOptions o;
    o.caseSensitive = caseSensitive;
    o.wholeWord = wholeWord;
    o.regex = regex;
    o.ignoreDiacritics = ignoreDiacritics;
    return o;
}

}  // namespace

TEST_F(SearchTest, accented_needle_with_and_without_diacritics) {
    OT_FEATURE("view-18");
    // С диакритикой: «отти́ск» находит только себя; без учёта диакритики — все четыре «оттиск…» страницы.
    EXPECT_EQ(texts(0, onPage(0, kAccented)), (std::vector<std::string>{kAccented}));
    EXPECT_EQ(onPage(0, kAccented, opts(false, false, false, true)).size(), 4u);
    // Без ударения: обычный поиск не видит «отти́ск», без учёта диакритики — видит.
    EXPECT_EQ(texts(0, onPage(0, "оттиск")), (std::vector<std::string>{"Оттиск", "оттиск", "оттиск"}));
    const auto all = texts(0, onPage(0, "оттиск", opts(false, false, false, true)));
    EXPECT_EQ(all, (std::vector<std::string>{"Оттиск", kAccented, "оттиск", "оттиск"}));
}

TEST_F(SearchTest, whole_word_skips_longer_words) {
    OT_FEATURE("view-18");
    const auto hits = onPage(0, "оттиск", opts(false, true, false, true));
    EXPECT_EQ(texts(0, hits), (std::vector<std::string>{"Оттиск", kAccented, "оттиск"}));
    for (const auto& h : hits) {
        const auto& chars = pages_[0]->chars();
        ASSERT_LT(h.lastChar + 1, static_cast<int>(chars.size()));
        EXPECT_NE(chars[h.lastChar + 1].c, U'а');  // не «оттиска»
    }
    EXPECT_EQ(onPage(0, "оттиска", opts(false, true, false, false)).size(), 1u);
    EXPECT_TRUE(onPage(0, "ттиск", opts(false, true, false, false)).empty());
}

TEST_F(SearchTest, case_sensitivity) {
    EXPECT_EQ(onPage(1, "оттиск").size(), 3u);
    EXPECT_EQ(texts(1, onPage(1, "оттиск", opts(true, false, false, false))),
              (std::vector<std::string>{"оттиск"}));
    EXPECT_EQ(texts(1, onPage(1, "ОТТИСК", opts(true, false, false, false))),
              (std::vector<std::string>{"ОТТИСК"}));
    EXPECT_EQ(texts(1, onPage(1, "Оттиск", opts(true, false, false, false))),
              (std::vector<std::string>{"Оттиск"}));
}

TEST_F(SearchTest, regex_four_digits) {
    OT_FEATURE("view-18");
    const SearchOptions re = opts(false, false, true, false);
    EXPECT_EQ(texts(0, onPage(0, "\\d{4}", re)), (std::vector<std::string>{"1999", "2026", "1234"}));
    EXPECT_EQ(texts(0, onPage(0, "\\d{4}", opts(false, true, true, false))),
              (std::vector<std::string>{"1999", "2026"}));  // 12345 — не целое слово
    EXPECT_EQ(texts(1, onPage(1, "\\d{4}", re)), (std::vector<std::string>{"2027"}));
    EXPECT_EQ(texts(0, onPage(0, "оттиск[а-я]", re)), (std::vector<std::string>{"оттиска"}));
    EXPECT_THROW((void)onPage(0, "(", re), ot::MuError);
}

TEST_F(SearchTest, ligatures_match_letters) {
    const auto fi = onPage(0, "fi");
    ASSERT_EQ(fi.size(), 2u);  // «ﬁнал» (U+FB01) и обычное «fi»
    EXPECT_EQ(pages_[0]->chars()[fi[0].firstChar].c, 0xFB01u);
    EXPECT_EQ(fi[0].firstChar, fi[0].lastChar);
    const auto word = onPage(0, "fiнал", opts(false, true, false, false));
    ASSERT_EQ(word.size(), 1u);
    EXPECT_EQ(onPage(0, "flаг").size(), 1u);
}

TEST_F(SearchTest, quads_cover_the_match) {
    const auto hits = onPage(0, "оттиска");
    ASSERT_EQ(hits.size(), 1u);
    const auto& h = hits[0];
    EXPECT_EQ(h.page, 0);
    EXPECT_EQ(h.endPage, 0);
    ASSERT_EQ(h.quads.size(), 1u);  // одна строка — один квад
    EXPECT_EQ(h.quads[0].page, 0);
    const ot::RectF q = h.quads[0].quad.bounds();
    for (int i = h.firstChar; i <= h.lastChar; ++i) {
        const ot::RectF c = pages_[0]->chars()[i].quad.bounds();
        EXPECT_GE(c.x0, q.x0 - 0.01f);
        EXPECT_LE(c.x1, q.x1 + 0.01f);
    }
    EXPECT_TRUE(onPage(0, "").empty());
    EXPECT_TRUE(onPage(2, "оттиск").empty());
}

TEST_F(SearchTest, session_across_pages_with_counter) {
    OT_FEATURE("view-18");
    SearchSession s("оттиск", opts(false, false, false, true));
    std::vector<ot::SearchProgress> progress;
    s.run(doc_->pageCount(), source(), [&](const ot::SearchProgress& p) { progress.push_back(p); });
    EXPECT_TRUE(s.finished());
    EXPECT_FALSE(s.aborted());
    ASSERT_EQ(s.count(), 7);  // 4 на первой странице, 3 на второй
    EXPECT_EQ(s.hits()[3].page, 0);
    EXPECT_EQ(s.hits()[4].page, 1);
    EXPECT_EQ(s.hitsOnPage(1), (std::vector<int>{4, 5, 6}));
    EXPECT_TRUE(s.hitsOnPage(2).empty());
    ASSERT_FALSE(progress.empty());
    EXPECT_TRUE(progress.back().finished);
    EXPECT_EQ(progress.back().pagesDone, 3);
    EXPECT_EQ(progress.back().pageCount, 3);
    EXPECT_EQ(progress.back().hits, 7);

    // «3 из 17»: не выбрано → F3 по кругу → Shift+F3.
    EXPECT_EQ(s.counter(), std::make_pair(0, 7));
    EXPECT_EQ(s.next(), 0);
    EXPECT_EQ(s.counter(), std::make_pair(1, 7));
    EXPECT_EQ(s.previous(), 6);
    EXPECT_EQ(s.counter(), std::make_pair(7, 7));
    EXPECT_EQ(s.next(), 0);
    EXPECT_EQ(s.selectFrom(1), 4);  // с текущей страницы 2 — первое на ней
    EXPECT_EQ(s.counter(), std::make_pair(5, 7));
    EXPECT_EQ(s.selectFrom(2), 0);  // дальше нет — по кругу к первому
    ASSERT_NE(s.currentHit(), nullptr);
    EXPECT_EQ(s.currentHit()->page, 0);

    SearchSession none("нетакогослова", {});
    none.run(doc_->pageCount(), source());
    EXPECT_EQ(none.count(), 0);
    EXPECT_EQ(none.next(), -1);
    EXPECT_EQ(none.counter(), std::make_pair(0, 0));
    EXPECT_EQ(none.currentHit(), nullptr);
}

TEST_F(SearchTest, session_cancel_keeps_found) {
    SearchSession s("оттиск", opts(false, false, false, true));
    int asked = 0;
    s.run(
        doc_->pageCount(),
        [&](int p) {
            ++asked;
            return pages_.at(p);
        },
        {}, [&] { return asked >= 1; });  // отмена после первой страницы
    EXPECT_TRUE(s.aborted());
    EXPECT_FALSE(s.finished());
    EXPECT_EQ(asked, 1);
    EXPECT_LE(s.count(), 4);
    for (const auto& h : s.hits()) {
        EXPECT_EQ(h.page, 0);
    }
    // Повторный запуск начинает заново.
    s.run(doc_->pageCount(), source());
    EXPECT_TRUE(s.finished());
    EXPECT_EQ(s.count(), 7);
}

TEST_F(SearchTest, session_skips_pages_without_text_layer) {
    SearchSession s("оттиск", opts(false, false, false, true));
    s.run(doc_->pageCount(), [&](int p) -> std::shared_ptr<const TextPage> {
        if (p == 0) {
            return nullptr;  // слой не построился (битая страница) — страница пропускается
        }
        return pages_.at(p);
    });
    EXPECT_TRUE(s.finished());
    EXPECT_EQ(s.count(), 3);
}

// Контекст совпадения «Расширенного поиска» (2.5): строка с совпадением между \x02 и \x03, обрезка по radius
// символов с «…» (пробелы у края обрезки убираются).
TEST_F(SearchTest, snippet_marks_match_in_line) {
    OT_FEATURE("view-19");
    const auto hits = onPage(0, "оттиска", opts(false, true, false, false));
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(ot::searchSnippet(*pages_[0], hits[0], 100),
              "Без ударения: оттиск, а в родительном падеже — \x02оттиска\x03.");
    EXPECT_EQ(ot::searchSnippet(*pages_[0], hits[0], 10), "…падеже — \x02оттиска\x03.");
    const auto years = onPage(0, "1999");
    ASSERT_EQ(years.size(), 1u);
    EXPECT_EQ(ot::searchSnippet(*pages_[0], years[0], 7),
              "Годы \x02"
              "1999\x03 и 2026…");
}

TEST(search_session, runs_in_document_thread_with_text_cache) {
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(searchPdf().wstring())));
    SearchOptions o;
    o.ignoreDiacritics = true;
    auto future = session.post([o](ot::TaskContext& tc) {
        SearchSession s("оттиск", o);
        s.run(
            tc.document().pageCount(), [&](int p) { return tc.textPage(p, SearchSession::textOptions()); },
            {}, [&] { return tc.aborted(); });
        return s;
    });
    const SearchSession s = future.result();
    EXPECT_EQ(s.count(), 7);
    // Слои остались в кеше сессии: тот же объект.
    const auto cached =
        session.postSync([](ot::TaskContext& tc) { return tc.textPage(1, SearchSession::textOptions()); });
    EXPECT_EQ(session.textPage(1, SearchSession::textOptions()).result(), cached);
    EXPECT_NE(session.textPage(1).result(), cached);  // слой с порядком чтения — отдельная запись кеша
    session.close();
}
