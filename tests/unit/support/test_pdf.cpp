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
