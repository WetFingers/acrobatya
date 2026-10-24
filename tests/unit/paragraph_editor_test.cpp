// unit.paragraph_editor.* — модель правки абзаца (план 3.3, п. 1; эталон 3.9 «Модель»; edit-01): редактор из
// блока фикстуры даёт тот же текст, что TextBlock::text, стили пробегов из спанов (шрифт, кегль, цвет с
// исходными операторами, Tc, Tz, Ts), геометрию набора (левый край, отступ первой строки, ширина, интервал) и
// раскладку по позициям глифов; insert/erase/replace/applyStyle меняют строку и пробеги, курсор ↔ координаты
// страницы (и на странице с /Rotate 90), слово, выделение по строкам, «только чтение» не правится.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/paragraph_editor.h"
#include "core/edit/text_blocks.h"
#include "core/mu/call.h"
#include "core/util/features.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::edit::Align;
using ot::edit::blocksForPage;
using ot::edit::CharStyle;
using ot::edit::ContentModel;
using ot::edit::ParagraphEditor;
using ot::edit::StyleChange;
using ot::edit::TextBlock;
using ot::edit::TextRange;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

struct Page {
    std::unique_ptr<Document> doc;
    ContentModel model;
    std::vector<TextBlock> blocks;
};

Page load(const std::string& name, int page = 0) {
    Page p;
    p.doc = Document::openOrThrow(editFixture(name));
    p.model = ContentModel::build(*p.doc, page);
    p.blocks = blocksForPage(p.model);
    return p;
}

ParagraphEditor editorOf(const Page& p, std::size_t block) {
    return ParagraphEditor::fromBlock(p.model, p.blocks.at(block), p.blocks);
}

std::u32string u32(const char* s) { return ot::utf8::decode(s); }

int find(const ParagraphEditor& e, const char* needle) {
    const std::u32string text = e.text();
    const std::size_t at = text.find(u32(needle));
    return at == std::u32string::npos ? -1 : static_cast<int>(at);
}

// Глифы блока по порядку чтения (первые символы глифов).
std::vector<const ot::edit::Glyph*> glyphsOf(const Page& p, const TextBlock& b) {
    std::vector<const ot::edit::Glyph*> out;
    for (int si : b.spans) {
        const auto& s = p.model.spans()[static_cast<std::size_t>(si)];
        for (int g = s.firstGlyph; g < s.firstGlyph + s.glyphCount; ++g) {
            out.push_back(&p.model.glyphs()[static_cast<std::size_t>(g)]);
        }
    }
    return out;
}

}  // namespace

TEST(paragraph_editor, text_matches_blocks) {
    OT_FEATURE("edit-01");
    for (const char* name : {"latin.pdf", "cyr-cid.pdf", "type1-diff.pdf", "base14.pdf", "tj-kerning.pdf",
                             "tz-tc-tw.pdf", "two-streams.pdf", "columns-2.pdf", "columns-3.pdf",
                             "align-left.pdf", "align-right.pdf", "align-center.pdf", "align-justify.pdf",
                             "inline-image.pdf", "word-paragraphs.pdf", "word-lists.pdf", "word-table.pdf"}) {
        SCOPED_TRACE(name);
        const Page p = load(name);
        int editable = 0;
        for (std::size_t i = 0; i < p.blocks.size(); ++i) {
            const TextBlock& b = p.blocks[i];
            if (!b.editable()) {
                EXPECT_THROW((void)editorOf(p, i), std::invalid_argument);
                continue;
            }
            ++editable;
            SCOPED_TRACE(b.text);
            const ParagraphEditor e = editorOf(p, i);
            EXPECT_EQ(e.utf8(), b.text);
            EXPECT_FALSE(e.modified());
            EXPECT_TRUE(e.layoutValid());
            ASSERT_EQ(e.lineCount(), static_cast<int>(b.lines.size()));
            for (std::size_t l = 0; l < b.lines.size(); ++l) {
                EXPECT_NEAR(e.layout().lines[l].baseline, b.lines[l].y, 0.01) << l;
                EXPECT_NEAR(e.layout().lines[l].x0, b.lines[l].x0, 0.01) << l;
            }
            EXPECT_EQ(e.layout().lines.front().begin, 0);
            EXPECT_EQ(e.layout().lines.back().end, e.size());
            // Источник — операторы показа блока по порядку потока, их текст — из модели.
            EXPECT_FALSE(e.source().ops.empty());
            std::string text;
            for (std::size_t k = 0; k < e.source().ops.size(); ++k) {
                if (k > 0) {
                    EXPECT_LT(e.source().ops[k - 1], e.source().ops[k]);
                }
                text +=
                    p.model.spanText(p.model.operations()[static_cast<std::size_t>(e.source().ops[k])].span);
            }
            EXPECT_EQ(e.source().text, text);
            EXPECT_EQ(e.alignment(), b.align);
        }
        EXPECT_GT(editable, 0);
    }
}

TEST(paragraph_editor, styles_from_spans) {
    {
        const Page p = load("latin.pdf");
        const ParagraphEditor e = editorOf(p, 0);
        ASSERT_EQ(e.runs().size(), 1u);
        const CharStyle& s = e.styleAt(0);
        EXPECT_EQ(s.fontName, "ArialMT");
        EXPECT_FALSE(s.fontResource.empty());
        EXPECT_NEAR(s.size, 11, 1e-4);
        EXPECT_EQ(s.color, 0xFF000000u);
        EXPECT_FALSE(s.bold);
        EXPECT_FALSE(s.italic);
        EXPECT_NEAR(s.hscale, 1, 1e-6);
        EXPECT_NEAR(s.charSpacing, 0, 1e-6);
        // Символы из файла — с кодами и ширинами глифов.
        for (const auto& c : e.chars()) {
            if (c.ch != U' ' || !c.code.empty()) {
                EXPECT_FALSE(c.code.empty());
                EXPECT_GT(c.w0, 0);
            }
        }
    }
    {
        const Page p = load("tz-tc-tw.pdf");
        const ParagraphEditor tc = editorOf(p, 0);
        EXPECT_NEAR(tc.styleAt(0).charSpacing, 1.5, 1e-4);
        const ParagraphEditor tz = editorOf(p, 2);
        EXPECT_NEAR(tz.styleAt(0).hscale, 0.75, 1e-4);
        // «E = mc²»: индекс поднят Ts 5 при кегле 8, нижний — Ts −3.
        const ParagraphEditor idx = editorOf(p, 3);
        EXPECT_EQ(idx.utf8(), "E = mc2 and H2O in one line");
        const int sup = find(idx, "2 and");
        ASSERT_GE(sup, 0);
        EXPECT_NEAR(idx.styleAt(sup).rise, 5, 1e-4);
        EXPECT_NEAR(idx.styleAt(sup).size, 8, 1e-4);
        const int sub = find(idx, "2O");
        EXPECT_NEAR(idx.styleAt(sub).rise, -3, 1e-4);
        EXPECT_EQ(idx.runs().size(), 5u);
    }
    {
        // Цвет — с исходными операторами: «0 0 0.6 rg» до BT.
        const Page p = load("two-streams.pdf");
        const ParagraphEditor e = editorOf(p, 0);
        EXPECT_EQ(e.styleAt(0).color, 0xFF000099u);
        EXPECT_EQ(e.styleAt(0).colorOps, "0 0 0.6 rg");
        EXPECT_EQ(e.source().ops.size(), 3u);  // три оператора в двух потоках
    }
}

TEST(paragraph_editor, geometry) {
    {
        const Page p = load("cyr-cid.pdf");
        // Первый абзац — с отступом первой строки 24 pt.
        const ParagraphEditor e = editorOf(p, 0);
        EXPECT_NEAR(e.geometry().left, 72, 0.01);
        EXPECT_NEAR(e.geometry().firstIndent, 24, 0.01);
        EXPECT_NEAR(e.geometry().firstBaseline, p.blocks[0].lines[0].y,
                    1e-4);  // раскладка: y пользователя, вниз
        EXPECT_NEAR(e.caret(0).bottom.y - e.layout().lines[0].descent, 100,
                    0.01);  // страница: базовая линия 100
        EXPECT_NEAR(e.geometry().fontSize, 12, 1e-4);
        EXPECT_NEAR(e.geometry().leading, 15, 0.01);
        EXPECT_NEAR(e.lineSpacing(), 15.0 / 12.0, 1e-3);
        const TextBlock& b = p.blocks[0];
        float right = 0;
        for (const auto& l : b.lines) {
            right = std::max(right, l.x1);
        }
        EXPECT_NEAR(e.geometry().left + e.geometry().width, right, 0.01);
    }
    {
        // Абзац из одной строки: справа на её высоте никого — набор до поля, зеркального левому (595 − 72).
        const Page p = load("base14.pdf");
        const ParagraphEditor one = editorOf(p, 1);
        EXPECT_EQ(one.lineCount(), 1);
        EXPECT_NEAR(one.lineSpacing(), ParagraphEditor::kDefaultLineSpacing, 1e-6);
        EXPECT_NEAR(one.geometry().left, 72, 0.01);
        EXPECT_NEAR(one.geometry().left + one.geometry().width, p.model.pageBounds().x1 - 72, 0.5);
    }
    {
        // Ячейка таблицы Word: набор — до соседней ячейки справа (её левый край минус полкегля).
        const Page p = load("word-table.pdf");
        std::size_t cell = p.blocks.size();
        for (std::size_t i = 0; i < p.blocks.size(); ++i) {
            if (p.blocks[i].text == "Город") {
                cell = i;
            }
        }
        ASSERT_LT(cell, p.blocks.size());
        const ParagraphEditor one = editorOf(p, cell);
        const auto& line = p.blocks[cell].lines.front();
        float next = 1e9f;
        for (const auto& b : p.blocks) {
            for (const auto& l : b.lines) {
                if (std::fabs(l.y - line.y) < 2 && l.x0 >= line.x1) {
                    next = std::min(next, l.x0);
                }
            }
        }
        ASSERT_LT(next, 1e9f);
        EXPECT_NEAR(one.geometry().left + one.geometry().width, next - 0.5f * line.size, 0.01);
    }
    {
        // Одна строка без соседей: до поля страницы, зеркального левому (A4 595 × 842: 595 − 72).
        const Page p = load("rotated.pdf");
        const ParagraphEditor one = editorOf(p, 0);
        EXPECT_NEAR(one.geometry().left + one.geometry().width, p.model.pageBounds().x1 - 72, 0.5);
    }
}

TEST(paragraph_editor, insert_erase_replace) {
    const Page p = load("latin.pdf");
    ParagraphEditor e = editorOf(p, 0);
    const std::string before = e.utf8();
    const int at = find(e, "model");
    ASSERT_GT(at, 0);
    EXPECT_EQ(e.insert(at, u32("big ")), at + 4);
    EXPECT_TRUE(e.modified());
    EXPECT_FALSE(e.layoutValid());
    EXPECT_EQ(e.utf8().substr(0, static_cast<std::size_t>(at) + 9),
              before.substr(0, static_cast<std::size_t>(at)) + "big model");
    EXPECT_EQ(e.runs().size(), 1u);  // стиль соседнего символа
    for (int i = at; i < at + 4; ++i) {
        EXPECT_TRUE(e.chars()[static_cast<std::size_t>(i)].code.empty());  // набранные — без кодов
    }
    EXPECT_EQ(e.erase({at, at + 4}), at);
    EXPECT_EQ(e.utf8(), before);
    // Табуляция — пробел, перевод строки — «\n».
    e.insert(0, u32("A\tB\r\nC\r"));
    EXPECT_EQ(ot::utf8::encode(e.text({0, 7})), "A B\nC\n" + before.substr(0, 1));
    e.erase({0, 6});
    EXPECT_EQ(e.utf8(), before);
    // Замена берёт стиль первого заменяемого символа.
    e.applyStyle({0, 3}, StyleChange{.size = 20.0f});
    const int pos = e.replace({0, 3}, u32("A"));
    EXPECT_EQ(pos, 1);
    EXPECT_NEAR(e.styleAt(0).size, 20, 1e-6);
    EXPECT_NEAR(e.styleAt(1).size, 11, 1e-6);
    // Зажим за края: вставка в конец, стирание сверх длины.
    const int end = e.insert(10000, u32("!"));
    EXPECT_EQ(end, e.size());
    EXPECT_EQ(e.text().back(), U'!');
    const int erased = e.erase({e.size() - 1, e.size() + 50});
    EXPECT_EQ(erased, e.size());
    EXPECT_NE(e.text().back(), U'!');
}

TEST(paragraph_editor, apply_style_runs) {
    const Page p = load("latin.pdf");
    ParagraphEditor e = editorOf(p, 0);
    const int at = find(e, "model");
    // Жирный на слово — три пробега, у среднего bold.
    e.applyStyle({at, at + 5}, StyleChange{.bold = true});
    auto runs = e.runs();
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[1].begin, at);
    EXPECT_EQ(runs[1].end, at + 5);
    EXPECT_TRUE(e.styles()[static_cast<std::size_t>(runs[1].style)].bold);
    EXPECT_FALSE(e.styles()[static_cast<std::size_t>(runs[0].style)].bold);
    // Ввод сразу после слова — его стилем.
    EXPECT_TRUE(e.typingStyle(at + 5).bold);
    EXPECT_FALSE(e.typingStyle(at).bold);
    // Обратно — снова один пробег (стили одинаковые — один индекс).
    e.applyStyle({at, at + 5}, StyleChange{.bold = false});
    EXPECT_EQ(e.runs().size(), 1u);
    // Цвет, кегль, подчёркивание, семейство, межбуквенный, масштаб, подъём.
    StyleChange all;
    all.color = 0xFFFF0000u;
    all.size = 14.0f;
    all.underline = true;
    all.family = "Times New Roman";
    all.charSpacing = 0.5f;
    all.hscale = 0.8f;
    all.rise = 3.0f;
    e.applyStyle({0, 3}, all);
    const CharStyle& s = e.styleAt(0);
    EXPECT_EQ(s.color, 0xFFFF0000u);
    EXPECT_TRUE(s.colorOps.empty());  // новый цвет — rg, исходные операторы не годятся
    EXPECT_NEAR(s.size, 14, 1e-6);
    EXPECT_TRUE(s.underline);
    EXPECT_EQ(s.family, "Times New Roman");
    EXPECT_NEAR(s.charSpacing, 0.5, 1e-6);
    EXPECT_NEAR(s.hscale, 0.8, 1e-6);
    EXPECT_NEAR(s.rise, 3, 1e-6);
    EXPECT_EQ(e.runs().size(), 2u);
    e.setAlignment(Align::Justify);
    EXPECT_EQ(e.alignment(), Align::Justify);
    e.setLineSpacing(1.5f);
    EXPECT_NEAR(e.lineSpacing(), 1.5, 1e-6);
}

TEST(paragraph_editor, caret_and_positions) {
    const Page p = load("latin.pdf");
    const TextBlock& b = p.blocks[0];
    const ParagraphEditor e = editorOf(p, 0);
    const auto glyphs = glyphsOf(p, b);
    // Курсор перед каждым символом из файла — у начала его глифа; точка у левой половины глифа — его позиция.
    int pos = 0;
    std::size_t g = 0;
    for (; pos < e.size() && g < glyphs.size(); ++pos) {
        if (e.chars()[static_cast<std::size_t>(pos)].code.empty()) {
            continue;  // пробел между строками
        }
        const auto& gl = *glyphs[g++];
        const ot::edit::Caret c = e.caret(pos);
        EXPECT_NEAR(c.top.x, gl.origin.x, 0.01) << pos;
        EXPECT_LT(c.top.y, gl.origin.y);
        EXPECT_GT(c.bottom.y, gl.origin.y - 0.01f);
        const float w = gl.quad.ur.x - gl.quad.ul.x;
        const ot::PointF inside{gl.origin.x + w * 0.25f, gl.origin.y - 3};
        EXPECT_EQ(e.positionAt(inside), pos) << pos;
        const ot::PointF right{gl.origin.x + w * 0.75f, gl.origin.y - 3};
        EXPECT_EQ(e.positionAt(right), pos + 1) << pos;
    }
    EXPECT_EQ(g, glyphs.size());
    // Правее конца первой строки — конец её текста (до пробела переноса), ниже последней — конец абзаца.
    const auto& l0 = e.layout().lines[0];
    const ot::PointF farRight{l0.x1 + 50, b.lines[0].baseline - 2};
    EXPECT_EQ(e.positionAt(farRight), l0.end - 1);
    EXPECT_EQ(e.positionAt({b.bbox.x1 + 10, b.bbox.y1 + 40}), e.size());
    EXPECT_EQ(e.positionAt({b.bbox.x0 - 30, b.bbox.y0 - 40}), 0);
    // Строки курсора: вниз под тем же x, вверх обратно.
    EXPECT_EQ(e.lineOf(0), 0);
    EXPECT_EQ(e.lineOf(e.size()), e.lineCount() - 1);
    const int down = e.moveLine(5, 1);
    EXPECT_EQ(e.lineOf(down), 1);
    EXPECT_NEAR(e.caret(down).x, e.caret(5).x, 6.0);
    EXPECT_LE(std::abs(e.moveLine(down, -1) - 5), 1);
    EXPECT_EQ(e.moveLine(5, -1), 0);
    EXPECT_EQ(e.moveLine(down, 1), e.size());
    // Слово и выделение по двум строкам.
    const int at = find(e, "model");
    EXPECT_EQ(e.wordAt(at + 2), (TextRange{at, at + 5}));
    EXPECT_EQ(e.wordAt(at + 5).size(), 5);  // сразу за словом — оно же
    const auto quads = e.selectionQuads({at, e.layout().lines[1].begin + 3});
    ASSERT_EQ(quads.size(), 2u);
    EXPECT_NEAR(quads[0].ul.x, e.caret(at).top.x, 0.01);
    EXPECT_LT(quads[0].ul.y, quads[1].ul.y);
}

TEST(paragraph_editor, caret_on_rotated_page) {
    auto doc = Document::openOrThrow(editFixture("latin.pdf"));
    {
        ot::Journal::Operation op(*doc, "Rotate");
        fz_context* ctx = doc->context().get();
        pdf_document* pdf = doc->pdf();
        ot::mu::call(ctx,
                     [&] { pdf_dict_put_int(ctx, pdf_lookup_page_obj(ctx, pdf, 0), PDF_NAME(Rotate), 90); });
        op.invalidate(0);
        op.commit();
    }
    const ContentModel model = ContentModel::build(*doc, 0);
    const auto blocks = blocksForPage(model);
    const ParagraphEditor e = ParagraphEditor::fromBlock(model, blocks.at(0), blocks);
    EXPECT_EQ(e.lineCount(), 2);
    // Страница повёрнута: строка идёт сверху вниз — курсор горизонтальный, глифы по y.
    const auto& b = blocks[0];
    const auto& span = model.spans()[static_cast<std::size_t>(b.spans[0])];
    const auto& gl = model.glyphs()[static_cast<std::size_t>(span.firstGlyph + 3)];
    const ot::edit::Caret c = e.caret(3);
    EXPECT_NEAR(c.top.y, gl.origin.y, 0.01);
    EXPECT_NEAR(c.bottom.y, gl.origin.y, 0.01);
    EXPECT_GT(std::fabs(c.top.x - c.bottom.x), 5.0f);
    const float h = gl.quad.lr.y - gl.quad.ll.y;
    EXPECT_EQ(e.positionAt({(gl.quad.ul.x + gl.quad.ll.x) / 2, gl.origin.y + h * 0.25f}), 3);
}
