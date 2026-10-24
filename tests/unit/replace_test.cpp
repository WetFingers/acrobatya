// unit.replace.* — найти и заменить в ядре (план 3.3, п. 5; эталон 3.9; edit-05 — ядро, view-18 — замена):
// совпадения панели «Найти» сопоставляются с абзацами и позициями редактора, предпросмотр показывает фрагмент
// до и после; «заменить все» 5 вхождений на странице — fz_stext содержит замену ровно 5 раз, образца — ни
// разу, одна операция журнала (undo возвращает все 5); замена с образцом внутри не зацикливается; одно
// совпадение — только оно; совпадение через границу абзацев и в тексте «только чтение» — пропуск с причиной;
// по документу — своя операция на страницу, прогресс; регистр и «целое слово» — как у поиска.
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_resolver.h"
#include "core/edit/replace.h"
#include "core/edit/rewriter.h"
#include "core/edit/system_fonts.h"
#include "core/edit/text_blocks.h"
#include "core/mu/context.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::SearchOptions;
using ot::edit::FakeSystemFonts;
using ot::edit::FontLibrary;
using ot::edit::ReplaceMatch;
using ot::edit::Replacer;
using ot::edit::ReplaceReport;
using ot::edit::ReplaceSkip;
using ot::edit::Rewriter;

namespace {

struct Session {
    std::unique_ptr<Document> doc;
    FakeSystemFonts system;
    std::unique_ptr<FontLibrary> lib;
    std::unique_ptr<Rewriter> rewriter;
    std::unique_ptr<Replacer> replacer;

    explicit Session(std::unique_ptr<Document> d) : doc(std::move(d)) {
        lib = std::make_unique<FontLibrary>(doc->context().get());
        rewriter = std::make_unique<Rewriter>(*doc, *lib, system, ot::edit::FallbackFonts{});
        replacer = std::make_unique<Replacer>(*doc, *rewriter);
    }
};

Session fixture(const std::string& name) {
    return Session(Document::openOrThrow(ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name));
}

Session bytes(const std::vector<std::string>& pages) {
    return Session(Document::openOrThrowMemory(ot::test::makeTextPdf(ot::MuContext::global(), pages)));
}

int count(const std::string& text, const std::string& word) {
    int n = 0;
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + word.size())) {
        ++n;
    }
    return n;
}

std::string pageText(const Document& doc, int page = 0) {
    ot::TextOptions options;
    options.readingOrder = false;
    return ot::TextPage::build(doc, page, options)->text();
}

// Два абзаца, «cat» — пять раз.
const std::vector<std::string> kCats = {
    "BT /F1 12 Tf 72 700 Td (The cat sat on the mat while another cat watched) Tj 0 -15 Td (the third cat "
    "from "
    "the window.) Tj ET\n"
    "BT /F1 12 Tf 72 600 Td (A fourth cat and a fifth cat joined them.) Tj ET\n"};

}  // namespace

TEST(replace, find_maps_hits_to_paragraphs) {
    OT_FEATURE("edit-05");
    Session s = fixture("latin.pdf");
    const std::vector<ReplaceMatch> matches = s.replacer->find(0, "paragraph", {}, "block");
    ASSERT_EQ(matches.size(), 3u);  // «paragraphs.», «A paragraph», «one paragraph» (latin.pdf)
    const auto blocks = ot::edit::blocksForPage(ot::edit::ContentModel::build(*s.doc, 0));
    for (const ReplaceMatch& m : matches) {
        SCOPED_TRACE(m.context);
        ASSERT_TRUE(m.replaceable()) << ot::edit::replaceSkipCode(m.skip);
        ASSERT_GE(m.block, 0);
        EXPECT_EQ(m.found, "paragraph");
        EXPECT_EQ(m.range.size(), 9);
        EXPECT_NE(m.context.find("\x02paragraph\x03"), std::string::npos);
        EXPECT_NE(m.result.find("\x02"
                                "block\x03"),
                  std::string::npos);
        EXPECT_FALSE(m.hit.quads.empty());
        // Позиции — в тексте абзаца.
        const std::string& text = blocks[static_cast<std::size_t>(m.block)].text;
        EXPECT_NE(text.find("paragraph"), std::string::npos);
    }
    // Регистр и целое слово — как у поиска.
    SearchOptions exact;
    exact.caseSensitive = true;
    exact.wholeWord = true;
    EXPECT_EQ(s.replacer->find(0, "Paragraph", exact).size(), 0u);
    EXPECT_EQ(s.replacer->find(0, "paragraph", exact).size(), 2u);  // «paragraphs.» — не целое слово
}

TEST(replace, replace_all_five_one_operation) {
    OT_FEATURE("edit-05");
    OT_FEATURE("view-18");
    Session s = bytes(kCats);
    ASSERT_EQ(count(pageText(*s.doc), "cat"), 5);
    const ReplaceReport r = s.replacer->replacePage(0, "cat", {}, "dog");
    EXPECT_EQ(r.found, 5);
    EXPECT_EQ(r.replaced, 5);
    EXPECT_TRUE(r.skipped.empty());
    EXPECT_EQ(r.pages, std::vector<int>{0});
    EXPECT_EQ(r.dirty.size(), 2u);  // два абзаца
    const std::string text = pageText(*s.doc);
    EXPECT_EQ(count(text, "dog"), 5);
    EXPECT_EQ(count(text, "cat"), 0);
    // Одна операция журнала на страницу.
    EXPECT_EQ(s.doc->undoStack().size(), 1);
    EXPECT_EQ(s.doc->undoStack().undoName(), "Replace all");
    s.doc->undoStack().undo();
    EXPECT_EQ(count(pageText(*s.doc), "cat"), 5);
    EXPECT_EQ(count(pageText(*s.doc), "dog"), 0);
}

TEST(replace, replacement_containing_needle) {
    Session s = bytes(kCats);
    const ReplaceReport r = s.replacer->replacePage(0, "cat", {}, "big cat");
    EXPECT_EQ(r.replaced, 5);
    EXPECT_EQ(count(pageText(*s.doc), "big cat"), 5);
}

TEST(replace, replace_one) {
    Session s = bytes(kCats);
    const auto matches = s.replacer->find(0, "cat", {}, "dog");
    ASSERT_EQ(matches.size(), 5u);
    const ReplaceReport r = s.replacer->replaceOne(matches[3], "dog");
    EXPECT_EQ(r.replaced, 1);
    const std::string text = pageText(*s.doc);
    EXPECT_EQ(count(text, "cat"), 4);
    EXPECT_NE(text.find("A fourth dog and a fifth cat"), std::string::npos);
    EXPECT_EQ(s.doc->undoStack().undoName(), "Replace text");
}

TEST(replace, cross_block_and_read_only_skipped) {
    // Абзац меняет кегль на второй строке — два абзаца модели, одна строка поиска за другой; Tr 7 — только
    // для чтения.
    Session s =
        bytes({"BT /F1 12 Tf 72 700 Td (the first paragraph ends with alpha) Tj ET\n"
               "BT /F1 16 Tf 72 682 Td (beta begins another one) Tj ET\n"
               "BT /F1 12 Tf 7 Tr 72 600 Td (secret clip text) Tj ET\n"});
    const auto cross = s.replacer->find(0, "alpha beta", {}, "x");
    ASSERT_EQ(cross.size(), 1u);
    EXPECT_EQ(cross[0].skip, ReplaceSkip::CrossBlock);
    const auto clip = s.replacer->find(0, "secret", {}, "x");
    ASSERT_EQ(clip.size(), 1u);
    EXPECT_EQ(clip[0].skip, ReplaceSkip::ReadOnly);
    EXPECT_EQ(clip[0].readOnly, "clip");
    // «Заменить все» по таким — ничего не меняет, пропуски — в отчёте.
    const ReplaceReport r = s.replacer->replacePage(0, "secret", {}, "x");
    EXPECT_EQ(r.replaced, 0);
    ASSERT_EQ(r.skipped.size(), 1u);
    EXPECT_FALSE(s.doc->undoStack().canUndo());
    // Обычное слово рядом — заменяется.
    EXPECT_EQ(s.replacer->replacePage(0, "alpha", {}, "omega").replaced, 1);
    EXPECT_NE(pageText(*s.doc).find("ends with omega"), std::string::npos) << pageText(*s.doc);
}

TEST(replace, document_pages_and_progress) {
    Session s = bytes({kCats[0], "BT /F1 12 Tf 72 700 Td (no felines here) Tj ET\n", kCats[0]});
    const auto preview = s.replacer->findAll("cat", {}, "dog");
    EXPECT_EQ(preview.size(), 10u);
    std::vector<int> seen;
    const ReplaceReport r = s.replacer->replaceAll(
        "cat", {}, "dog", [&](const ot::edit::ReplaceProgress& p) { seen.push_back(p.pagesDone); });
    EXPECT_EQ(r.replaced, 10);
    EXPECT_EQ(r.pages, (std::vector<int>{0, 2}));
    EXPECT_EQ(seen.back(), 3);
    // Своя операция журнала на страницу; в меню одноимённые подряд — один шаг «Отменить: Заменить все».
    EXPECT_EQ(ot::test::undoSteps(*s.doc), 2);
    EXPECT_EQ(s.doc->undoStack().undoName(), "Replace all");
    for (int page : {0, 2}) {
        EXPECT_EQ(count(pageText(*s.doc, page), "dog"), 5);
    }
    // Отмена между страницами.
    Session c = bytes({kCats[0], kCats[0], kCats[0]});
    int calls = 0;
    const ReplaceReport stopped = c.replacer->replaceAll("cat", {}, "dog", {}, [&] { return ++calls > 1; });
    EXPECT_EQ(stopped.pages, std::vector<int>{0});
}
