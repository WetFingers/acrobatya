// unit.search_ops.* — поиск через DocumentSession для панели «Найти» и «Расширенного поиска» (план 2.5;
// эталон 3.2; view-18, view-19): search.pdf — 7 совпадений «оттиск» без диакритики на трёх страницах,
// прогресс отдаёт каждое совпадение ровно раз, контекст — с метками совпадения; отмена до начала; скан без
// OCR (scans/scan-01.pdf) — текста нет; nav.pdf — закладки «Глава 1», «Глава 2» и PDF-вложение inner.pdf со
// словом «Inner».
#include <gtest/gtest.h>

#include <QFuture>
#include <QStringList>

#include <filesystem>
#include <memory>
#include <vector>

#include "core/util/features.h"
#include "services/document_session.h"
#include "services/search_ops.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace search = ot::search;

namespace {

QString fixture(const char* rel) {
    return QString::fromStdWString((ot::test::sourceDir() / "tests" / "fixtures" / rel).wstring());
}

search::Request request(const char* needle) {
    search::Request r;
    r.needle = QString::fromUtf8(needle);
    r.options.ignoreDiacritics = true;
    return r;
}

}  // namespace

TEST(search_ops, document_hits_progress_and_snippets) {
    OT_FEATURE("view-18");
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(fixture("text/search.pdf")));
    search::Request r = request("оттиск");
    r.snippets = true;
    int fresh = 0;
    int calls = 0;
    const search::Result result =
        search::run(session, r, std::make_shared<search::Control>(),
                    [&](const ot::SearchProgress& p, const std::vector<ot::SearchHit>& hits) {
                        ++calls;
                        fresh += static_cast<int>(hits.size());
                        EXPECT_LE(p.pagesDone, p.pageCount);
                    })
            .result();
    EXPECT_EQ(result.hits.size(), 7u);
    EXPECT_EQ(fresh, 7);  // каждое совпадение — ровно в одном вызове
    EXPECT_GE(calls, 1);
    ASSERT_EQ(result.snippets.size(), 7);
    EXPECT_TRUE(result.snippets[0].contains(QChar(2)));
    EXPECT_TRUE(result.snippets[0].contains(QChar(3)));
    EXPECT_EQ(result.pageCount, 3);
    EXPECT_EQ(result.pagesDone, 3);
    EXPECT_EQ(result.pagesWithText, 3);
    EXPECT_FALSE(result.aborted);
    EXPECT_FALSE(result.noText());
    session.close();
}

TEST(search_ops, cancelled_before_start_finds_nothing) {
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(fixture("text/search.pdf")));
    auto control = std::make_shared<search::Control>();
    control->cancel();
    const search::Result result = search::run(session, request("оттиск"), control).result();
    EXPECT_TRUE(result.aborted);
    EXPECT_TRUE(result.hits.empty());
    EXPECT_FALSE(result.noText());
    session.close();
}

TEST(search_ops, scan_without_ocr_has_no_text) {
    OT_FEATURE("view-18");
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(fixture("scans/scan-01.pdf")));
    const search::Result result = search::run(session, request("текст")).result();
    EXPECT_TRUE(result.hits.empty());
    EXPECT_TRUE(result.noText());
    session.close();
}

TEST(search_ops, bookmarks_and_pdf_attachments) {
    OT_FEATURE("view-19");
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(fixture("view/nav.pdf")));
    const QVector<search::BookmarkHit> marks = search::bookmarks(session, request("глава")).result();
    ASSERT_EQ(marks.size(), 2);
    EXPECT_EQ(marks[0].title, QString::fromUtf8("Глава 1"));
    EXPECT_EQ(marks[0].page, 1);
    EXPECT_EQ(marks[1].title, QString::fromUtf8("Глава 2"));
    EXPECT_EQ(marks[1].page, 2);
    search::Request word = request("глав");
    word.options.wholeWord = true;
    EXPECT_TRUE(search::bookmarks(session, word).result().isEmpty());

    search::Request inner = request("inner");
    inner.snippets = true;
    const QVector<search::AttachmentHits> found = search::attachments(session, inner).result();
    ASSERT_EQ(found.size(), 1);
    EXPECT_EQ(found[0].fileName, QStringLiteral("inner.pdf"));
    ASSERT_EQ(found[0].hits.size(), 1u);
    EXPECT_EQ(found[0].hits[0].page, 0);
    ASSERT_EQ(found[0].snippets.size(), 1);
    EXPECT_TRUE(found[0].snippets[0].contains(QStringLiteral("Inner")));
    session.close();
}

TEST(search_ops, matches_follows_options) {
    search::Request r = request("Глава");
    EXPECT_TRUE(search::matches(QString::fromUtf8("глава 3"), r));
    r.options.caseSensitive = true;
    EXPECT_FALSE(search::matches(QString::fromUtf8("глава 3"), r));
    r = request("\\d+");
    EXPECT_FALSE(search::matches(QStringLiteral("x 12"), r));  // не регулярное — буквально
    r.options.regex = true;
    EXPECT_TRUE(search::matches(QStringLiteral("x 12"), r));
    r.needle = QStringLiteral("(");
    EXPECT_FALSE(search::matches(QStringLiteral("("), r));  // неверное выражение — нет совпадения
}
