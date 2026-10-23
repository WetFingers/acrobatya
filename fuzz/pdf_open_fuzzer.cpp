// pdf_open_fuzzer — libFuzzer-цель (эталон 9.5), пресет msvc-fuzz: fz_open_memory →
// fz_open_document_with_stream → fz_count_pages → рендер 32×32 первой страницы → stext → закрыть.
//
// Рубеж: pdf_open_fuzzer.exe <корпус> -max_total_time=600 -rss_limit_mb=4096 -timeout=20.
//
// Упавшие входы → tests/fixtures/regress/ + тест unit.regress.load.
//
// fz_try здесь допустим: цель не входит в src/ (страж fztry проверяет src/), это отдельная программа.
#include <cstddef>
#include <cstdint>

extern "C" {
#include <mupdf/fitz.h>
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    fz_context* ctx = fz_new_context(nullptr, nullptr, 64 << 20);
    if (ctx == nullptr) {
        return 0;
    }
    fz_stream* stm = nullptr;
    fz_document* doc = nullptr;
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;
    fz_stext_page* text = nullptr;
    fz_var(stm);
    fz_var(doc);
    fz_var(page);
    fz_var(pix);
    fz_var(text);
    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        stm = fz_open_memory(ctx, data, size);
        doc = fz_open_document_with_stream(ctx, "application/pdf", stm);
        if (fz_count_pages(ctx, doc) > 0) {
            page = fz_load_page(ctx, doc, 0);
            const fz_rect box = fz_bound_page(ctx, page);
            const float w = box.x1 - box.x0, h = box.y1 - box.y0;
            const float s = (w > 0 && h > 0) ? 32.0f / (w > h ? w : h) : 1.0f;
            pix = fz_new_pixmap_from_page(ctx, page, fz_scale(s, s), fz_device_rgb(ctx), 0);
            text = fz_new_stext_page_from_page(ctx, page, nullptr);
        }
    }
    fz_always(ctx) {
        fz_drop_stext_page(ctx, text);
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
        fz_drop_document(ctx, doc);
        fz_drop_stream(ctx, stm);
    }
    fz_catch(ctx) {
        fz_report_error(ctx);
    }
    fz_drop_context(ctx);
    return 0;
}
