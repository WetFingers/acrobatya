// RAII-обёртки указателей MuPDF (эталон 4.1): владеют одной ссылкой и отдают её fz_drop_* в деструкторе.
// Объявляются СНАРУЖИ mu::call; внутри тела — только reset(…) (без временных объектов):
//
//     mu::PageHandle page(ctx);
//     mu::call(ctx, [&] { page.reset(fz_load_page(ctx, doc, 0)); box = fz_bound_page(ctx, page); });
//
// Drop-функции MuPDF не бросают, поэтому деструктор noexcept. Устройство (fz_device) перед drop нужно
// закрыть fz_close_device внутри mu::call — обёртка этого не делает (close может бросить).
#pragma once

#include <utility>

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::mu {

template <class T, void (*Drop)(fz_context*, T*)>
class Handle {
public:
    explicit Handle(fz_context* ctx, T* ptr = nullptr) noexcept : ctx_(ctx), ptr_(ptr) {}
    ~Handle() { reset(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : ctx_(other.ctx_), ptr_(std::exchange(other.ptr_, nullptr)) {}
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) {
            reset();
            ctx_ = other.ctx_;
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    // Принять владение ptr (прежний указатель отдаётся Drop).
    void reset(T* ptr = nullptr) noexcept {
        T* old = std::exchange(ptr_, ptr);
        if (old != nullptr) {
            Drop(ctx_, old);
        }
    }
    // Отдать владение вызывающему.
    [[nodiscard]] T* release() noexcept { return std::exchange(ptr_, nullptr); }

    T* get() const noexcept { return ptr_; }
    operator T*() const noexcept { return ptr_; }  // NOLINT(google-explicit-constructor): для вызовов fz_*
    T* operator->() const noexcept { return ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    fz_context* context() const noexcept { return ctx_; }

private:
    fz_context* ctx_;
    T* ptr_;
};

using DocumentHandle = Handle<fz_document, fz_drop_document>;
using PdfDocumentHandle = Handle<pdf_document, pdf_drop_document>;
using PdfObjHandle = Handle<pdf_obj, pdf_drop_obj>;
using PageHandle = Handle<fz_page, fz_drop_page>;
using PixmapHandle = Handle<fz_pixmap, fz_drop_pixmap>;
using DisplayListHandle = Handle<fz_display_list, fz_drop_display_list>;
using DeviceHandle = Handle<fz_device, fz_drop_device>;
using BufferHandle = Handle<fz_buffer, fz_drop_buffer>;
using StreamHandle = Handle<fz_stream, fz_drop_stream>;
using OutputHandle = Handle<fz_output, fz_drop_output>;
using DocumentWriterHandle = Handle<fz_document_writer, fz_drop_document_writer>;
using ImageHandle = Handle<fz_image, fz_drop_image>;
using StextPageHandle = Handle<fz_stext_page, fz_drop_stext_page>;
using FontHandle = Handle<fz_font, fz_drop_font>;
using ColorspaceHandle = Handle<fz_colorspace, fz_drop_colorspace>;
using PdfAnnotHandle = Handle<pdf_annot, pdf_drop_annot>;
using GraftMapHandle = Handle<pdf_graft_map, pdf_drop_graft_map>;

}  // namespace ot::mu
