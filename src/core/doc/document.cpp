#include "core/doc/document.h"

#include <cwctype>
#include <stdexcept>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/paths.h"

namespace ot {

namespace {

std::string magicFor(const std::filesystem::path& path) {
    std::wstring ext = path.extension().native();
    for (auto& ch : ext) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    if (ext.empty()) {
        return ".pdf";
    }
    return paths::narrow(ext);
}

}  // namespace

Document::Document(MuContext& mu, fz_document* doc, std::string bytes)
    : mu_(mu), doc_(doc), memory_(std::move(bytes)) {}

Document::~Document() {
    if (doc_ != nullptr) {
        fz_drop_document(mu_.get(), doc_);
    }
}

std::unique_ptr<Document> Document::open(MuContext& mu, const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        throw std::runtime_error("file not found: " + paths::toUtf8(path));
    }
    fz_context* ctx = mu.get();
    const std::wstring wide = path.native();
    const std::string magic = magicFor(path);
    fz_stream* stm = nullptr;
    fz_document* doc = nullptr;
    try {
        mu::call(ctx, [&] {
            stm = fz_open_file_w(ctx, wide.c_str());
            doc = fz_open_document_with_stream(ctx, magic.c_str(), stm);
        });
    } catch (...) {
        fz_drop_stream(ctx, stm);
        throw;
    }
    fz_drop_stream(ctx, stm);  // документ держит свою ссылку на поток
    return std::unique_ptr<Document>(new Document(mu, doc, {}));
}

std::unique_ptr<Document> Document::openMemory(MuContext& mu, std::string bytes, const std::string& magic) {
    fz_context* ctx = mu.get();
    fz_stream* stm = nullptr;
    fz_document* doc = nullptr;
    try {
        mu::call(ctx, [&] {
            stm = fz_open_memory(ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size());
            doc = fz_open_document_with_stream(ctx, magic.c_str(), stm);
        });
    } catch (...) {
        fz_drop_stream(ctx, stm);
        throw;
    }
    fz_drop_stream(ctx, stm);
    return std::unique_ptr<Document>(new Document(mu, doc, std::move(bytes)));
}

std::unique_ptr<Document> Document::createBlank(MuContext& mu, int pages) {
    fz_context* ctx = mu.get();
    pdf_document* pdf = nullptr;
    pdf_obj* page = nullptr;
    try {
        mu::call(ctx, [&] {
            pdf = pdf_create_document(ctx);
            for (int i = 0; i < pages; ++i) {
                page = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, nullptr, nullptr);
                pdf_insert_page(ctx, pdf, -1, page);
                pdf_drop_obj(ctx, page);
                page = nullptr;
            }
        });
    } catch (...) {
        pdf_drop_obj(ctx, page);
        pdf_drop_document(ctx, pdf);
        throw;
    }
    return std::unique_ptr<Document>(new Document(mu, &pdf->super, {}));
}

bool Document::needsPassword() const {
    int needs = 0;
    mu::call(mu_.get(), [&] { needs = fz_needs_password(mu_.get(), doc_); });
    return needs != 0;
}

bool Document::authenticate(const std::string& password) {
    int ok = 0;
    mu::call(mu_.get(), [&] { ok = fz_authenticate_password(mu_.get(), doc_, password.c_str()); });
    return ok != 0;
}

int Document::pageCount() const {
    int n = 0;
    mu::call(mu_.get(), [&] { n = fz_count_pages(mu_.get(), doc_); });
    return n;
}

PageSize Document::pageSize(int index) const {
    fz_context* ctx = mu_.get();
    fz_page* page = nullptr;
    fz_rect box{};
    try {
        mu::call(ctx, [&] {
            page = fz_load_page(ctx, doc_, index);
            box = fz_bound_page(ctx, page);
        });
    } catch (...) {
        fz_drop_page(ctx, page);
        throw;
    }
    fz_drop_page(ctx, page);
    return {box.x1 - box.x0, box.y1 - box.y0};
}

Bitmap Document::renderPage(int index, const RenderOptions& options) const {
    fz_context* ctx = mu_.get();
    fz_page* page = nullptr;
    fz_pixmap* pix = nullptr;
    const float scale = options.dpi / 72.0f;
    Bitmap out;
    try {
        mu::call(ctx, [&] {
            page = fz_load_page(ctx, doc_, index);
            const fz_matrix ctm = fz_scale(scale, scale);
            if (options.annotations) {
                pix = fz_new_pixmap_from_page(ctx, page, ctm, fz_device_rgb(ctx), 0);
            } else {
                pix = fz_new_pixmap_from_page_contents(ctx, page, ctm, fz_device_rgb(ctx), 0);
            }
        });
        out.width = fz_pixmap_width(ctx, pix);
        out.height = fz_pixmap_height(ctx, pix);
        out.components = fz_pixmap_components(ctx, pix);
        out.stride = out.width * out.components;
        out.pixels.resize(static_cast<size_t>(out.stride) * static_cast<size_t>(out.height));
        const unsigned char* src = fz_pixmap_samples(ctx, pix);
        const auto srcStride = static_cast<size_t>(fz_pixmap_stride(ctx, pix));
        for (int y = 0; y < out.height; ++y) {
            std::memcpy(out.row(y), src + srcStride * static_cast<size_t>(y),
                        static_cast<size_t>(out.stride));
        }
    } catch (...) {
        fz_drop_pixmap(ctx, pix);
        fz_drop_page(ctx, page);
        throw;
    }
    fz_drop_pixmap(ctx, pix);
    fz_drop_page(ctx, page);
    return out;
}

std::string Document::savePdfToMemory() const {
    fz_context* ctx = mu_.get();
    fz_buffer* buf = nullptr;
    fz_output* out = nullptr;
    std::string bytes;
    try {
        mu::call(ctx, [&] {
            pdf_document* pdf = pdf_document_from_fz_document(ctx, doc_);
            if (pdf == nullptr) {
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "not a PDF document");
            }
            buf = fz_new_buffer(ctx, 64 * 1024);
            out = fz_new_output_with_buffer(ctx, buf);
            pdf_write_options opts = pdf_default_write_options;
            pdf_write_document(ctx, pdf, out, &opts);
            fz_close_output(ctx, out);
        });
        unsigned char* data = nullptr;
        const size_t len = fz_buffer_storage(ctx, buf, &data);
        bytes.assign(reinterpret_cast<const char*>(data), len);
    } catch (...) {
        fz_drop_output(ctx, out);
        fz_drop_buffer(ctx, buf);
        throw;
    }
    fz_drop_output(ctx, out);
    fz_drop_buffer(ctx, buf);
    return bytes;
}

}  // namespace ot
