#include "support/pdf_objects.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

namespace ot::test {

namespace {

std::string bufferText(fz_context* ctx, fz_buffer* buf) {
    if (buf == nullptr) {
        return {};
    }
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

}  // namespace

PdfObjects pdfObjects(fz_context* ctx, pdf_document* pdf) {
    PdfObjects out;
    int len = 0;
    // Запись 0 — ссылки трейлера /Root, /Info, /Encrypt (без /ID, /Size и полей таблицы xref).
    mu::BufferHandle trailer(ctx);
    mu::OutputHandle to(ctx);
    mu::call(ctx, [&] {
        len = pdf_xref_len(ctx, pdf);
        trailer.reset(fz_new_buffer(ctx, 64));
        to.reset(fz_new_output_with_buffer(ctx, trailer));
        pdf_obj* t = pdf_trailer(ctx, pdf);
        fz_write_string(ctx, to, "Root=");
        pdf_print_obj(ctx, to, pdf_dict_get(ctx, t, PDF_NAME(Root)), 1, 1);
        fz_write_string(ctx, to, " Info=");
        pdf_print_obj(ctx, to, pdf_dict_get(ctx, t, PDF_NAME(Info)), 1, 1);
        fz_write_string(ctx, to, " Encrypt=");
        pdf_print_obj(ctx, to, pdf_dict_get(ctx, t, PDF_NAME(Encrypt)), 1, 1);
        fz_close_output(ctx, to);
    });
    out.emplace(0, bufferText(ctx, trailer));
    for (int num = 1; num < len; ++num) {
        mu::PdfObjHandle obj(ctx);
        mu::BufferHandle text(ctx);
        mu::BufferHandle raw(ctx);
        mu::OutputHandle o(ctx);
        int exists = 0;
        int isNull = 0;
        mu::call(ctx, [&] {
            exists = pdf_object_exists(ctx, pdf, num);
            if (exists) {
                obj.reset(pdf_load_object(ctx, pdf, num));
                isNull = pdf_is_null(ctx, obj);
                text.reset(fz_new_buffer(ctx, 256));
                o.reset(fz_new_output_with_buffer(ctx, text));
                pdf_print_obj(ctx, o, obj, 1, 1);
                fz_close_output(ctx, o);
                if (pdf_obj_num_is_stream(ctx, pdf, num)) {
                    raw.reset(pdf_load_raw_stream_number(ctx, pdf, num));
                }
            }
        });
        if (exists == 0 || isNull != 0) {
            continue;
        }
        std::string value = bufferText(ctx, text);
        if (raw) {
            value += "\nstream:" + bufferText(ctx, raw);
        }
        out.emplace(num, std::move(value));
    }
    return out;
}

PdfObjects pdfObjects(const Document& doc) {
    fz_context* ctx = doc.context().get();
    return pdfObjects(ctx, pdf_document_from_fz_document(ctx, doc.handle()));
}

PdfObjects pdfObjectsOfBytes(const std::string& bytes) {
    const auto doc = Document::openOrThrowMemory(bytes);
    return pdfObjects(*doc);
}

PdfObjects pdfObjectsAfterFullWrite(Document& doc) { return pdfObjectsOfBytes(doc.savePdfToMemory()); }

std::string firstDifference(const PdfObjects& a, const PdfObjects& b) {
    for (const auto& [num, value] : a) {
        const auto it = b.find(num);
        if (it == b.end()) {
            return std::to_string(num) + ": only in first: " + value.substr(0, 200);
        }
        if (it->second != value) {
            return std::to_string(num) + ": " + value.substr(0, 200) + " != " + it->second.substr(0, 200);
        }
    }
    for (const auto& [num, value] : b) {
        if (a.find(num) == a.end()) {
            return std::to_string(num) + ": only in second: " + value.substr(0, 200);
        }
    }
    return {};
}

}  // namespace ot::test
