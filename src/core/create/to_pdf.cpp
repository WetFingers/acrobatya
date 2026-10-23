#include "core/create/to_pdf.h"

#include <stdexcept>
#include <system_error>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"

namespace ot {

std::string renderToPdf(const Document& doc, const std::function<bool()>& cancelled) {
    fz_context* ctx = doc.context().get();
    fz_document* source = doc.handle();
    mu::BufferHandle buffer(ctx);
    mu::DocumentWriterHandle writer(ctx);
    mu::call(ctx, [&] {
        buffer.reset(fz_new_buffer(ctx, 64 * 1024));
        writer.reset(fz_new_document_writer_with_buffer(ctx, buffer, "pdf", ""));
    });
    const int pages = doc.pageCount();
    for (int i = 0; i < pages; ++i) {
        if (cancelled && cancelled()) {
            throw MuError(FZ_ERROR_ABORT, "save as PDF aborted");
        }
        mu::PageHandle page(ctx);
        fz_rect box{};
        fz_device* dev = nullptr;
        // Страница как есть: рамка с поворотом (fz_bound_page) — MediaBox новой страницы, содержимое — без
        // преобразования (как mutool convert).
        mu::call(ctx, [&] {
            page.reset(fz_load_page(ctx, source, i));
            box = fz_bound_page(ctx, page);
            dev = fz_begin_page(ctx, writer, box);
            fz_run_page(ctx, page, dev, fz_identity, nullptr);
            fz_end_page(ctx, writer);
        });
    }
    mu::call(ctx, [&] { fz_close_document_writer(ctx, writer); });
    return std::string(reinterpret_cast<const char*>(buffer->data), buffer->len);
}

void writeAsPdf(const Document& doc, const std::filesystem::path& out,
                const std::function<bool()>& cancelled) {
    const std::string bytes = renderToPdf(doc, cancelled);
    std::filesystem::path temp = out;
    temp += L".part";
    paths::writeFile(temp, bytes);
    std::error_code ec;
    std::filesystem::rename(temp, out, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        throw std::runtime_error("save as PDF: cannot replace " + paths::toUtf8(out));
    }
}

}  // namespace ot
