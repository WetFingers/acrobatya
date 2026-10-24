// unit.overprint.* — предпросмотр наложения красок (план 2.5b; эталон 3.1 «PDF/A (pro-45)… overprint
// preview»; pro-47): страница с пурпурным прямоугольником поверх голубого с /OP true /OPM 1 — без симуляции
// пересечение пурпурное, с симуляцией (fz_separations у растра) — синее (голубой + пурпурный). Режим
// «Всегда / Только для PDF/X / Никогда» решает simulateOverprint по OutputIntents /S /GTS_PDFX документа;
// страница без overprint с симуляцией не меняется (DisplayList::usesOverprint — симуляция не включается
// вовсе). Фикстура — PDF в памяти (MuPDF).
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/render/display_cache.h"
#include "core/render/renderer.h"
#include "core/render/tiles.h"
#include "core/util/features.h"

extern "C" {
#include <mupdf/pdf.h>
}

using ot::Document;
using ot::IRect;
using ot::OverprintPreview;
using ot::PixelFormat;
using ot::RenderStyle;

namespace {

// Страница 200 × 200: голубой (20…140), поверх — пурпурный (60…180); overprint — у пурпурного, если op.
std::string makeOverprintPdf(bool op, bool pdfx) {
    fz_context* ctx = ot::MuContext::global().get();
    std::string bytes;
    pdf_document* pdf = nullptr;
    pdf_obj* resources = nullptr;
    fz_buffer* contents = nullptr;
    fz_buffer* out = nullptr;
    fz_output* stream = nullptr;
    pdf_obj* page = nullptr;
    const char* ops = op ? "1 0 0 0 k 20 20 120 120 re f /GS1 gs 0 1 0 0 k 60 60 120 120 re f\n"
                         : "1 0 0 0 k 20 20 120 120 re f 0 1 0 0 k 60 60 120 120 re f\n";
    ot::mu::call(ctx, [&] {
        pdf = pdf_create_document(ctx);
        resources = pdf_new_dict(ctx, pdf, 1);
        if (op) {  // ExtGState с /OP в ресурсах — уже «страница с overprint» (fz_page_uses_overprint)
            pdf_obj* gs = pdf_dict_put_dict(ctx, resources, PDF_NAME(ExtGState), 1);
            pdf_obj* gs1 = pdf_dict_puts_dict(ctx, gs, "GS1", 3);
            pdf_dict_put_bool(ctx, gs1, PDF_NAME(OP), 1);
            pdf_dict_put_bool(ctx, gs1, PDF_NAME(op), 1);
            pdf_dict_put_int(ctx, gs1, PDF_NAME(OPM), 1);
        }
        contents =
            fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(ops), strlen(ops));
        page = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 200, 200), 0, resources, contents);
        pdf_insert_page(ctx, pdf, -1, page);
        if (pdfx) {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
            pdf_obj* intents = pdf_dict_put_array(ctx, root, PDF_NAME(OutputIntents), 1);
            pdf_obj* intent = pdf_array_push_dict(ctx, intents, 4);
            pdf_dict_put(ctx, intent, PDF_NAME(Type), PDF_NAME(OutputIntent));
            pdf_dict_put_name(ctx, intent, PDF_NAME(S), "GTS_PDFX");
            pdf_dict_put_text_string(ctx, intent, PDF_NAME(OutputConditionIdentifier), "FOGRA39");
        }
        out = fz_new_buffer(ctx, 1024);
        stream = fz_new_output_with_buffer(ctx, out);
        pdf_write_options opts = pdf_default_write_options;
        pdf_write_document(ctx, pdf, stream, &opts);
        fz_close_output(ctx, stream);
        unsigned char* data = nullptr;
        const size_t len = fz_buffer_storage(ctx, out, &data);
        bytes.assign(reinterpret_cast<const char*>(data), len);
    });
    fz_drop_output(ctx, stream);
    fz_drop_buffer(ctx, out);
    fz_drop_buffer(ctx, contents);
    pdf_drop_obj(ctx, page);
    pdf_drop_obj(ctx, resources);
    pdf_drop_document(ctx, pdf);
    return bytes;
}

// Пиксель (x, y) страницы при 72 dpi (y вниз), RGB.
std::vector<int> pixel(const Document& doc, bool overprint, int x, int y) {
    const auto list = ot::DisplayList::build(doc, 0);
    const ot::Matrix m = ot::pageMatrix(list->bounds(), 1.0f, 1.0f);
    const IRect area = ot::pageDeviceRect(list->bounds(), 1.0f, 1.0f);
    std::vector<std::uint8_t> rgb(static_cast<size_t>(area.width()) * area.height() * 3);
    RenderStyle style;
    style.overprint = overprint;
    ot::renderDisplayList(ot::MuContext::forThisThread().get(), *list, m, area, rgb.data(), area.width() * 3,
                          PixelFormat::Rgb24, nullptr, style);
    const std::uint8_t* p = rgb.data() + (static_cast<size_t>(y) * area.width() + x) * 3;
    return {p[0], p[1], p[2]};
}

// Цвета равны с точностью до округления перевода CMYK → RGB (симуляция идёт через группу CMYK).
void expectNear(const std::vector<int>& a, const std::vector<int>& b) {
    for (int i = 0; i < 3; ++i) {
        EXPECT_LE(std::abs(a[i] - b[i]), 2) << i << ": " << a[i] << " vs " << b[i];
    }
}

}  // namespace

TEST(overprint, simulation_mixes_cyan_and_magenta) {
    OT_FEATURE("pro-47");
    auto doc = Document::openOrThrowMemory(makeOverprintPdf(true, true));
    EXPECT_TRUE(doc->isPdfX());
    EXPECT_TRUE(ot::DisplayList::build(*doc, 0)->usesOverprint());
    const std::vector<int> plain = pixel(*doc, false, 100, 100);  // пересечение
    const std::vector<int> simulated = pixel(*doc, true, 100, 100);
    // Без симуляции — пурпурный (красного много, зелёного нет), с симуляцией — синий (красного мало).
    EXPECT_GT(plain[0], 180) << plain[0] << "," << plain[1] << "," << plain[2];
    EXPECT_LT(simulated[0], 110) << simulated[0] << "," << simulated[1] << "," << simulated[2];
    EXPECT_GT(simulated[2], 100);
    // Вне пересечения — тот же цвет.
    expectNear(pixel(*doc, false, 30, 170), pixel(*doc, true, 30, 170));  // только голубой
    expectNear(pixel(*doc, false, 170, 30), pixel(*doc, true, 170, 30));  // только пурпурный
}

TEST(overprint, page_without_overprint_is_unchanged) {
    auto doc = Document::openOrThrowMemory(makeOverprintPdf(false, false));
    EXPECT_FALSE(doc->isPdfX());
    EXPECT_FALSE(ot::DisplayList::build(*doc, 0)->usesOverprint());
    EXPECT_EQ(pixel(*doc, false, 100, 100), pixel(*doc, true, 100, 100));
}

TEST(overprint, preview_mode_follows_pdfx) {
    using ot::simulateOverprint;
    EXPECT_TRUE(simulateOverprint(OverprintPreview::Always, false));
    EXPECT_TRUE(simulateOverprint(OverprintPreview::Always, true));
    EXPECT_TRUE(simulateOverprint(OverprintPreview::PdfX, true));
    EXPECT_FALSE(simulateOverprint(OverprintPreview::PdfX, false));
    EXPECT_FALSE(simulateOverprint(OverprintPreview::Never, true));
    // «Только для PDF/X»: тот же файл без OutputIntents — пиксели как без симуляции.
    auto pdfx = Document::openOrThrowMemory(makeOverprintPdf(true, true));
    auto plain = Document::openOrThrowMemory(makeOverprintPdf(true, false));
    const auto mode = OverprintPreview::PdfX;
    EXPECT_NE(pixel(*pdfx, simulateOverprint(mode, pdfx->isPdfX()), 100, 100), pixel(*pdfx, false, 100, 100));
    EXPECT_EQ(pixel(*plain, simulateOverprint(mode, plain->isPdfX()), 100, 100),
              pixel(*plain, false, 100, 100));
    EXPECT_EQ(ot::overprintPreviewFromName("always"), OverprintPreview::Always);
    EXPECT_EQ(ot::overprintPreviewFromName("never"), OverprintPreview::Never);
    EXPECT_EQ(ot::overprintPreviewFromName("pdfx"), OverprintPreview::PdfX);
    EXPECT_EQ(ot::overprintPreviewFromName("что-то"), OverprintPreview::PdfX);
}
