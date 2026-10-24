// unit.edit_format.* — форматирование пробегов (план 3.3, п. 4; эталон 3.9, панель FORMAT TEXT; edit-03):
// жирный на слово — три пробега, у среднего Tf нового шрифта Arial Bold (грань семейства), у крайних —
// исходный; курсив без грани — синтетический наклон; семейство из панели — свой шрифт; кегль, цвет (rg),
// межбуквенный (Tc), масштаб (Tz), верхний и нижний индекс (Ts и меньший кегль), межстрочный интервал, четыре
// выравнивания; подчёркивание — «re f» после ET в пометке /OttU, рисуется под словом, повторная правка не
// множит пометки, а редактор, построенный заново, видит подчёркнутые символы; рендер вне абзаца — pixels ==
// 0.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_resolver.h"
#include "core/edit/paragraph_editor.h"
#include "core/edit/rewriter.h"
#include "core/edit/system_fonts.h"
#include "core/edit/text_blocks.h"
#include "core/render/image_compare.h"
#include "core/util/features.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::edit::Align;
using ot::edit::ContentModel;
using ot::edit::FakeSystemFonts;
using ot::edit::FontLibrary;
using ot::edit::ParagraphEditor;
using ot::edit::Rewriter;
using ot::edit::RewriteReport;
using ot::edit::StyleChange;
using ot::edit::TextBlock;
using ot::edit::TextRange;

namespace {

constexpr float kDpi = 96;

struct Session {
    std::unique_ptr<Document> doc;
    FakeSystemFonts system;
    std::unique_ptr<FontLibrary> lib;
    std::unique_ptr<Rewriter> rewriter;

    explicit Session(const std::string& fixture)
        : doc(Document::openOrThrow(ot::test::sourceDir() / "tests" / "fixtures" / "edit" / fixture)) {
        lib = std::make_unique<FontLibrary>(doc->context().get());
        rewriter = std::make_unique<Rewriter>(*doc, *lib, system, ot::edit::FallbackFonts{});
    }
    ContentModel model() const { return ContentModel::build(*doc, 0); }
    ParagraphEditor editor(std::size_t block = 0) const {
        const ContentModel m = model();
        const auto b = ot::edit::blocksForPage(m);
        return ParagraphEditor::fromBlock(m, b.at(block), b);
    }
    // Записать и вернуть отчёт; рендер вне абзаца — тот же.
    RewriteReport write(ParagraphEditor& e) {
        const ot::Bitmap before = doc->renderPage(0, {kDpi, false});
        const RewriteReport r = rewriter->rewrite(e);
        EXPECT_TRUE(r.ok()) << r.message;
        const ot::Bitmap after = doc->renderPage(0, {kDpi, false});
        EXPECT_EQ(ot::diffPixelsOutside(before, after, {ot::pixelRect(r.dirty, kDpi, 2)}), 0);
        return r;
    }
};

int find(const ParagraphEditor& e, const char* word) {
    const std::size_t at = e.text().find(ot::utf8::decode(word));
    return at == std::u32string::npos ? -1 : static_cast<int>(at);
}

TextRange wordRange(const ParagraphEditor& e, const char* word) {
    const int at = find(e, word);
    return {at, at + static_cast<int>(ot::utf8::decode(word).size())};
}

// Спаны абзаца (первый блок модели) с текстом.
const ot::edit::TextSpan* spanWith(const ContentModel& m, const std::string& text) {
    for (std::size_t i = 0; i < m.spans().size(); ++i) {
        if (m.spanText(static_cast<int>(i)).find(text) != std::string::npos) {
            return &m.spans()[i];
        }
    }
    return nullptr;
}

// Число пометок /OttU на странице.
int underlineMarks(const ContentModel& m) {
    int n = 0;
    for (const auto& op : m.operations()) {
        if (op.kind == ot::edit::OpKind::BeginMarked && !op.operands.empty() &&
            op.operands.front().kind == ot::edit::Operand::Kind::Name && op.operands.front().text == "OttU") {
            ++n;
        }
    }
    return n;
}

}  // namespace

TEST(edit_format, bold_word_three_runs) {
    OT_FEATURE("edit-03");
    Session s("latin.pdf");
    if (!s.system.face("Arial", 700, false)) {
        GTEST_SKIP() << "нет arialbd.ttf";
    }
    ParagraphEditor e = s.editor();
    const std::string original = e.styleAt(0).fontResource;
    e.applyStyle(wordRange(e, "model"), StyleChange{.bold = true});
    s.write(e);
    const ContentModel m = s.model();
    const auto blocks = ot::edit::blocksForPage(m);
    const TextBlock& b = blocks.front();
    ASSERT_EQ(b.runs.size(), 3u);
    EXPECT_EQ(b.runs[1].text, "model");
    const auto& bold = m.fonts()[static_cast<std::size_t>(b.runs[1].font)];
    EXPECT_EQ(bold.name, "Arial-BoldMT");
    EXPECT_NE(bold.resource, original);
    EXPECT_EQ(m.fonts()[static_cast<std::size_t>(b.runs[0].font)].resource, original);
    EXPECT_EQ(m.fonts()[static_cast<std::size_t>(b.runs[2].font)].resource, original);
    for (const auto& run : b.runs) {
        EXPECT_NEAR(run.size, 11, 1e-4);  // Tf — тот же кегль
    }
    // Редактор заново: у слова — жирный.
    const ParagraphEditor again = ParagraphEditor::fromBlock(m, b, blocks);
    EXPECT_TRUE(again.styleAt(find(again, "model")).bold);
    EXPECT_FALSE(again.styleAt(0).bold);
}

TEST(edit_format, italic_synthetic_and_family) {
    Session s("latin.pdf");
    ParagraphEditor e = s.editor();
    // Семейство из панели — свой шрифт; курсив у Segoe UI Symbol… — проверяем грань Times New Roman Italic.
    e.applyStyle(wordRange(e, "model"),
                 StyleChange{.family = std::string("Times New Roman"), .italic = true});
    // Курсив без грани — наклон: у семейства без курсива (Symbol) — синтетика.
    e.applyStyle(wordRange(e, "editor"),
                 StyleChange{.family = std::string("Segoe UI Symbol"), .italic = true});
    const RewriteReport r = s.write(e);
    const ContentModel m = s.model();
    const auto* times = spanWith(m, "model");
    ASSERT_NE(times, nullptr);
    EXPECT_EQ(m.fonts()[static_cast<std::size_t>(times->font)].name, "TimesNewRomanPS-ItalicMT");
    const auto* symbol = spanWith(m, "editor");
    ASSERT_NE(symbol, nullptr);
    EXPECT_NEAR(symbol->tm.c, 0.2f, 1e-4);  // наклон [1 0 0.2 1] в Tm
    const bool synthesized = std::any_of(r.notices.begin(), r.notices.end(), [](const auto& n) {
        return n.kind == ot::edit::FontNoticeKind::Synthesized;
    });
    EXPECT_TRUE(synthesized);
}

TEST(edit_format, size_color_spacing_scale) {
    Session s("latin.pdf");
    ParagraphEditor e = s.editor();
    StyleChange c;
    c.size = 16.0f;
    c.color = 0xFF1060C0u;
    c.charSpacing = 1.5f;
    c.hscale = 0.8f;
    e.applyStyle(wordRange(e, "model"), c);
    s.write(e);
    const ContentModel m = s.model();
    const auto* span = spanWith(m, "model");
    ASSERT_NE(span, nullptr);
    EXPECT_NEAR(span->fontSize, 16 * std::sqrt(0.8f), 0.01);  // кегль на странице с Tz
    EXPECT_NEAR(span->size, 16, 1e-4);
    EXPECT_EQ(span->fill, 0xFF1060C0u);
    EXPECT_NEAR(span->scale, 0.8, 1e-4);
    EXPECT_NEAR(span->charSpace * span->scale, 1.5, 1e-3);  // Tc · Tz = 1,5 pt после символа
    // Соседние пробеги — прежние.
    const auto* before = spanWith(m, "The editor");
    ASSERT_NE(before, nullptr);
    EXPECT_EQ(before->fill, 0xFF000000u);
    EXPECT_NEAR(before->size, 11, 1e-4);
    EXPECT_NEAR(before->scale, 1, 1e-6);
}

TEST(edit_format, superscript_and_subscript) {
    Session s("latin.pdf");
    ParagraphEditor e = s.editor();
    const ot::edit::CharStyle base = e.styleAt(0);
    e.insert(find(e, " of every"), ot::utf8::decode("2"));
    e.insert(find(e, "every") + 1, ot::utf8::decode("i"));
    const int sup = find(e, "2 of");
    e.applyStyle({sup, sup + 1}, ot::edit::scriptChange(e.styleAt(sup), ot::edit::Script::Super));
    const int sub = find(e, "eivery") + 1;
    e.applyStyle({sub, sub + 1}, ot::edit::scriptChange(e.styleAt(sub), ot::edit::Script::Sub));
    EXPECT_EQ(ot::edit::scriptOf(e.styleAt(sup)), ot::edit::Script::Super);
    EXPECT_EQ(ot::edit::scriptOf(e.styleAt(sub)), ot::edit::Script::Sub);
    EXPECT_EQ(ot::edit::scriptOf(base), ot::edit::Script::Normal);
    s.write(e);
    const ContentModel m = s.model();
    float supRise = 0;
    float subRise = 0;
    for (std::size_t i = 0; i < m.spans().size(); ++i) {
        const auto& span = m.spans()[i];
        const std::string text = m.spanText(static_cast<int>(i));
        if (text == "2") {
            supRise = span.rise;
            EXPECT_LT(span.size, base.size);
        } else if (text == "i") {
            subRise = span.rise;
        }
    }
    EXPECT_GT(supRise, 0.3f * base.size * 0.99f);
    EXPECT_LT(subRise, 0);
    // Обратно — обычный: кегль и базовая линия прежние.
    const ot::edit::StyleChange back = ot::edit::scriptChange(e.styleAt(sup), ot::edit::Script::Normal);
    EXPECT_NEAR(*back.size, base.size, 1e-3);
    EXPECT_NEAR(*back.rise, 0, 1e-6);
}

TEST(edit_format, line_spacing_and_alignment) {
    for (const Align align : {Align::Left, Align::Center, Align::Right, Align::Justify}) {
        SCOPED_TRACE(ot::edit::alignCode(align));
        // Последний абзац страницы: вырастая вниз, он не встаёт вплотную к соседу (иначе блоки склеятся).
        Session s("latin.pdf");
        ParagraphEditor e = s.editor(2);
        e.insert(e.size(),
                 ot::utf8::decode(" More words make a third line for the alignment test here, and a "
                                  "fourth one follows when the paragraph keeps growing downwards."));
        e.setAlignment(align);
        e.setLineSpacing(1.6f);
        s.write(e);
        const auto& lines = e.layout().lines;
        ASSERT_GE(lines.size(), 3u);
        const float left = e.geometry().left;
        const float right = left + e.geometry().width;
        for (std::size_t i = 1; i < lines.size(); ++i) {
            EXPECT_NEAR(lines[i].baseline - lines[i - 1].baseline, 1.6f * 11, 1e-3);
        }
        // Строки новой модели — там, где раскладка (выравнивание в Tm каждой строки).
        const ContentModel m = s.model();
        const auto& first = m.spans()[static_cast<std::size_t>(
            m.operations()[static_cast<std::size_t>(e.source().ops.front())].span)];
        EXPECT_NEAR(first.origin.x - (e.caret(0).bottom.x), 0, 0.05);
        for (std::size_t i = 0; i + 1 < lines.size(); ++i) {
            switch (align) {
                case Align::Left:
                    EXPECT_NEAR(lines[i].x0, left, 0.01);
                    break;
                case Align::Right:
                    EXPECT_NEAR(lines[i].x1, right, 0.01);
                    break;
                case Align::Center:
                    EXPECT_NEAR((lines[i].x0 + lines[i].x1) / 2, (left + right) / 2, 0.01);
                    break;
                case Align::Justify:
                    EXPECT_NEAR(lines[i].x0, left, 0.01);
                    EXPECT_NEAR(lines[i].x1, right, 0.01);
                    break;
            }
        }
        // Блок после записи — с тем же выравниванием.
        EXPECT_EQ(ot::edit::blocksForPage(m).back().align, align);
    }
}

TEST(edit_format, underline_marked_and_reedited) {
    Session s("latin.pdf");
    ParagraphEditor e = s.editor();
    const TextRange word = wordRange(e, "model");
    e.applyStyle(word, StyleChange{.underline = true});
    const ot::Bitmap plain = s.doc->renderPage(0, {kDpi, false});
    s.write(e);
    ContentModel m = s.model();
    EXPECT_EQ(underlineMarks(m), 1);
    // Под словом — тёмная полоса: у нижнего края слова рендер темнее, чем был.
    const ot::Bitmap lined = s.doc->renderPage(0, {kDpi, false});
    const ot::edit::Caret a = e.caret(find(e, "model"));
    const ot::edit::Caret b = e.caret(find(e, "model") + 5);
    const ot::IRect band =
        ot::pixelRect({a.bottom.x + 1, a.bottom.y - 3.5f, b.bottom.x - 1, a.bottom.y}, kDpi);
    long long darker = 0;
    for (int y = band.y0; y < band.y1; ++y) {
        for (int x = band.x0; x < band.x1; ++x) {
            const std::size_t k = static_cast<std::size_t>(x) * 3;
            darker += lined.row(y)[k] + 40 < plain.row(y)[k] ? 1 : 0;
        }
    }
    EXPECT_GT(darker, (band.x1 - band.x0) / 2);
    // Редактор заново видит подчёркнутые символы; повторная запись не множит пометки.
    ParagraphEditor again = s.editor();
    const int at = find(again, "model");
    for (int i = at; i < at + 5; ++i) {
        EXPECT_TRUE(again.styleAt(i).underline) << i;
    }
    EXPECT_FALSE(again.styleAt(0).underline);
    again.insert(0, ot::utf8::decode("New "));
    s.write(again);
    m = s.model();
    EXPECT_EQ(underlineMarks(m), 1);
    // Подчёркивание снято — пометок нет.
    ParagraphEditor third = s.editor();
    third.applyStyle({0, third.size()}, StyleChange{.underline = false});
    s.write(third);
    EXPECT_EQ(underlineMarks(s.model()), 0);
}
