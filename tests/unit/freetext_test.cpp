// unit.freetext.* — текстовое поле и выноска (план 5.2, п. 1; эталон 3.4 «Ядро»: /DA для FreeText с
// кириллицей через встроенный TrueType; comment-04, comment-05, comment-09): перенос строк по ширине, имена
// шрифтов
// (/DA Helv → Arial), кириллица — шрифт /DA лежит в /AcroForm /DR и в ресурсах вида: Type0 Identity-H с
// /FontFile2 и /ToUnicode, урезан до латиницы, кириллицы и знаков (кегль — по /DA), рисуется и извлекается;
// выноска сохраняет /IT /FreeTextCallout, /CL (3 точки), /LE (имя), /Q и /RD после инкрементальной записи и
// переоткрытия; правка свойств, рамки и флагов оставляет наш шрифт (MuPDF не пересинтезирует вид с Helv);
// поля одного семейства делят один шрифт, знак вне подмножества — новый шрифт; /Rotate страницы — /Rotate
// поля; «без рамки и заливки» — вид без re f / re S.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/annot/freetext.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/bitmap.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::PointF;
using ot::RectF;
using ot::annot::AnnotColor;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;

namespace {

AnnotProps textBox(const char* text, RectF rect, const char* font = "Arial", float size = 14) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::FreeText);
    p.rect = rect;
    p.contents = text;
    p.fontName = font;
    p.fontSize = size;
    p.author = "Tester";
    return p;
}

// Сведения о словаре аннотации и её виде — строки и номера (снаружи mu::call в тестах).
struct Raw {
    std::string da;
    std::string le;            // /LE — имя (выноска)
    std::string it;            // /IT
    std::vector<float> cl;     // /CL
    int q = -1;                // /Q
    int rotate = 0;            // /Rotate
    bool hasRd = false;        // /RD
    fz_rect rect{};            // /Rect (пункты PDF)
    std::string apFont;        // имя первого шрифта в /AP /N /Resources /Font
    int apFontNum = 0;         // его объект
    std::string apContents;    // поток /AP /N
    int drFontNum = 0;         // /AcroForm /DR /Font /<имя шрифта /DA>
    std::string drSubtype;     // /Subtype шрифта /DR
    std::string drEncoding;    // /Encoding
    std::string drBaseFont;    // /BaseFont
    bool drFontFile2 = false;  // /DescendantFonts[0] /FontDescriptor /FontFile2 — поток
    std::string toUnicode;     // поток /ToUnicode
};

std::string nameOf(fz_context* ctx, pdf_obj* o) {
    const char* s = pdf_to_name(ctx, o);
    return s != nullptr ? std::string(s) : std::string();
}

std::string streamText(fz_context* ctx, pdf_obj* stream) {
    if (!pdf_is_stream(ctx, stream)) {
        return {};
    }
    fz_buffer* buf = pdf_load_stream(ctx, stream);
    unsigned char* data = nullptr;
    const std::size_t len = fz_buffer_storage(ctx, buf, &data);
    std::string out(reinterpret_cast<const char*>(data), len);
    fz_drop_buffer(ctx, buf);
    return out;
}

Raw rawOf(Document& doc, int objnum) {
    Raw r;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::mu::call(ctx, [&] {
        pdf_obj* a = pdf_load_object(ctx, pdf, objnum);
        const char* da = pdf_dict_get_text_string(ctx, a, PDF_NAME(DA));
        r.da = da != nullptr ? da : "";
        r.le = nameOf(ctx, pdf_dict_get(ctx, a, PDF_NAME(LE)));
        r.it = nameOf(ctx, pdf_dict_get(ctx, a, PDF_NAME(IT)));
        pdf_obj* cl = pdf_dict_get(ctx, a, PDF_NAME(CL));
        for (int i = 0; i < pdf_array_len(ctx, cl); ++i) {
            r.cl.push_back(pdf_array_get_real(ctx, cl, i));
        }
        if (pdf_dict_get(ctx, a, PDF_NAME(Q)) != nullptr) {
            r.q = pdf_dict_get_int(ctx, a, PDF_NAME(Q));
        }
        r.rotate = pdf_dict_get_int(ctx, a, PDF_NAME(Rotate));
        r.hasRd = pdf_dict_get(ctx, a, PDF_NAME(RD)) != nullptr;
        r.rect = pdf_dict_get_rect(ctx, a, PDF_NAME(Rect));
        pdf_obj* n = pdf_dict_getp(ctx, a, "AP/N");
        r.apContents = streamText(ctx, n);
        pdf_obj* fonts = pdf_dict_getp(ctx, n, "Resources/Font");
        if (pdf_dict_len(ctx, fonts) > 0) {
            r.apFont = nameOf(ctx, pdf_dict_get_key(ctx, fonts, 0));
            r.apFontNum = pdf_to_num(ctx, pdf_dict_get_val(ctx, fonts, 0));
        }
        pdf_drop_obj(ctx, a);
        char fontName[64] = {};
        float size = 0;
        int nc = 0;
        float color[4] = {};
        pdf_parse_default_appearance_unmapped(ctx, r.da.c_str(), fontName, sizeof fontName, &size, &nc,
                                              color);
        pdf_obj* drFonts = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/DR/Font");
        pdf_obj* font = pdf_dict_gets(ctx, drFonts, fontName);
        r.drFontNum = pdf_to_num(ctx, font);
        r.drSubtype = nameOf(ctx, pdf_dict_get(ctx, font, PDF_NAME(Subtype)));
        r.drEncoding = nameOf(ctx, pdf_dict_get(ctx, font, PDF_NAME(Encoding)));
        r.drBaseFont = nameOf(ctx, pdf_dict_get(ctx, font, PDF_NAME(BaseFont)));
        pdf_obj* descendant = pdf_array_get(ctx, pdf_dict_get(ctx, font, PDF_NAME(DescendantFonts)), 0);
        r.drFontFile2 = pdf_is_stream(ctx, pdf_dict_getp(ctx, descendant, "FontDescriptor/FontFile2")) != 0;
        r.toUnicode = streamText(ctx, pdf_dict_get(ctx, font, PDF_NAME(ToUnicode)));
    });
    return r;
}

// Глифы /W шрифта Type0 объекта num.
std::vector<int> widthGids(Document& doc, int num) {
    std::vector<int> out;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::mu::call(ctx, [&] {
        pdf_obj* font = pdf_load_object(ctx, pdf, num);
        pdf_obj* w = pdf_dict_get(
            ctx, pdf_array_get(ctx, pdf_dict_get(ctx, font, PDF_NAME(DescendantFonts)), 0), PDF_NAME(W));
        for (int i = 0; i < pdf_array_len(ctx, w);) {
            const int first = pdf_array_get_int(ctx, w, i);
            pdf_obj* next = pdf_array_get(ctx, w, i + 1);
            if (pdf_is_array(ctx, next)) {
                for (int k = 0; k < pdf_array_len(ctx, next); ++k) {
                    out.push_back(first + k);
                }
                i += 2;
            } else {
                for (int c = first; c <= pdf_to_int(ctx, next); ++c) {
                    out.push_back(c);
                }
                i += 3;
            }
        }
        pdf_drop_obj(ctx, font);
    });
    return out;
}

// Текст аннотации objnum через stext её вида.
std::string annotText(Document& doc, int page, int objnum) {
    fz_context* ctx = doc.context().get();
    ot::mu::PageHandle handle(ctx);
    ot::mu::StextPageHandle text(ctx);
    ot::mu::BufferHandle buf(ctx);
    std::string out;
    ot::mu::call(ctx, [&] {
        handle.reset(fz_load_page(ctx, doc.handle(), page));
        for (pdf_annot* a = pdf_first_annot(ctx, pdf_page_from_fz_page(ctx, handle)); a != nullptr;
             a = pdf_next_annot(ctx, a)) {
            if (pdf_to_num(ctx, pdf_annot_obj(ctx, a)) == objnum) {
                text.reset(pdf_new_stext_page_from_annot(ctx, a, nullptr));
            }
        }
        if (text) {
            buf.reset(fz_new_buffer_from_stext_page(ctx, text));
            out = fz_string_from_buffer(ctx, buf);
        }
    });
    return out;
}

int inkPixels(const ot::Bitmap& with, const ot::Bitmap& without, const RectF& r) {
    int n = 0;
    for (int y = std::max(0, static_cast<int>(r.y0)); y < std::min(with.height, static_cast<int>(r.y1));
         ++y) {
        for (int x = std::max(0, static_cast<int>(r.x0)); x < std::min(with.width, static_cast<int>(r.x1));
             ++x) {
            for (int k = 0; k < with.components; ++k) {
                const auto i = static_cast<std::size_t>(x * with.components + k);
                if (with.row(y)[i] != without.row(y)[i]) {
                    ++n;
                    break;
                }
            }
        }
    }
    return n;
}

fs::path blankFile(const ot::test::TempDir& dir, const char* name) {
    const fs::path path = dir.path() / name;
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), 1));
    return path;
}

}  // namespace

TEST(freetext, wrap_text_by_width) {
    const auto unit = [](char32_t) { return 1.0f; };
    using Lines = std::vector<std::u32string>;
    EXPECT_EQ(ot::annot::wrapText(U"aa bb cc", 5, unit), (Lines{U"aa bb", U"cc"}));
    EXPECT_EQ(ot::annot::wrapText(U"abcdefgh", 3, unit), (Lines{U"abc", U"def", U"gh"}));
    EXPECT_EQ(ot::annot::wrapText(U"a\n\nb", 10, unit), (Lines{U"a", U"", U"b"}));
    EXPECT_EQ(ot::annot::wrapText(U"a\r\nb", 10, unit), (Lines{U"a", U"b"}));
    EXPECT_EQ(ot::annot::wrapText(U"", 10, unit), (Lines{U""}));
    EXPECT_EQ(ot::annot::wrapText(U"aa    bb", 4, unit), (Lines{U"aa", U"bb"}));
}

TEST(freetext, font_names) {
    EXPECT_EQ(ot::annot::textFontFamily("Helv"), "Arial");
    EXPECT_EQ(ot::annot::textFontFamily("TiRo"), "Times New Roman");
    EXPECT_EQ(ot::annot::textFontFamily("Cour"), "Courier New");
    EXPECT_EQ(ot::annot::textFontFamily("SegoeUI"), "Segoe UI");
    EXPECT_EQ(ot::annot::textFontFamily("segoe ui"), "Segoe UI");
    EXPECT_EQ(ot::annot::textFontFamily("Unknown"), "Arial");
    EXPECT_STREQ(ot::annot::textFont("courier new").resource, "CourierNew");
}

// Кириллица (критерий этапа 5): шрифт /DA — встроенный TrueType Type0 Identity-H с /ToUnicode в /AcroForm /DR
// и в ресурсах вида; поле рисуется и извлекается «Привет»; undo убирает поле, redo возвращает.
TEST(freetext, cyrillic_embeds_truetype_in_da_and_dr) {
    OT_FEATURE("comment-04");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotProps p = textBox("Привет, мир", {60, 60, 260, 120}, "Segoe UI", 18);
    const AnnotRef ref = ot::annot::create(*doc, 0, p);
    const Raw r = rawOf(*doc, ref.objnum);
    EXPECT_EQ(r.da.rfind("/SegoeUI 18 Tf", 0), 0u) << r.da;
    EXPECT_EQ(r.apFont, "SegoeUI");
    EXPECT_GT(r.drFontNum, 0);
    EXPECT_EQ(r.apFontNum, r.drFontNum);
    EXPECT_EQ(r.drSubtype, "Type0");
    EXPECT_EQ(r.drEncoding, "Identity-H");
    EXPECT_NE(r.drBaseFont.find("+SegoeUI"), std::string::npos) << r.drBaseFont;  // подмножество
    EXPECT_TRUE(r.drFontFile2);
    EXPECT_NE(r.toUnicode.find("<041F>"), std::string::npos);  // «П»
    EXPECT_EQ(r.apContents.find("Helv"), std::string::npos);

    const auto props = ot::annot::readProps(*doc, ref);
    ASSERT_TRUE(props);
    EXPECT_EQ(props->fontName, "Segoe UI");
    EXPECT_FLOAT_EQ(props->fontSize, 18);
    EXPECT_NEAR(props->rect.x0, 60, 0.1);
    EXPECT_NEAR(props->rect.y1, 120, 0.1);

    const ot::Bitmap with = doc->renderPage(0, {.dpi = 72, .annotations = true});
    const ot::Bitmap without = doc->renderPage(0, {.dpi = 72, .annotations = false});
    EXPECT_GE(inkPixels(with, without, p.rect), 50);
    EXPECT_NE(annotText(*doc, 0, ref.objnum).find("Привет"), std::string::npos);

    // Подмножество: ASCII, Latin-1, русский алфавит и знаки — да, весь шрифт — нет.
    const std::vector<int> gids = widthGids(*doc, r.drFontNum);
    EXPECT_GT(gids.size(), 200u);
    EXPECT_LT(gids.size(), 600u);

    doc->undoStack().undo();
    EXPECT_TRUE(ot::annot::listAnnots(*doc, 0).empty());
    doc->undoStack().redo();
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), 1u);
}

// Выноска: /IT /FreeTextCallout, /CL из трёх точек, /LE — имя, /Q и /RD переживают запись и переоткрытие.
TEST(freetext, callout_keys_survive_reopen) {
    OT_FEATURE("comment-05");
    ot::test::TempDir dir;
    const fs::path path = blankFile(dir, "callout.pdf");
    AnnotProps p = textBox("Выноска", {200, 100, 340, 140});
    p.intent = "FreeTextCallout";
    p.callout = {{80, 260}, {170, 120}, {200, 120}};
    p.lineStartStyle = ot::annot::LineEnding::OpenArrow;
    p.align = 1;
    p.borderWidth = 1;
    std::string nm;
    {
        auto doc = Document::openOrThrow(path);
        const AnnotRef ref = ot::annot::create(*doc, 0, p);
        nm = ref.nm;
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    auto doc = Document::openOrThrow(path);
    const auto list = ot::annot::listAnnots(*doc, 0);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list.front().ref.nm, nm);
    const Raw r = rawOf(*doc, list.front().ref.objnum);
    EXPECT_EQ(r.it, "FreeTextCallout");
    EXPECT_EQ(r.le, "OpenArrow");
    ASSERT_EQ(r.cl.size(), 6u);
    EXPECT_EQ(r.q, 1);
    EXPECT_TRUE(r.hasRd);
    EXPECT_EQ(r.da.rfind("/Arial 14 Tf", 0), 0u) << r.da;
    // /Rect охватывает конец стрелки (страница A4: y PDF = 842 − y страницы).
    EXPECT_LE(r.rect.x0, 80 - 1);
    EXPECT_GE(r.rect.y1, 842 - 120);
    EXPECT_LE(r.rect.y0, 842 - 260 - 1);

    const auto props = ot::annot::readProps(*doc, list.front().ref);
    ASSERT_TRUE(props);
    EXPECT_EQ(props->intent, "FreeTextCallout");
    ASSERT_EQ(props->callout.size(), 3u);
    EXPECT_NEAR(props->callout[0].x, 80, 0.1);
    EXPECT_NEAR(props->callout[0].y, 260, 0.1);
    EXPECT_NEAR(props->callout[1].x, 170, 0.1);
    EXPECT_EQ(props->lineStartStyle, ot::annot::LineEnding::OpenArrow);
    EXPECT_EQ(props->align, 1);
    EXPECT_NEAR(props->rect.x0, 200, 0.1);
    EXPECT_NEAR(props->rect.y0, 100, 0.1);
    EXPECT_NEAR(props->rect.x1, 340, 0.1);
    EXPECT_NEAR(props->rect.y1, 140, 0.1);
    EXPECT_NE(r.apContents.find(" l\n"), std::string::npos);  // линия выноски
}

// Правка свойств, рамки и флагов: вид — наш, со шрифтом /DA (не Helv MuPDF).
TEST(freetext, update_resize_flags_keep_own_font) {
    OT_FEATURE("comment-09");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef ref = ot::annot::create(*doc, 0, textBox("Первый", {60, 60, 260, 100}));
    ot::annot::AnnotPatch patch;
    patch.contents = "Второй текст";
    patch.fontName = "Times New Roman";
    patch.fontSize = 16;
    patch.textColor = AnnotColor::rgb(0.8f, 0, 0);
    patch.align = 2;
    ot::annot::update(*doc, ref, patch);
    Raw r = rawOf(*doc, ref.objnum);
    EXPECT_EQ(r.da.rfind("/TimesNewRoman 16 Tf", 0), 0u) << r.da;
    EXPECT_EQ(r.apFont, "TimesNewRoman");
    EXPECT_EQ(r.q, 2);
    EXPECT_NE(annotText(*doc, 0, ref.objnum).find("Второй"), std::string::npos);
    auto props = ot::annot::readProps(*doc, ref);
    ASSERT_TRUE(props);
    EXPECT_EQ(props->fontName, "Times New Roman");
    EXPECT_FLOAT_EQ(props->fontSize, 16);
    EXPECT_NEAR(props->textColor.c[0], 0.8f, 0.01f);

    ot::annot::setRect(*doc, ref, {60, 60, 160, 160});
    r = rawOf(*doc, ref.objnum);
    EXPECT_EQ(r.apFont, "TimesNewRoman");
    EXPECT_EQ(r.apContents.find("Helv"), std::string::npos);
    EXPECT_NE(annotText(*doc, 0, ref.objnum).find("Второй"), std::string::npos);

    ot::annot::setFlags(*doc, {ref}, ot::annot::kAnnotLocked, 0);
    r = rawOf(*doc, ref.objnum);
    EXPECT_EQ(r.apFont, "TimesNewRoman");
    // Рендер не пересинтезирует вид (needs_new_ap снят).
    doc->renderPage(0, {.dpi = 36, .annotations = true});
    r = rawOf(*doc, ref.objnum);
    EXPECT_EQ(r.apFont, "TimesNewRoman");
}

// Поля одного семейства делят шрифт; знак вне подмножества — новый шрифт с ним, /DR указывает на новый.
TEST(freetext, boxes_share_font_until_glyph_missing) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef a = ot::annot::create(*doc, 0, textBox("Один", {60, 60, 200, 90}));
    const AnnotRef b = ot::annot::create(*doc, 0, textBox("Два — №5, €", {60, 100, 200, 130}));
    const Raw ra = rawOf(*doc, a.objnum);
    const Raw rb = rawOf(*doc, b.objnum);
    EXPECT_EQ(ra.apFontNum, rb.apFontNum);
    EXPECT_EQ(rb.drFontNum, ra.drFontNum);
    const AnnotRef c = ot::annot::create(*doc, 0, textBox("Δ = ∑", {60, 140, 200, 170}));
    const Raw rc = rawOf(*doc, c.objnum);
    EXPECT_NE(rc.apFontNum, ra.apFontNum);
    EXPECT_EQ(rc.drFontNum, rc.apFontNum);
    EXPECT_NE(annotText(*doc, 0, c.objnum).find("Δ"), std::string::npos);
    // Прежние поля рисуются своим шрифтом.
    EXPECT_EQ(rawOf(*doc, a.objnum).apFontNum, ra.apFontNum);
}

// Страница /Rotate 90: поле получает /Rotate 90 (текст читается на повёрнутой странице, как у Acrobat).
TEST(freetext, rotated_page_rotates_text) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    {
        ot::Journal::Operation op(*doc, "Rotate");
        ot::mu::call(ctx,
                     [&] { pdf_dict_put_int(ctx, pdf_lookup_page_obj(ctx, pdf, 0), PDF_NAME(Rotate), 90); });
        op.invalidate(0);
        op.commit();
    }
    const AnnotRef ref = ot::annot::create(*doc, 0, textBox("Повёрнуто", {60, 60, 260, 100}));
    const Raw r = rawOf(*doc, ref.objnum);
    EXPECT_EQ(r.rotate, 90);
    const auto props = ot::annot::readProps(*doc, ref);
    ASSERT_TRUE(props);
    EXPECT_NEAR(props->rect.x0, 60, 0.1);
    EXPECT_NEAR(props->rect.x1, 260, 0.1);
    EXPECT_NE(annotText(*doc, 0, ref.objnum).find("Повёрнуто"), std::string::npos);
}

// «Без рамки и заливки» — вид без прямоугольников; рамка и заливка /C — с ними.
TEST(freetext, border_and_fill) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps plain = textBox("Без рамки", {60, 60, 260, 100});
    plain.borderWidth = 0;
    plain.color = {};
    const AnnotRef a = ot::annot::create(*doc, 0, plain);
    Raw r = rawOf(*doc, a.objnum);
    EXPECT_EQ(r.apContents.find("re\nf"), std::string::npos) << r.apContents;
    EXPECT_EQ(r.apContents.find("re\nS"), std::string::npos) << r.apContents;
    AnnotProps framed = textBox("С рамкой", {60, 120, 260, 160});
    framed.borderWidth = 2;
    framed.color = AnnotColor::rgb(1, 1, 0.8f);
    const AnnotRef b = ot::annot::create(*doc, 0, framed);
    r = rawOf(*doc, b.objnum);
    EXPECT_NE(r.apContents.find("re\nf"), std::string::npos) << r.apContents;
    EXPECT_NE(r.apContents.find("re\nS"), std::string::npos) << r.apContents;
    const auto props = ot::annot::readProps(*doc, b);
    ASSERT_TRUE(props);
    EXPECT_FLOAT_EQ(props->borderWidth, 2);
    EXPECT_EQ(props->color.n, 3);
}
