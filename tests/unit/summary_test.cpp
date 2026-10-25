// unit.summary.* — сводка комментариев (план 5.4; core/annot/summary.h; comment-25): «документ и комментарии
// с выносками» — лист на страницу документа с комментариями, текст каждого комментария и ответа — в текстовом
// слое сводки (кириллица Segoe UI), линии-связки нарисованы; длинная колонка переходит на следующий лист;
// фильтр панели — в сводке только видимые; «только комментарии» и «на отдельных страницах» — свои числа
// листов; диапазон страниц; кегль меняет число листов.
#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/annot/summary.h"
#include "core/doc/document.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::annot::SummaryLayout;
using ot::annot::SummaryOptions;

namespace {

std::unique_ptr<Document> reviewed() {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 3));
    const struct {
        int page;
        float y;
        const char* author;
        const char* text;
        AnnotType type;
    } items[] = {
        {0, 120, "Анна", "Проверить первый абзац", AnnotType::Text},
        {0, 300, "Борис", "Цифры за квартал", AnnotType::Square},
        {0, 500, "Анна", "Лишнее предложение", AnnotType::Square},
        {2, 200, "Вера", "Подпись внизу", AnnotType::Text},
        {2, 600, "Борис", "Дата договора", AnnotType::Circle},
    };
    AnnotRef first;
    for (const auto& it : items) {
        AnnotProps p = ot::annot::toolDefaults(it.type);
        p.rect = {80, it.y, it.type == AnnotType::Text ? 100.0f : 260.0f, it.y + 20};
        p.author = it.author;
        p.contents = it.text;
        const AnnotRef ref = ot::annot::create(*doc, it.page, p);
        if (!first.valid()) {
            first = ref;
        }
    }
    ot::annot::addReply(*doc, first, {.author = "Борис", .contents = "Согласен, поправлю"});
    ot::annot::setStatus(*doc, first, ot::annot::kReviewModel, "Accepted", "Борис");
    return doc;
}

SummaryOptions options(SummaryLayout layout) {
    SummaryOptions o;
    o.layout = layout;
    o.texts.title = "Сводка комментариев";
    o.texts.pageTitle = [](int page) { return "Страница " + std::to_string(page); };
    o.texts.typeName = [](const ot::annot::Comment& c) {
        return c.depth > 0 ? std::string("Ответ") : std::string(ot::annot::annotTypeName(c.type));
    };
    o.texts.stateName = [](ot::annot::ReviewState s) { return std::string(ot::annot::reviewStateName(s)); };
    o.texts.noComments = "Комментариев нет";
    return o;
}

// Текст всех страниц сводки.
std::string allText(const std::string& pdf, int* pages = nullptr) {
    auto doc = Document::openOrThrowMemory(pdf);
    if (pages != nullptr) {
        *pages = doc->pageCount();
    }
    std::string out;
    for (int p = 0; p < doc->pageCount(); ++p) {
        out += ot::TextPage::build(*doc, p)->text();
        out += '\n';
    }
    return out;
}

bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

}  // namespace

TEST(summary, document_and_comments_with_callouts) {
    OT_FEATURE("comment-25");
    auto doc = reviewed();
    const ot::annot::CommentTree tree = ot::annot::buildComments(*doc);
    const ot::annot::SummaryResult result =
        ot::annot::buildSummary(*doc, tree, options(SummaryLayout::DocumentAndComments));
    EXPECT_EQ(result.comments, 5);
    EXPECT_EQ(result.pages, 2);  // страницы 1 и 3 (на второй комментариев нет)
    int pages = 0;
    const std::string text = allText(result.pdf, &pages);
    EXPECT_EQ(pages, 2);
    for (const char* t :
         {"Проверить первый абзац", "Цифры за квартал", "Лишнее предложение", "Подпись внизу",
          "Дата договора", "Согласен, поправлю", "Анна", "Борис", "Accepted", "Страница 1", "Страница 3"}) {
        EXPECT_TRUE(has(text, t)) << t;
    }
    // Страница документа — на листе (номер страницы демо-PDF есть только на второй и далее).
    EXPECT_TRUE(has(text, "3"));
    // С пустыми страницами — лист и на вторую.
    SummaryOptions withEmpty = options(SummaryLayout::DocumentAndComments);
    withEmpty.includeEmptyPages = true;
    EXPECT_EQ(ot::annot::buildSummary(*doc, tree, withEmpty).pages, 3);
}

TEST(summary, filtered_summary_shows_only_visible) {
    OT_FEATURE("comment-25");
    auto doc = reviewed();
    const ot::annot::CommentTree tree = ot::annot::buildComments(*doc);
    SummaryOptions o = options(SummaryLayout::CommentsOnly);
    o.filter.authors = {"Анна"};
    const ot::annot::SummaryResult result = ot::annot::buildSummary(*doc, tree, o);
    EXPECT_EQ(result.comments, 2);
    const std::string text = allText(result.pdf);
    EXPECT_TRUE(has(text, "Проверить первый абзац"));
    EXPECT_TRUE(has(text, "Лишнее предложение"));
    EXPECT_FALSE(has(text, "Цифры за квартал"));
    EXPECT_FALSE(has(text, "Подпись внизу"));
    EXPECT_FALSE(has(text, "Дата договора"));
    EXPECT_TRUE(has(text, "Сводка комментариев"));
}

TEST(summary, comments_only_and_separate_pages) {
    OT_FEATURE("comment-25");
    auto doc = reviewed();
    const ot::annot::CommentTree tree = ot::annot::buildComments(*doc);
    const ot::annot::SummaryResult only =
        ot::annot::buildSummary(*doc, tree, options(SummaryLayout::CommentsOnly));
    EXPECT_EQ(only.pages, 1);
    EXPECT_EQ(only.comments, 5);
    const ot::annot::SummaryResult separate =
        ot::annot::buildSummary(*doc, tree, options(SummaryLayout::SeparatePages));
    EXPECT_EQ(separate.pages, 4);  // страница 1 + её комментарии, страница 3 + её комментарии
    const std::string text = allText(separate.pdf);
    EXPECT_TRUE(has(text, "Дата договора"));
    // Диапазон: только первая страница.
    SummaryOptions range = options(SummaryLayout::CommentsOnly);
    range.firstPage = 0;
    range.lastPage = 0;
    const ot::annot::SummaryResult first = ot::annot::buildSummary(*doc, tree, range);
    EXPECT_EQ(first.comments, 3);
    EXPECT_FALSE(has(allText(first.pdf), "Дата договора"));
    // Крупный кегль и много комментариев — колонка переходит на следующий лист.
    auto many = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    for (int i = 0; i < 30; ++i) {
        AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
        p.rect = {80, 40.0f + static_cast<float>(i) * 25, 100, 60.0f + static_cast<float>(i) * 25};
        p.contents = "Комментарий номер " + std::to_string(i + 1) + " с длинным текстом для переноса строк";
        p.author = "Анна";
        ot::annot::create(*many, 0, p);
    }
    SummaryOptions big = options(SummaryLayout::DocumentAndComments);
    big.fontSize = 14;
    const ot::annot::SummaryResult overflow =
        ot::annot::buildSummary(*many, ot::annot::buildComments(*many), big);
    EXPECT_GT(overflow.pages, 1);
    const std::string text30 = allText(overflow.pdf);
    EXPECT_TRUE(has(text30, "Комментарий номер 30"));
    EXPECT_TRUE(has(text30, "Комментарий номер 1"));
}
