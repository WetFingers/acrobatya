// unit.rewriter.* — запись абзаца в контент-поток (план 3.3, п. 3; эталон 3.9 «Модель», «Граничные случаи»;
// edit-01, edit-02 — ядро, edit-03): запись без правки и со вставленным словом — текст абзаца в новой модели
// и в stext, рендер вне абзаца (рамки до и после ± 2 pt) — pixels == 0, нетронутые операции — те же байты в
// том же порядке; удаление абзаца целиком убирает его текстовые объекты; undo возвращает потоки байт в байт и
// снимает новый шрифт; блок, наследующий Tf и цвет от абзаца, не меняется; MCID у изменённого абзаца те же
// (BDC снаружи и внутри BT); поток, общий для двух страниц, копируется только для своей; подписанный документ
// — отказ; модель, изменённая после построения редактора, — Stale; страница с /Rotate 90; кириллица в
// латинском подмножестве — новый CID-шрифт; набор в стёртый абзац — новым потоком на том же месте.
#include <gtest/gtest.h>

#include <QString>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_resolver.h"
#include "core/edit/paragraph_editor.h"
#include "core/edit/rewriter.h"
#include "core/edit/system_fonts.h"
#include "core/edit/text_blocks.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"
#include "core/render/image_compare.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"
#include "services/document_session.h"
#include "services/task_context.h"
#include "support/qt_app.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::edit::ContentModel;
using ot::edit::FakeSystemFonts;
using ot::edit::FontLibrary;
using ot::edit::ParagraphEditor;
using ot::edit::Rewriter;
using ot::edit::RewriteReport;
using ot::edit::TextBlock;

namespace {

constexpr float kDpi = 96;

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

std::u32string u32(const char* s) { return ot::utf8::decode(s); }

// Редактор, библиотека шрифтов и Rewriter над одним документом.
struct Session {
    std::unique_ptr<Document> doc;
    FakeSystemFonts system;
    std::unique_ptr<FontLibrary> lib;
    std::unique_ptr<Rewriter> rewriter;

    explicit Session(std::unique_ptr<Document> d) : doc(std::move(d)) {
        lib = std::make_unique<FontLibrary>(doc->context().get());
        rewriter = std::make_unique<Rewriter>(*doc, *lib, system, ot::edit::FallbackFonts{});
    }
    ContentModel model(int page = 0) const { return ContentModel::build(*doc, page); }
    std::vector<TextBlock> blocks(int page = 0) const { return ot::edit::blocksForPage(model(page)); }
    ParagraphEditor editor(std::size_t block, int page = 0) const {
        const ContentModel m = model(page);
        const auto b = ot::edit::blocksForPage(m);
        return ParagraphEditor::fromBlock(m, b.at(block), b);
    }
};

Session open(const std::string& fixture) { return Session(Document::openOrThrow(editFixture(fixture))); }
Session openBytes(const std::string& bytes) { return Session(Document::openOrThrowMemory(bytes)); }

std::string pageText(const Document& doc, int page = 0) {
    ot::TextOptions options;
    options.readingOrder = false;
    return ot::TextPage::build(doc, page, options)->text();
}

std::string squeeze(std::string s) {
    std::string out;
    for (char c : s) {
        const bool space = c == ' ' || c == '\n' || c == '\r' || c == '\t';
        if (space) {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// Сырые (сжатые) байты потоков /Contents страницы.
std::vector<std::string> rawContents(const Document& doc, int page = 0) {
    std::vector<std::string> out;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    std::vector<pdf_obj*> refs;
    ot::mu::call(ctx, [&] {
        pdf_obj* c = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Contents));
        if (pdf_is_array(ctx, c)) {
            for (int i = 0; i < pdf_array_len(ctx, c); ++i) {
                refs.push_back(pdf_array_get(ctx, c, i));
            }
        } else {
            refs.push_back(c);
        }
    });
    for (pdf_obj* ref : refs) {
        ot::mu::BufferHandle buf(ctx);
        ot::mu::call(ctx, [&] { buf.reset(pdf_load_raw_stream(ctx, ref)); });
        unsigned char* data = nullptr;
        const size_t len = fz_buffer_storage(ctx, buf, &data);
        out.emplace_back(reinterpret_cast<const char*>(data), len);
    }
    return out;
}

int fontCount(const Document& doc, int page = 0) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int n = 0;
    ot::mu::call(ctx, [&] {
        pdf_obj* res =
            pdf_dict_get_inheritable(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Resources));
        n = pdf_dict_len(ctx, pdf_dict_get(ctx, res, PDF_NAME(Font)));
    });
    return n;
}

// Операции before, кроме touched, идут в after в том же порядке с теми же байтами.
::testing::AssertionResult untouchedKept(const ContentModel& before, const ContentModel& after,
                                         const std::vector<bool>& touched) {
    std::vector<std::string> next;
    for (std::size_t i = 0; i < after.operations().size(); ++i) {
        if (!after.streams()[static_cast<std::size_t>(after.operations()[i].stream)].form) {
            next.push_back(after.opText(static_cast<int>(i)));
        }
    }
    std::size_t k = 0;
    for (std::size_t i = 0; i < before.operations().size(); ++i) {
        if (touched[i] || before.streams()[static_cast<std::size_t>(before.operations()[i].stream)].form) {
            continue;
        }
        const std::string text = before.opText(static_cast<int>(i));
        while (k < next.size() && next[k] != text) {
            ++k;
        }
        if (k == next.size()) {
            return ::testing::AssertionFailure() << "операция " << i << " «" << text << "» пропала";
        }
        ++k;
    }
    return ::testing::AssertionSuccess();
}

// Операции, которые запись абзаца может тронуть: его текстовые объекты целиком.
std::vector<bool> touchedOps(const ContentModel& m, const ParagraphEditor& e) {
    std::vector<bool> touched(m.operations().size(), false);
    for (int op : e.source().ops) {
        const int t = m.operations()[static_cast<std::size_t>(op)].textObject;
        if (t < 0) {
            touched[static_cast<std::size_t>(op)] = true;
            continue;
        }
        const auto& obj = m.textObjects()[static_cast<std::size_t>(t)];
        const int end = obj.end >= 0 ? obj.end : static_cast<int>(m.operations().size()) - 1;
        for (int i = obj.begin; i <= end; ++i) {
            touched[static_cast<std::size_t>(i)] = true;
        }
    }
    return touched;
}

// Рендер вне рамок абзаца (до и после, ± 2 pt) — тот же до пикселя.
long long diffOutside(const ot::Bitmap& before, const Document& doc, const std::vector<ot::RectF>& rects,
                      int page = 0) {
    const ot::Bitmap after = doc.renderPage(page, {kDpi, false});
    std::vector<ot::IRect> masks;
    for (const auto& r : rects) {
        if (r.valid()) {
            masks.push_back(ot::pixelRect(r, kDpi, 2));
        }
    }
    return ot::diffPixelsOutside(before, after, masks);
}

}  // namespace

TEST(rewriter, rewrite_unchanged_paragraph) {
    OT_FEATURE("edit-01");
    Session s = open("latin.pdf");
    const ContentModel before = s.model();
    const ot::Bitmap shot = s.doc->renderPage(0, {kDpi, false});
    ParagraphEditor e = s.editor(0);
    const auto touched = touchedOps(before, e);
    const std::string text = e.utf8();
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.streams, 1);
    EXPECT_EQ(r.lines, 2);
    EXPECT_FALSE(e.modified());
    EXPECT_TRUE(e.layoutValid());
    const ContentModel after = s.model();
    EXPECT_TRUE(untouchedKept(before, after, touched));
    // Абзац — тот же текст в модели и в stext.
    const auto blocks = ot::edit::blocksForPage(after);
    EXPECT_EQ(blocks.front().text, text);
    EXPECT_NE(squeeze(pageText(*s.doc)).find(text), std::string::npos);
    EXPECT_EQ(diffOutside(shot, *s.doc, {r.dirty}), 0);
    // Новый источник — операторы новой модели с тем же текстом.
    std::string src;
    for (int op : e.source().ops) {
        src += after.spanText(after.operations()[static_cast<std::size_t>(op)].span);
    }
    EXPECT_EQ(src, e.source().text);
}

TEST(rewriter, insert_word_reflows_paragraph) {
    OT_FEATURE("edit-02");
    for (const char* name :
         {"latin.pdf", "cyr-cid.pdf", "align-justify.pdf", "align-center.pdf", "align-right.pdf",
          "tz-tc-tw.pdf", "two-streams.pdf", "tj-kerning.pdf", "base14.pdf", "type1-diff.pdf"}) {
        SCOPED_TRACE(name);
        Session s = open(name);
        const ContentModel before = s.model();
        const ot::Bitmap shot = s.doc->renderPage(0, {kDpi, false});
        ParagraphEditor e = s.editor(0);
        const auto touched = touchedOps(before, e);
        const int lines = e.lineCount();
        const auto& first = e.layout().lines.front();
        const int at = e.wordAt(first.begin + (first.end - first.begin) / 2).begin;
        e.insert(at, u32("inserted "));
        const std::string text = e.utf8();
        const RewriteReport r = s.rewriter->rewrite(e);
        ASSERT_TRUE(r.ok()) << r.message;
        EXPECT_GE(r.lines, lines);
        const ContentModel after = s.model();
        EXPECT_TRUE(untouchedKept(before, after, touched));
        // Абзац растёт вниз, соседи на месте: выросший до следующего абзаца склеивается с ним в один блок.
        const auto blocks = ot::edit::blocksForPage(after);
        EXPECT_EQ(blocks.front().text.substr(0, text.size()), text);
        EXPECT_NE(squeeze(pageText(*s.doc)).find("inserted"), std::string::npos);
        EXPECT_EQ(diffOutside(shot, *s.doc, {r.dirty}), 0);
        // Раскладка редактора — по новой записи: курсор у глифов новой модели.
        const auto& span = after.spans()[static_cast<std::size_t>(
            after.operations()[static_cast<std::size_t>(e.source().ops.front())].span)];
        EXPECT_NEAR(e.caret(0).bottom.x, after.glyphs()[static_cast<std::size_t>(span.firstGlyph)].origin.x,
                    0.05);
    }
}

TEST(rewriter, delete_paragraph_removes_text_objects) {
    Session s = open("latin.pdf");
    const ContentModel before = s.model();
    const auto raw = rawContents(*s.doc);
    const ot::Bitmap shot = s.doc->renderPage(0, {kDpi, false});
    ParagraphEditor e = s.editor(1);  // абзац из нескольких BT/ET
    const std::string text = e.utf8();
    const auto touched = touchedOps(before, e);
    e.erase({0, e.size()});
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_GE(r.textObjectsRemoved, 1);
    const ContentModel after = s.model();
    EXPECT_EQ(after.textObjects().size() + static_cast<std::size_t>(r.textObjectsRemoved),
              before.textObjects().size());
    EXPECT_TRUE(untouchedKept(before, after, touched));
    EXPECT_EQ(squeeze(pageText(*s.doc)).find(text.substr(0, 20)), std::string::npos);
    EXPECT_EQ(ot::edit::blocksForPage(after).size(), ot::edit::blocksForPage(before).size() - 1);
    EXPECT_EQ(diffOutside(shot, *s.doc, {r.dirty}), 0);
    EXPECT_TRUE(e.source().ops.empty());
    // Undo — потоки байт в байт.
    s.doc->undoStack().undo();
    EXPECT_EQ(rawContents(*s.doc), raw);
}

TEST(rewriter, undo_restores_streams_and_fonts) {
    Session s = open("latin.pdf");
    const auto raw = rawContents(*s.doc);
    const int fonts = fontCount(*s.doc);
    ParagraphEditor e = s.editor(0);
    e.insert(0, u32("Привет! "));  // кириллицы в латинском подмножестве нет — новый шрифт
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(fontCount(*s.doc), fonts + 1);
    EXPECT_NE(rawContents(*s.doc), raw);
    EXPECT_NE(squeeze(pageText(*s.doc)).find("Привет! The editor"), std::string::npos);
    ASSERT_FALSE(r.notices.empty());
    EXPECT_EQ(r.notices.front().kind, ot::edit::FontNoticeKind::SubsetExtended);
    ASSERT_TRUE(s.doc->undoStack().canUndo());
    EXPECT_EQ(s.doc->undoStack().undoName(), "Edit text");
    s.doc->undoStack().undo();
    EXPECT_EQ(rawContents(*s.doc), raw);
    EXPECT_EQ(fontCount(*s.doc), fonts);
    s.doc->undoStack().redo();
    EXPECT_NE(squeeze(pageText(*s.doc)).find("Привет!"), std::string::npos);
}

TEST(rewriter, state_restored_for_following_text) {
    // Второй блок наследует от абзаца Tf, Tc и красный цвет — после правки абзаца он тот же.
    const std::string bytes = ot::test::makeTextPdf(
        ot::MuContext::global(),
        {"BT /F1 12 Tf 0.5 Tc 1 0 0 rg 72 700 Td (Hello world from the) Tj 0 -16 Td (first paragraph) Tj ET\n"
         "BT 72 600 Td (Third block inherits state) Tj ET\n"
         "0 0 1 rg 72 500 100 20 re f\n"});
    Session s = openBytes(bytes);
    const ot::Bitmap shot = s.doc->renderPage(0, {kDpi, false});
    ASSERT_EQ(s.blocks().size(), 2u);
    ParagraphEditor e = s.editor(0);
    e.applyStyle({0, e.size()},
                 ot::edit::StyleChange{.size = 20.0f, .color = 0xFF00AA00u, .charSpacing = 0.0f});
    e.insert(5, u32(" big"));
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(diffOutside(shot, *s.doc, {r.dirty}), 0);
    const ContentModel after = s.model();
    const auto blocks = ot::edit::blocksForPage(after);
    ASSERT_EQ(blocks.size(), 2u);
    const auto& third = after.spans()[static_cast<std::size_t>(blocks[1].spans.front())];
    EXPECT_EQ(after.fonts()[static_cast<std::size_t>(third.font)].resource, "F1");
    EXPECT_NEAR(third.size, 12, 1e-6);
    EXPECT_NEAR(third.charSpace, 0.5, 1e-6);
    EXPECT_EQ(third.fill, 0xFFFF0000u);
    const auto& edited = after.spans()[static_cast<std::size_t>(blocks[0].spans.front())];
    EXPECT_EQ(edited.fill, 0xFF00AA00u);
    EXPECT_NEAR(edited.fontSize, 20, 0.01);
}

TEST(rewriter, mcid_kept_for_tagged_text) {
    const std::string bytes = ot::test::makeTextPdf(
        ot::MuContext::global(),
        {"/P <</MCID 0>> BDC BT /F1 12 Tf 72 700 Td (Tagged paragraph one) Tj ET EMC\n"
         "BT /F1 12 Tf 72 600 Td /Span <</MCID 1>> BDC (Inner tagged span) Tj EMC ET\n"},
        false, true);
    Session s = openBytes(bytes);
    for (std::size_t b = 0; b < 2; ++b) {
        ParagraphEditor e = s.editor(b);
        e.insert(e.size(), u32(" and more words"));
        const RewriteReport r = s.rewriter->rewrite(e);
        ASSERT_TRUE(r.ok()) << r.message;
    }
    const ContentModel m = s.model();
    const auto blocks = ot::edit::blocksForPage(m);
    ASSERT_EQ(blocks.size(), 2u);
    for (std::size_t b = 0; b < 2; ++b) {
        EXPECT_NE(blocks[b].text.find("and more words"), std::string::npos);
        for (int si : blocks[b].spans) {
            EXPECT_EQ(m.spans()[static_cast<std::size_t>(si)].mcid, static_cast<int>(b)) << b;
        }
    }
}

TEST(rewriter, shared_stream_copied_for_page) {
    const std::string bytes =
        ot::test::makeTextPdf(ot::MuContext::global(),
                              {"BT /F1 12 Tf 72 700 Td (Shared content stream) Tj ET\n", "unused\n"}, true);
    Session s = openBytes(bytes);
    const auto page1 = rawContents(*s.doc, 1);
    const std::string text1 = pageText(*s.doc, 1);
    ParagraphEditor e = s.editor(0);
    e.insert(0, u32("Edited "));
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(rawContents(*s.doc, 1), page1);
    EXPECT_EQ(pageText(*s.doc, 1), text1);
    EXPECT_NE(pageText(*s.doc, 0).find("Edited Shared"), std::string::npos);
}

TEST(rewriter, refuses_signed_and_stale) {
    {
        Session s(
            Document::openOrThrow(ot::test::sourceDir() / "tests" / "fixtures" / "sig" / "signed-bb.pdf"));
        ASSERT_TRUE(s.doc->isSigned());
        ParagraphEditor e;
        const RewriteReport r = s.rewriter->rewrite(e);
        EXPECT_EQ(r.error, ot::edit::RewriteError::Signed);
    }
    {
        // Абзац ниже правится по старой модели после того, как правка выше сдвинула операции потока.
        Session s = open("latin.pdf");
        ParagraphEditor below = s.editor(2);
        ParagraphEditor above = s.editor(1);
        above.insert(0, u32("More "));
        ASSERT_TRUE(s.rewriter->rewrite(above).ok());
        const auto raw = rawContents(*s.doc);
        below.insert(0, u32("Stale "));
        const RewriteReport r = s.rewriter->rewrite(below);
        EXPECT_EQ(r.error, ot::edit::RewriteError::Stale);
        EXPECT_EQ(rawContents(*s.doc), raw);  // документ не тронут
        EXPECT_EQ(below.utf8().substr(0, 6), "Stale ");
    }
}

TEST(rewriter, rotated_page) {
    Session s = open("latin.pdf");
    {
        ot::Journal::Operation op(*s.doc, "Rotate");
        ot::test::setRotate(*s.doc, 0, 90);
        op.invalidate(0);
        op.commit();
    }
    const ot::Bitmap shot = s.doc->renderPage(0, {kDpi, false});
    ParagraphEditor e = s.editor(0);
    e.insert(4, u32("rotated "));
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(diffOutside(shot, *s.doc, {r.dirty}), 0);
    EXPECT_EQ(ot::edit::blocksForPage(s.model()).front().text.substr(0, e.utf8().size()), e.utf8());
}

TEST(rewriter, retype_into_deleted_paragraph) {
    Session s = open("latin.pdf");
    ParagraphEditor e = s.editor(1);
    const float baseline = e.geometry().firstBaseline;
    e.erase({0, e.size()});
    ASSERT_TRUE(s.rewriter->rewrite(e).ok());
    e.insert(0, u32("A new paragraph typed after deleting the old one."));
    const RewriteReport r = s.rewriter->rewrite(e);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_TRUE(r.appended);
    const auto blocks = ot::edit::blocksForPage(s.model());
    const auto it = std::find_if(blocks.begin(), blocks.end(),
                                 [](const TextBlock& b) { return b.text.rfind("A new paragraph", 0) == 0; });
    ASSERT_NE(it, blocks.end());
    EXPECT_NEAR(it->lines.front().y, baseline, 0.01);
    // Ещё одна правка — уже на месте нового потока.
    e.insert(e.size(), u32(" Again."));
    const RewriteReport again = s.rewriter->rewrite(e);
    ASSERT_TRUE(again.ok()) << again.message;
    EXPECT_FALSE(again.appended);
    EXPECT_NE(squeeze(pageText(*s.doc)).find("old one. Again."), std::string::npos);
}

TEST(rewriter, layout_without_writing) {
    Session s = open("latin.pdf");
    const auto raw = rawContents(*s.doc);
    ParagraphEditor e = s.editor(0);
    e.insert(0, u32("Привет "));
    EXPECT_FALSE(e.layoutValid());
    const ot::edit::ReflowResult r = s.rewriter->layout(e);
    EXPECT_TRUE(e.layoutValid());
    EXPECT_EQ(r.layout.x.size(), static_cast<std::size_t>(e.size()));
    EXPECT_EQ(rawContents(*s.doc), raw);  // ничего не записано
    EXPECT_FALSE(s.doc->undoStack().canUndo());
}

// Правка через DocumentSession: шаг «Edit text», страница в pageChanged, отказ по устаревшему абзацу — без
// шага; «Сохранить как» — полная запись с подмножеством встроенного редактором шрифта.
TEST(rewriter, session_edit_text_and_save) {
    OT_FEATURE("edit-02");
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path path = dir.path() / L"абзац.pdf";
    fs::copy_file(editFixture("latin.pdf"), path);
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(path.wstring())));
    auto build = [&](std::size_t block) {
        return session.postSync([block](ot::TaskContext& tc) {
            const ContentModel m = ContentModel::build(tc.document(), 0);
            const auto b = ot::edit::blocksForPage(m);
            return ParagraphEditor::fromBlock(m, b.at(block), b);
        });
    };
    ParagraphEditor below = build(2);
    ParagraphEditor e = build(0);
    e.insert(0, u32("Привет, "));
    const quint64 revision = session.revision();
    ot::EditTextResult r = session.editText(e).result();
    ASSERT_TRUE(r.report.ok()) << r.report.message;
    EXPECT_GT(session.revision(), revision);
    EXPECT_TRUE(session.undoState().canUndo);
    EXPECT_EQ(session.undoState().undoName, QStringLiteral("Edit text"));
    EXPECT_FALSE(r.editor.modified());
    EXPECT_EQ(r.editor.utf8().rfind("Привет, The", 0), 0u);
    // Тот же редактор дальше: правка на месте записанного.
    r.editor.insert(r.editor.size(), u32(" Ещё."));
    const ot::EditTextResult again = session.editText(r.editor).result();
    ASSERT_TRUE(again.report.ok()) << again.report.message;
    // Абзац ниже построен до правок — устарел: отказ, шага нет.
    below.insert(0, u32("x"));
    const int steps = session.postSync([](ot::TaskContext& tc) { return tc.document().undoStack().size(); });
    const ot::EditTextResult stale = session.editText(below).result();
    EXPECT_EQ(stale.report.error, ot::edit::RewriteError::Stale);
    EXPECT_EQ(session.postSync([](ot::TaskContext& tc) { return tc.document().undoStack().size(); }), steps);
    // Полная запись: шрифт урезан (файл маленький), текст на месте.
    const fs::path out = dir.path() / L"сохранён.pdf";
    ot::SaveOptions options;
    const ot::SaveReport saved = session.saveAs(QString::fromStdWString(out.wstring()), options).result();
    ASSERT_TRUE(saved.ok()) << saved.message;
    EXPECT_NE(saved.mode, ot::SaveMode::Incremental);  // правка редактора — полная запись
    EXPECT_LT(fs::file_size(out), 120u * 1024u);
    auto reopened = Document::openOrThrow(out);
    const std::string text = squeeze(pageText(*reopened));
    EXPECT_NE(text.find("Привет, The editor"), std::string::npos);
    EXPECT_NE(text.find("paragraphs. Ещё."), std::string::npos);
    session.close();
}

// compat.edit: абзацы фикстур с правкой (латиница и кириллица — новые шрифты), стёртый и заново набранный
// абзац; полная запись с подмножествами шрифтов редактора в каталог edit под OTTISK_COMPAT_OUT и ожидания
// текста expected-text.json для tools/compat_check.py (qpdf --check, pdfcpu strict, mutool clean, pypdfium2
// рендер и текст).
TEST(rewriter_compat, DISABLED_write) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path dir = ot::paths::fromUtf8(outEnv) / L"edit";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    std::string expected = "{\n";
    bool firstEntry = true;
    auto write = [&](Session& s, const std::string& name, const std::vector<std::string>& texts) {
        ot::SaveOptions options;
        options.mode = ot::SaveMode::Full;
        options.prepare = [&](Document&) { (void)s.rewriter->subsetFonts(); };
        const ot::SaveReport saved = ot::Saver::save(*s.doc, dir / ot::paths::fromUtf8(name), options);
        ASSERT_TRUE(saved.ok()) << name << ": " << saved.message;
        expected += std::string(firstEntry ? "" : ",\n") + "  \"" + name + "\": [";
        for (std::size_t i = 0; i < texts.size(); ++i) {
            expected += std::string(i > 0 ? ", " : "") + "\"" + texts[i] + "\"";
        }
        expected += "]";
        firstEntry = false;
    };
    for (const char* fixture :
         {"latin.pdf", "cyr-cid.pdf", "align-justify.pdf", "tz-tc-tw.pdf", "two-streams.pdf", "base14.pdf",
          "align-center.pdf", "tj-kerning.pdf", "word-paragraphs.pdf"}) {
        // type1-diff.pdf не входит: base14 с /Differences без /FontDescriptor не проходит pdfcpu strict и до
        // правки (свойство фикстуры; правку того же файла проверяет unit.rewriter).
        SCOPED_TRACE(fixture);
        Session s = open(fixture);
        const std::size_t block = std::string(fixture) == "word-paragraphs.pdf" ? 1 : 0;
        ParagraphEditor e = s.editor(block);
        const auto& first = e.layout().lines.front();
        const int at = e.wordAt(first.begin + (first.end - first.begin) / 2).begin;
        e.insert(at, u32("Привет, world! "));
        const RewriteReport r = s.rewriter->rewrite(e);
        ASSERT_TRUE(r.ok()) << r.message;
        std::string name(fixture);
        name.insert(name.size() - 4, "-edited");
        write(s, name, {"Привет,world!"});
    }
    {
        Session s = open("latin.pdf");
        ParagraphEditor e = s.editor(1);
        e.erase({0, e.size()});
        ASSERT_TRUE(s.rewriter->rewrite(e).ok());
        e.insert(0, u32("Абзац набран заново — конец."));
        ASSERT_TRUE(s.rewriter->rewrite(e).ok());
        write(s, "latin-retyped.pdf", {"Untouchedoperators", "Абзацнабранзаново—конец."});
    }
    expected += "\n}\n";
    ot::paths::writeFile(dir / L"expected-text.json", expected);
}
