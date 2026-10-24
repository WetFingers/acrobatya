#include "core/ocr/is_scan.h"

#include <algorithm>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"

namespace ot::ocr {

namespace scan_device {

struct Device {
    fz_device super;
    fz_rect page;
    int text;
    int images;
    float best;  // наибольшая доля площади страницы под картинкой
};

void noteText(fz_context* /*ctx*/, Device* d, const fz_text* text) {
    for (const fz_text_span* s = text != nullptr ? text->head : nullptr; s != nullptr; s = s->next) {
        d->text += s->len;
    }
}

void fillText(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix /*ctm*/, fz_colorspace* /*cs*/,
              const float* /*color*/, float /*alpha*/, fz_color_params /*params*/) {
    noteText(ctx, reinterpret_cast<Device*>(dev), text);
}

void strokeText(fz_context* ctx, fz_device* dev, const fz_text* text, const fz_stroke_state* /*stroke*/,
                fz_matrix /*ctm*/, fz_colorspace* /*cs*/, const float* /*color*/, float /*alpha*/,
                fz_color_params /*params*/) {
    noteText(ctx, reinterpret_cast<Device*>(dev), text);
}

void clipText(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix /*ctm*/, fz_rect /*scissor*/) {
    noteText(ctx, reinterpret_cast<Device*>(dev), text);
}

void clipStrokeText(fz_context* ctx, fz_device* dev, const fz_text* text, const fz_stroke_state* /*stroke*/,
                    fz_matrix /*ctm*/, fz_rect /*scissor*/) {
    noteText(ctx, reinterpret_cast<Device*>(dev), text);
}

void ignoreText(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix /*ctm*/) {
    noteText(ctx, reinterpret_cast<Device*>(dev), text);
}

void noteImage(Device* d, fz_matrix ctm) {
    ++d->images;
    const fz_rect r = fz_intersect_rect(fz_transform_rect(fz_unit_rect, ctm), d->page);
    const float pageArea = (d->page.x1 - d->page.x0) * (d->page.y1 - d->page.y0);
    if (pageArea <= 0 || fz_is_empty_rect(r)) {
        return;
    }
    d->best = std::max(d->best, (r.x1 - r.x0) * (r.y1 - r.y0) / pageArea);
}

void fillImage(fz_context* /*ctx*/, fz_device* dev, fz_image* /*image*/, fz_matrix ctm, float /*alpha*/,
               fz_color_params /*params*/) {
    noteImage(reinterpret_cast<Device*>(dev), ctm);
}

void fillImageMask(fz_context* /*ctx*/, fz_device* dev, fz_image* /*image*/, fz_matrix ctm,
                   fz_colorspace* /*cs*/, const float* /*color*/, float /*alpha*/,
                   fz_color_params /*params*/) {
    noteImage(reinterpret_cast<Device*>(dev), ctm);
}

}  // namespace scan_device

ScanCheck checkScan(const Document& doc, int page) {
    ScanCheck out;
    fz_context* ctx = doc.context().get();
    fz_document* fz = doc.handle();
    mu::PageHandle p(ctx);
    mu::DeviceHandle dev(ctx);
    scan_device::Device* d = nullptr;
    int text = 0;
    int images = 0;
    float best = 0;
    try {
        mu::call(ctx, [&] {
            p.reset(fz_load_page(ctx, fz, page));
            d = fz_new_derived_device(ctx, scan_device::Device);
            dev.reset(&d->super);
            d->page = fz_bound_page(ctx, p);
            d->super.fill_text = scan_device::fillText;
            d->super.stroke_text = scan_device::strokeText;
            d->super.clip_text = scan_device::clipText;
            d->super.clip_stroke_text = scan_device::clipStrokeText;
            d->super.ignore_text = scan_device::ignoreText;
            d->super.fill_image = scan_device::fillImage;
            d->super.fill_image_mask = scan_device::fillImageMask;
            fz_run_page(ctx, p, dev, fz_identity, nullptr);
            fz_close_device(ctx, dev);
            text = d->text;
            images = d->images;
            best = d->best;
        });
    } catch (const std::exception&) {
        return out;
    }
    out.hasText = text > 0;
    out.images = images;
    out.coverage = best;
    out.scan = !out.hasText && best >= kScanCoverage;
    return out;
}

bool isScan(const Document& doc, int page) { return checkScan(doc, page).scan; }

}  // namespace ot::ocr
