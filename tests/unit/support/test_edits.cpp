#include "support/test_edits.h"

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

namespace ot::test {

using ot::Document;
using ot::Journal;

fz_context* ctxOf(const Document& doc) { return doc.context().get(); }
pdf_document* pdfOf(const Document& doc) { return pdf_document_from_fz_document(ctxOf(doc), doc.handle()); }

// Имя в каталоге (/Root): записать и прочитать (каталог — косвенный объект, правка идёт в журнал).
void putRootName(Document& doc, const char* key, const char* value) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    ot::mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_dict_puts_drop(ctx, root, key, pdf_new_name(ctx, value));
    });
}

std::string rootName(const Document& doc, const char* key) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    const char* name = "";
    ot::mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        name = pdf_to_name(ctx, pdf_dict_gets(ctx, root, key));
    });
    return name;
}

void setRotate(Document& doc, int page, int rotate) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    ot::mu::call(
        ctx, [&] { pdf_dict_put_int(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Rotate), rotate); });
}

int undoSteps(const Document& doc) {
    fz_context* ctx = ctxOf(doc);
    int steps = 0;
    ot::mu::call(ctx, [&] { (void)pdf_undoredo_state(ctx, pdfOf(doc), &steps); });
    return steps;
}

// Десять разных правок (каждая — своя операция): страница, каталог, поток, картинка, аннотация, страницы.
void tenEdits(Document& doc) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    {
        Journal::Operation op(doc, "Rotate page");
        setRotate(doc, 0, 90);
        op.invalidate(0);
        op.commit();
    }
    {
        Journal::Operation op(doc, "Set media box");
        ot::mu::call(ctx, [&] {
            pdf_dict_put_rect(ctx, pdf_lookup_page_obj(ctx, pdf, 1), PDF_NAME(MediaBox),
                              fz_make_rect(0, 0, 500, 700));
        });
        op.invalidate(1);
        op.commit();
    }
    {
        Journal::Operation op(doc, "Page mode");
        putRootName(doc, "PageMode", "UseOutlines");
        op.commit();
    }
    {
        Journal::Operation op(doc, "Page layout");
        putRootName(doc, "PageLayout", "TwoColumnLeft");
        op.commit();
    }
    {
        Journal::Operation op(doc, "Update contents");
        ot::mu::BufferHandle buf(ctx);
        ot::mu::call(ctx, [&] {
            buf.reset(fz_new_buffer_from_copied_data(
                ctx, reinterpret_cast<const unsigned char*>("0 0 1 rg 10 10 50 50 re f\n"), 26));
            pdf_obj* contents = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, 2), PDF_NAME(Contents));
            pdf_update_stream(ctx, pdf, contents, buf, 0);
        });
        op.invalidate(2);
        op.commit();
    }
    {
        Journal::Operation op(doc, "Add image");
        ot::mu::PixmapHandle pix(ctx);
        ot::mu::ImageHandle img(ctx);
        ot::mu::PdfObjHandle ref(ctx);
        ot::mu::call(ctx, [&] {
            pix.reset(fz_new_pixmap(ctx, fz_device_rgb(ctx), 8, 8, nullptr, 0));
            fz_clear_pixmap_with_value(ctx, pix, 0x80);
            img.reset(fz_new_image_from_pixmap(ctx, pix, nullptr));
            ref.reset(pdf_add_image(ctx, pdf, img));
            pdf_obj* res = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, 0), PDF_NAME(Resources));
            pdf_obj* xobj = pdf_dict_put_dict(ctx, res, PDF_NAME(XObject), 1);
            pdf_dict_puts(ctx, xobj, "Im1", ref);
        });
        op.invalidate(0);
        op.commit();
    }
    {
        Journal::Operation op(doc, "Add note");
        pdf_page* page = nullptr;
        ot::mu::call(ctx, [&] {
            page = pdf_load_page(ctx, pdf, 1);
            pdf_annot* annot = pdf_create_annot(ctx, page, PDF_ANNOT_TEXT);
            pdf_set_annot_contents(ctx, annot, "note");
            pdf_drop_annot(ctx, annot);
        });
        fz_drop_page(ctx, reinterpret_cast<fz_page*>(page));
        op.invalidate(1);
        op.commit();
    }
    {
        Journal::Operation op(doc, "Insert page");
        ot::mu::PdfObjHandle page(ctx);
        ot::mu::call(ctx, [&] {
            page.reset(pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 300, 300), 0, nullptr, nullptr));
            pdf_insert_page(ctx, pdf, -1, page);
        });
        op.invalidateAll();
        op.commit();
    }
    {
        Journal::Operation op(doc, "Delete page");
        ot::mu::call(ctx, [&] { pdf_delete_page(ctx, pdf, 0); });
        op.invalidateAll();
        op.commit();
    }
    {
        Journal::Operation op(doc, "Add object");
        ot::mu::PdfObjHandle dict(ctx);
        ot::mu::call(ctx, [&] {
            dict.reset(pdf_add_new_dict(ctx, pdf, 1));
            pdf_dict_puts_drop(ctx, dict, "Kind", pdf_new_name(ctx, "Test"));
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
            pdf_dict_puts(ctx, root, "OtTest", dict);
        });
        op.commit();
    }
}

void deletePage(Document& doc, int page) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    Journal::Operation op(doc, "Delete page");
    op.markHeavy();
    ot::mu::call(ctx, [&] { pdf_delete_page(ctx, pdf, page); });
    op.invalidateAll();
    op.commit();
}

void addNote(Document& doc, int page, const char* text) {
    fz_context* ctx = ctxOf(doc);
    pdf_document* pdf = pdfOf(doc);
    Journal::Operation op(doc, "Add note");
    pdf_page* loaded = nullptr;
    ot::mu::call(ctx, [&] {
        loaded = pdf_load_page(ctx, pdf, page);
        pdf_annot* annot = pdf_create_annot(ctx, loaded, PDF_ANNOT_TEXT);
        pdf_set_annot_rect(ctx, annot, fz_make_rect(40, 40, 60, 60));
        pdf_set_annot_contents(ctx, annot, text);
        pdf_update_annot(ctx, annot);
        pdf_drop_annot(ctx, annot);
    });
    fz_drop_page(ctx, reinterpret_cast<fz_page*>(loaded));
    op.invalidate(page);
    op.commit();
}

}  // namespace ot::test
