#include "support/test_pdf.h"

#include <atomic>
#include <chrono>
#include <system_error>

#include <windows.h>

#include "core/mu/call.h"

extern "C" {
#include <mupdf/pdf.h>
}
#include "core/util/paths.h"

namespace ot::test {

std::string makePdf(MuContext& mu, int pages) {
    fz_context* ctx = mu.get();
    pdf_document* pdf = nullptr;
    pdf_obj* page = nullptr;
    pdf_obj* resources = nullptr;
    fz_buffer* contents = nullptr;
    fz_buffer* out = nullptr;
    fz_output* stream = nullptr;
    fz_font* font = nullptr;
    std::string bytes;
    mu::call(ctx, [&] {
        pdf = pdf_create_document(ctx);
        for (int i = 0; i < pages; ++i) {
            resources = pdf_new_dict(ctx, pdf, 1);
            pdf_obj* fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
            if (font == nullptr) {
                font = fz_new_base14_font(ctx, "Helvetica");
            }
            pdf_dict_puts_drop(ctx, fonts, "F1",
                               pdf_add_simple_font(ctx, pdf, font, PDF_SIMPLE_ENCODING_LATIN));
            contents = fz_new_buffer(ctx, 256);
            // y от нижнего края: A4 = 842 pt; квадрат 100..200 по x, 642..742 по y (100 pt от верха)
            fz_append_printf(ctx, contents,
                             "0 0 0 rg 100 642 100 100 re f\nBT /F1 24 Tf 100 500 Td (Page %d) Tj ET\n",
                             i + 1);
            page = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, resources, contents);
            pdf_insert_page(ctx, pdf, -1, page);
            pdf_drop_obj(ctx, page);
            page = nullptr;
            pdf_drop_obj(ctx, resources);
            resources = nullptr;
            fz_drop_buffer(ctx, contents);
            contents = nullptr;
        }
        out = fz_new_buffer(ctx, 4096);
        stream = fz_new_output_with_buffer(ctx, out);
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 0;
        pdf_write_document(ctx, pdf, stream, &opts);
        fz_close_output(ctx, stream);
    });
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, out, &data);
    bytes.assign(reinterpret_cast<const char*>(data), len);
    fz_drop_output(ctx, stream);
    fz_drop_font(ctx, font);
    fz_drop_buffer(ctx, out);
    fz_drop_buffer(ctx, contents);
    pdf_drop_obj(ctx, resources);
    pdf_drop_obj(ctx, page);
    pdf_drop_document(ctx, pdf);
    return bytes;
}

std::string makeHeavyPdf(MuContext& mu, int curves, bool clipped) {
    fz_context* ctx = mu.get();
    pdf_document* pdf = nullptr;
    pdf_obj* page = nullptr;
    pdf_obj* resources = nullptr;
    fz_buffer* contents = nullptr;
    fz_buffer* out = nullptr;
    fz_output* stream = nullptr;
    std::string bytes;
    // Линейный конгруэнтный генератор: те же кривые при каждом запуске.
    unsigned int seed = 1;
    auto next = [&seed](int range) {
        seed = seed * 1103515245u + 12345u;
        return static_cast<int>((seed >> 8) % static_cast<unsigned int>(range));
    };
    std::string ops;
    ops.reserve(static_cast<size_t>(curves) * 96);
    if (clipped) {
        ops += "q 10 10 575 822 re W n\n";  // всё под обрезкой: отмена оставит clip на стеке устройства
    }
    ops += "3 w\n";
    for (int i = 0; i < curves; ++i) {
        ops += std::to_string(next(256)) + " " + std::to_string(next(256)) + " " + std::to_string(next(256)) +
               " RG\n";
        ops += std::to_string(next(595)) + " 0 m " + std::to_string(next(595)) + " " +
               std::to_string(next(842)) + " " + std::to_string(next(595)) + " " + std::to_string(next(842)) +
               " " + std::to_string(next(595)) + " 842 c S\n";
    }
    if (clipped) {
        ops += "Q\n";
    }
    mu::call(ctx, [&] {
        pdf = pdf_create_document(ctx);
        resources = pdf_new_dict(ctx, pdf, 1);
        contents = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(ops.data()),
                                                  ops.size());
        page = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, resources, contents);
        pdf_insert_page(ctx, pdf, -1, page);
        out = fz_new_buffer(ctx, 4096);
        stream = fz_new_output_with_buffer(ctx, out);
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;
        pdf_write_document(ctx, pdf, stream, &opts);
        fz_close_output(ctx, stream);
    });
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, out, &data);
    bytes.assign(reinterpret_cast<const char*>(data), len);
    fz_drop_output(ctx, stream);
    fz_drop_buffer(ctx, out);
    fz_drop_buffer(ctx, contents);
    pdf_drop_obj(ctx, resources);
    pdf_drop_obj(ctx, page);
    pdf_drop_document(ctx, pdf);
    return bytes;
}

namespace {

// Градиентная RGB-картинка w × h (узор зависит от seed).
fz_pixmap* gradientPixmap(fz_context* ctx, int w, int h, int seed) {
    fz_pixmap* pix = nullptr;
    mu::call(ctx, [&] { pix = fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, nullptr, 0); });
    unsigned char* s = fz_pixmap_samples(ctx, pix);
    const int stride = fz_pixmap_stride(ctx, pix);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            unsigned char* p = s + static_cast<ptrdiff_t>(y) * stride + x * 3;
            p[0] = static_cast<unsigned char>((x * 255) / (w - 1));
            p[1] = static_cast<unsigned char>((y * 255) / (h - 1));
            p[2] = static_cast<unsigned char>(((x / 7 + y / 5 + seed) % 2) * 200 + 30);
        }
    }
    return pix;
}

}  // namespace

std::string makeMixedPdf(MuContext& mu, MixedContent content) {
    fz_context* ctx = mu.get();
    pdf_document* pdf = nullptr;
    pdf_obj* page = nullptr;
    pdf_obj* resources = nullptr;
    fz_buffer* contents = nullptr;
    fz_buffer* out = nullptr;
    fz_output* stream = nullptr;
    fz_font* helv = nullptr;
    fz_font* times = nullptr;
    fz_image* small = nullptr;
    fz_image* large = nullptr;
    fz_pixmap* smallPix = gradientPixmap(ctx, 100, 80, 0);
    fz_pixmap* largePix = gradientPixmap(ctx, 1200, 900, 1);
    std::string bytes;
    std::string opsText =
        "0.8 0.1 0.1 rg 60 300 220 40 re f\n"
        "0.1 0.1 0.6 rg 333.3 610.7 151.9 77.3 re f\n"
        "BT /F1 18 Tf 60 500 Td (Mosaic tiles: Helvetica 18) Tj ET\n"
        "BT /F2 11 Tf 60 470 Td (The quick brown fox jumps over the lazy dog 0123456789) Tj ET\n"
        "BT /F2 7 Tf 300 40 Td (small text at seven points near the bottom edge) Tj ET\n"
        "BT /F1 9 Tf 420 760 Td (Edge 1 2 3 4 5 6 7 8 9) Tj ET\n";
    if (content != MixedContent::TextAndRects) {
        opsText += "0 0 0.7 RG 2.5 w 60 120 m 200 300 380 20 540 260 c S\n";
        opsText += "0.2 0.6 0.2 rg 0 0 0 RG 1 w 400 100 m 520 100 l 460 220 l h B\n";
    }
    if (content == MixedContent::Full) {
        opsText += "q 300 0 0 240 50 550 cm /Im1 Do Q\n";
        opsText += "q 200 0 0 150 360 360 cm /Im2 Do Q\n";
    }
    const char* ops = opsText.c_str();
    mu::call(ctx, [&] {
        pdf = pdf_create_document(ctx);
        resources = pdf_new_dict(ctx, pdf, 2);
        pdf_obj* fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 2);
        helv = fz_new_base14_font(ctx, "Helvetica");
        times = fz_new_base14_font(ctx, "Times-Roman");
        pdf_dict_puts_drop(ctx, fonts, "F1", pdf_add_simple_font(ctx, pdf, helv, PDF_SIMPLE_ENCODING_LATIN));
        pdf_dict_puts_drop(ctx, fonts, "F2", pdf_add_simple_font(ctx, pdf, times, PDF_SIMPLE_ENCODING_LATIN));
        pdf_obj* xobj = pdf_dict_put_dict(ctx, resources, PDF_NAME(XObject), 2);
        small = fz_new_image_from_pixmap(ctx, smallPix, nullptr);
        large = fz_new_image_from_pixmap(ctx, largePix, nullptr);
        pdf_dict_puts_drop(ctx, xobj, "Im1", pdf_add_image(ctx, pdf, small));
        pdf_dict_puts_drop(ctx, xobj, "Im2", pdf_add_image(ctx, pdf, large));
        contents =
            fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(ops), strlen(ops));
        page = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, resources, contents);
        pdf_insert_page(ctx, pdf, -1, page);
        out = fz_new_buffer(ctx, 4096);
        stream = fz_new_output_with_buffer(ctx, out);
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;
        pdf_write_document(ctx, pdf, stream, &opts);
        fz_close_output(ctx, stream);
    });
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, out, &data);
    bytes.assign(reinterpret_cast<const char*>(data), len);
    fz_drop_output(ctx, stream);
    fz_drop_buffer(ctx, out);
    fz_drop_buffer(ctx, contents);
    fz_drop_image(ctx, small);
    fz_drop_image(ctx, large);
    fz_drop_pixmap(ctx, smallPix);
    fz_drop_pixmap(ctx, largePix);
    fz_drop_font(ctx, helv);
    fz_drop_font(ctx, times);
    pdf_drop_obj(ctx, resources);
    pdf_drop_obj(ctx, page);
    pdf_drop_document(ctx, pdf);
    return bytes;
}

std::string makeImagePagesPdf(MuContext& mu, int pages, int w, int h) {
    fz_context* ctx = mu.get();
    pdf_document* pdf = nullptr;
    pdf_obj* page = nullptr;
    pdf_obj* resources = nullptr;
    fz_buffer* contents = nullptr;
    fz_buffer* out = nullptr;
    fz_output* stream = nullptr;
    fz_font* font = nullptr;
    fz_image* image = nullptr;
    fz_pixmap* pix = nullptr;
    std::string bytes;
    mu::call(ctx, [&] {
        pdf = pdf_create_document(ctx);
        font = fz_new_base14_font(ctx, "Helvetica");
    });
    for (int i = 0; i < pages; ++i) {
        pix = gradientPixmap(ctx, w, h, i);
        mu::call(ctx, [&] {
            resources = pdf_new_dict(ctx, pdf, 2);
            pdf_obj* fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
            pdf_dict_puts_drop(ctx, fonts, "F1",
                               pdf_add_simple_font(ctx, pdf, font, PDF_SIMPLE_ENCODING_LATIN));
            pdf_obj* xobj = pdf_dict_put_dict(ctx, resources, PDF_NAME(XObject), 1);
            image = fz_new_image_from_pixmap(ctx, pix, nullptr);
            pdf_dict_puts_drop(ctx, xobj, "Im1", pdf_add_image(ctx, pdf, image));
            contents = fz_new_buffer(ctx, 128);
            fz_append_printf(ctx, contents,
                             "q 595 0 0 842 0 0 cm /Im1 Do Q BT /F1 24 Tf 60 60 Td (Image page %d) Tj ET\n",
                             i + 1);
            page = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, resources, contents);
            pdf_insert_page(ctx, pdf, -1, page);
        });
        pdf_drop_obj(ctx, page);
        page = nullptr;
        pdf_drop_obj(ctx, resources);
        resources = nullptr;
        fz_drop_buffer(ctx, contents);
        contents = nullptr;
        fz_drop_image(ctx, image);
        image = nullptr;
        fz_drop_pixmap(ctx, pix);
        pix = nullptr;
    }
    mu::call(ctx, [&] {
        out = fz_new_buffer(ctx, 1 << 20);
        stream = fz_new_output_with_buffer(ctx, out);
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;
        pdf_write_document(ctx, pdf, stream, &opts);
        fz_close_output(ctx, stream);
    });
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, out, &data);
    bytes.assign(reinterpret_cast<const char*>(data), len);
    fz_drop_output(ctx, stream);
    fz_drop_buffer(ctx, out);
    fz_drop_font(ctx, font);
    pdf_drop_document(ctx, pdf);
    return bytes;
}

TempDir::TempDir() {
    static std::atomic<int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            (std::wstring(L"оттиск-тест-") + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(stamp) + L"-" + std::to_wstring(counter++));
    std::filesystem::create_directories(path_);
}

TempDir::~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

std::filesystem::path sourceDir() { return paths::fromUtf8(OTTISK_SOURCE_DIR); }

}  // namespace ot::test
