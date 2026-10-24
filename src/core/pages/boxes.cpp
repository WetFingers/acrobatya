#include "core/pages/boxes.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::pages {

namespace {

RectF toRect(const fz_rect& r) { return {r.x0, r.y0, r.x1, r.y1}; }

}  // namespace

PageBoxes pageBoxes(Document& doc, int page) {
    fz_context* ctx = doc.context().get();
    mu::PageHandle p(ctx);
    PageBoxes out;
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        out.media = toRect(fz_bound_page_box(ctx, p, FZ_MEDIA_BOX));
        out.crop = toRect(fz_bound_page_box(ctx, p, FZ_CROP_BOX));
        out.bleed = toRect(fz_bound_page_box(ctx, p, FZ_BLEED_BOX));
        out.trim = toRect(fz_bound_page_box(ctx, p, FZ_TRIM_BOX));
        out.art = toRect(fz_bound_page_box(ctx, p, FZ_ART_BOX));
        if (pdf_page* pp = pdf_page_from_fz_page(ctx, p)) {
            out.hasBleed = pdf_dict_get_inheritable(ctx, pp->obj, PDF_NAME(BleedBox)) != nullptr;
            out.hasTrim = pdf_dict_get_inheritable(ctx, pp->obj, PDF_NAME(TrimBox)) != nullptr;
            out.hasArt = pdf_dict_get_inheritable(ctx, pp->obj, PDF_NAME(ArtBox)) != nullptr;
        }
    });
    return out;
}

}  // namespace ot::pages
