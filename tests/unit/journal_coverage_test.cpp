// unit.journal_coverage.* — покрытие журнала MuPDF до редактора текста (этап 3) и OCR-слоя (этап 8) (эталон
// 4.3, риск 5): правка потока pdf_update_stream → pdf_undo → исходные байты потока (сырые и декодированные) и
// словарь (/Length, /Filter); новые объекты pdf_add_image / pdf_add_object / pdf_add_stream → pdf_undo →
// объекта нет (свободная запись xref), объекты после полной записи равны исходнику; redo возвращает.
// Трейлер в журнал не входит (parent_num 0): /Info, созданный правкой, после undo остаётся висячей ссылкой.
// Итог проверки — решение в docs/progress.md (сессия 1.3).
#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/features.h"
#include "support/pdf_objects.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

using ot::Document;
using ot::Journal;

namespace {

fz_context* ctxOf(const Document& doc) { return doc.context().get(); }
pdf_document* pdfOf(const Document& doc) { return pdf_document_from_fz_document(ctxOf(doc), doc.handle()); }

std::string text(fz_context* ctx, fz_buffer* buf) {
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

// Байты PDF с потоками содержимого: compressed — /FlateDecode (полная запись do_compress).
std::string pdfBytes(bool compressed) {
    std::string bytes = ot::test::makePdf(ot::MuContext::global(), 2);
    if (compressed) {
        auto plain = Document::openOrThrowMemory(bytes);
        fz_context* ctx = ctxOf(*plain);
        ot::mu::BufferHandle buf(ctx);
        ot::mu::OutputHandle out(ctx);
        ot::mu::call(ctx, [&] {
            buf.reset(fz_new_buffer(ctx, 4096));
            out.reset(fz_new_output_with_buffer(ctx, buf));
            pdf_write_options opts = pdf_default_write_options;
            opts.do_compress = 1;
            pdf_write_document(ctx, pdfOf(*plain), out, &opts);
            fz_close_output(ctx, out);
        });
        bytes = text(ctx, buf);
    }
    return bytes;
}

std::unique_ptr<Document> openPdf(bool compressed) {
    return Document::openOrThrowMemory(pdfBytes(compressed));
}

// Объекты полной записи исходника (отдельная копия: запись сбрасывает историю документа).
ot::test::PdfObjects writtenSource(const std::string& bytes) {
    auto copy = Document::openOrThrowMemory(bytes);
    return ot::test::pdfObjectsAfterFullWrite(*copy);
}

struct StreamState {
    std::string dict;
    std::string raw;
    std::string decoded;
};

int contentsNum(const Document& doc, int page) {
    fz_context* ctx = ctxOf(doc);
    int num = 0;
    ot::mu::call(ctx, [&] {
        num = pdf_to_num(ctx,
                         pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdfOf(doc), page), PDF_NAME(Contents)));
    });
    return num;
}

StreamState streamState(const Document& doc, int num) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    ot::mu::BufferHandle dict(ctx);
    ot::mu::BufferHandle raw(ctx);
    ot::mu::BufferHandle decoded(ctx);
    ot::mu::OutputHandle out(ctx);
    ot::mu::PdfObjHandle obj(ctx);
    ot::mu::call(ctx, [&] {
        obj.reset(pdf_load_object(ctx, pdf, num));
        dict.reset(fz_new_buffer(ctx, 64));
        out.reset(fz_new_output_with_buffer(ctx, dict));
        pdf_print_obj(ctx, out, obj, 1, 1);
        fz_close_output(ctx, out);
        raw.reset(pdf_load_raw_stream_number(ctx, pdf, num));
        decoded.reset(pdf_load_stream_number(ctx, pdf, num));
    });
    return {text(ctx, dict), text(ctx, raw), text(ctx, decoded)};
}

void updateStream(Document& doc, int num, const char* data, int compressed) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    ot::mu::BufferHandle buf(ctx);
    ot::mu::PdfObjHandle obj(ctx);
    ot::mu::call(ctx, [&] {
        buf.reset(
            fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(data), strlen(data)));
        obj.reset(pdf_new_indirect(ctx, pdf, num, 0));
        pdf_update_stream(ctx, pdf, obj, buf, compressed);
    });
}

bool objectExists(const Document& doc, int num) {
    fz_context* ctx = ctxOf(doc);
    int exists = 0;
    ot::mu::call(ctx, [&] { exists = pdf_object_exists(ctx, pdfOf(doc), num); });
    return exists != 0;
}

int addImage(Document& doc) {
    fz_context* ctx = ctxOf(doc);
    ot::mu::PixmapHandle pix(ctx);
    ot::mu::ImageHandle img(ctx);
    ot::mu::PdfObjHandle ref(ctx);
    int num = 0;
    ot::mu::call(ctx, [&] {
        pix.reset(fz_new_pixmap(ctx, fz_device_rgb(ctx), 16, 16, nullptr, 0));
        fz_clear_pixmap_with_value(ctx, pix, 0x33);
        img.reset(fz_new_image_from_pixmap(ctx, pix, nullptr));
        ref.reset(pdf_add_image(ctx, pdfOf(doc), img));
        num = pdf_to_num(ctx, ref);
    });
    return num;
}

}  // namespace

TEST(journal_coverage, update_stream_is_undone_to_original_bytes) {
    OT_FEATURE("edit-13");
    for (const bool compressed : {false, true}) {
        SCOPED_TRACE(compressed ? "FlateDecode" : "plain");
        auto doc = openPdf(compressed);
        const int num = contentsNum(*doc, 0);
        ASSERT_GT(num, 0);
        const StreamState before = streamState(*doc, num);
        EXPECT_EQ(before.dict.find("/FlateDecode") != std::string::npos, compressed);
        {
            Journal::Operation op(*doc, "Rewrite contents");
            updateStream(*doc, num, "1 0 0 rg 0 0 10 10 re f\n", 0);
            op.invalidate(0);
            op.commit();
        }
        const StreamState edited = streamState(*doc, num);
        EXPECT_EQ(edited.decoded, "1 0 0 rg 0 0 10 10 re f\n");
        EXPECT_EQ(edited.dict.find("/Filter"), std::string::npos);
        (void)doc->undoStack().undo();
        const StreamState undone = streamState(*doc, num);
        EXPECT_EQ(undone.raw, before.raw);
        EXPECT_EQ(undone.decoded, before.decoded);
        EXPECT_EQ(undone.dict, before.dict);
        (void)doc->undoStack().redo();
        EXPECT_EQ(streamState(*doc, num).decoded, edited.decoded);
        (void)doc->undoStack().undo();
        EXPECT_EQ(streamState(*doc, num).raw, before.raw);
    }
}

TEST(journal_coverage, two_stream_updates_in_one_operation_undo_to_first_bytes) {
    auto doc = openPdf(false);
    const int num = contentsNum(*doc, 1);
    const StreamState before = streamState(*doc, num);
    {
        Journal::Operation op(*doc, "Rewrite twice");
        updateStream(*doc, num, "0 g 0 0 1 1 re f\n", 0);
        updateStream(*doc, num, "0 g 0 0 2 2 re f\n", 0);
        op.commit();
    }
    EXPECT_EQ(streamState(*doc, num).decoded, "0 g 0 0 2 2 re f\n");
    (void)doc->undoStack().undo();
    EXPECT_EQ(streamState(*doc, num).raw, before.raw);
    EXPECT_EQ(streamState(*doc, num).dict, before.dict);
}

TEST(journal_coverage, added_image_object_disappears_on_undo) {
    const std::string bytes = pdfBytes(false);
    auto doc = Document::openOrThrowMemory(bytes);
    const ot::test::PdfObjects original = ot::test::pdfObjects(*doc);
    int num = 0;
    {
        Journal::Operation op(*doc, "Add image");
        num = addImage(*doc);
        op.commit();
    }
    ASSERT_GT(num, 0);
    ASSERT_TRUE(objectExists(*doc, num));
    const StreamState image = streamState(*doc, num);
    EXPECT_NE(image.dict.find("/Subtype/Image"), std::string::npos) << image.dict;
    EXPECT_EQ(image.decoded.size(), 16u * 16u * 3u);
    (void)doc->undoStack().undo();
    EXPECT_FALSE(objectExists(*doc, num));
    EXPECT_EQ(ot::test::firstDifference(original, ot::test::pdfObjects(*doc)), "");
    (void)doc->undoStack().redo();
    ASSERT_TRUE(objectExists(*doc, num));
    EXPECT_EQ(streamState(*doc, num).decoded, image.decoded);
    (void)doc->undoStack().undo();
    EXPECT_EQ(ot::test::firstDifference(writtenSource(bytes), ot::test::pdfObjectsAfterFullWrite(*doc)), "");
}

TEST(journal_coverage, added_objects_and_streams_disappear_on_undo) {
    const std::string bytes = pdfBytes(true);
    auto doc = Document::openOrThrowMemory(bytes);
    fz_context* ctx = ctxOf(*doc);
    pdf_document* pdf = pdfOf(*doc);
    int dictNum = 0;
    int streamNum = 0;
    {
        Journal::Operation op(*doc, "Add objects");
        ot::mu::PdfObjHandle dict(ctx);
        ot::mu::PdfObjHandle stream(ctx);
        ot::mu::BufferHandle buf(ctx);
        ot::mu::call(ctx, [&] {
            dict.reset(pdf_add_new_dict(ctx, pdf, 2));
            pdf_dict_puts_drop(ctx, dict, "Kind", pdf_new_name(ctx, "OcrLayer"));
            buf.reset(
                fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>("BT ET\n"), 6));
            stream.reset(pdf_add_stream(ctx, pdf, buf, nullptr, 0));
            pdf_dict_puts(ctx, dict, "Data", stream);
            pdf_dict_puts(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root)), "OtLayer", dict);
            dictNum = pdf_to_num(ctx, dict);
            streamNum = pdf_to_num(ctx, stream);
        });
        op.commit();
    }
    ASSERT_TRUE(objectExists(*doc, dictNum));
    ASSERT_TRUE(objectExists(*doc, streamNum));
    (void)doc->undoStack().undo();
    EXPECT_FALSE(objectExists(*doc, dictNum));
    EXPECT_FALSE(objectExists(*doc, streamNum));
    EXPECT_EQ(ot::test::firstDifference(writtenSource(bytes), ot::test::pdfObjectsAfterFullWrite(*doc)), "");
}

TEST(journal_coverage, trailer_is_not_journaled_info_created_by_edit_dangles_after_undo) {
    auto doc = openPdf(false);
    fz_context* ctx = ctxOf(*doc);
    pdf_document* pdf = pdfOf(*doc);
    int hadInfo = 1;
    ot::mu::call(ctx, [&] { hadInfo = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Info)) != nullptr; });
    ASSERT_EQ(hadInfo, 0);
    {
        Journal::Operation op(*doc, "Set title");
        ot::mu::call(ctx, [&] { fz_set_metadata(ctx, doc->handle(), FZ_META_INFO_TITLE, "Title"); });
        op.commit();
    }
    EXPECT_EQ(doc->properties().title, "Title");
    (void)doc->undoStack().undo();
    // Словарь /Info (новый объект) отменён, а ключ трейлера остался: ссылка на свободный объект читается
    // как null. Saver убирает такую ссылку перед записью (пункт «Сохранение» сессии 1.3).
    int infoKey = 0;
    int infoIsDict = 1;
    ot::mu::call(ctx, [&] {
        pdf_obj* info = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Info));
        infoKey = info != nullptr;
        infoIsDict = pdf_is_dict(ctx, info);
    });
    EXPECT_EQ(infoKey, 1);
    EXPECT_EQ(infoIsDict, 0);
    EXPECT_EQ(doc->properties().title, "");
}
