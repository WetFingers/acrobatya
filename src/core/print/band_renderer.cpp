#include "core/print/band_renderer.h"

#include <algorithm>
#include <cmath>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/render/display_cache.h"

namespace ot::print {

namespace {

constexpr const char* kUsage = "Print";

int alignedStride(int width, int bytesPerPixel) noexcept { return (width * bytesPerPixel + 3) & ~3; }

}  // namespace

std::shared_ptr<const DisplayList> buildPrintList(const Document& doc, int page, PrintContent content,
                                                  fz_cookie* cookie) {
    fz_context* ctx = doc.context().get();
    mu::PageHandle p(ctx);
    mu::DisplayListHandle list(ctx);
    mu::DeviceHandle dev(ctx);
    fz_rect bounds{};
    int overprint = 0;
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        bounds = fz_bound_page(ctx, p);
        overprint = fz_page_uses_overprint(ctx, p);
        list.reset(fz_new_display_list(ctx, bounds));
        dev.reset(fz_new_list_device(ctx, list));
        if (pdf_page* pp = pdf_page_from_fz_page(ctx, p); pp != nullptr) {
            if (content != PrintContent::FormFieldsOnly) {
                pdf_run_page_contents_with_usage(ctx, pp, dev, fz_identity, kUsage, cookie);
            }
            if (content == PrintContent::DocumentAndMarkups) {
                pdf_run_page_annots_with_usage(ctx, pp, dev, fz_identity, kUsage, cookie);
            }
            pdf_run_page_widgets_with_usage(ctx, pp, dev, fz_identity, kUsage, cookie);
        } else if (content != PrintContent::FormFieldsOnly) {
            fz_run_page(ctx, p, dev, fz_identity, cookie);
        }
        fz_close_device(ctx, dev);
    });
    if (cookie != nullptr && cookie->abort != 0) {
        throw MuError(FZ_ERROR_ABORT, "print list aborted");
    }
    dev.reset();
    return std::make_shared<const DisplayList>(doc.context().clone(), list.release(),
                                               RectF{bounds.x0, bounds.y0, bounds.x1, bounds.y1}, page,
                                               content == PrintContent::DocumentAndMarkups, overprint != 0);
}

IRect deviceRect(const RectF& sheetRect, float dpi) noexcept {
    const float k = dpi / 72.0f;
    return {static_cast<int>(std::lround(sheetRect.x0 * k)), static_cast<int>(std::lround(sheetRect.y0 * k)),
            static_cast<int>(std::lround(sheetRect.x1 * k)), static_cast<int>(std::lround(sheetRect.y1 * k))};
}

std::vector<IRect> splitBands(const IRect& area, int bytesPerPixel, std::size_t maxBytes) {
    std::vector<IRect> out;
    if (area.empty() || bytesPerPixel <= 0) {
        return out;
    }
    const auto stride = static_cast<std::size_t>(alignedStride(area.width(), bytesPerPixel));
    const int height = area.height();
    const int rowsPerBand =
        static_cast<int>(std::clamp<std::size_t>(maxBytes / stride, 1, static_cast<std::size_t>(height)));
    const int count = (height + rowsPerBand - 1) / rowsPerBand;
    const int base = height / count;
    const int extra = height % count;
    int y = area.y0;
    for (int i = 0; i < count; ++i) {
        const int h = base + (i < extra ? 1 : 0);
        out.push_back({area.x0, y, area.x1, y + h});
        y += h;
    }
    return out;
}

void renderPlacement(fz_context* ctx, const DisplayList& list, const Placement& placement,
                     const BandOptions& options, fz_cookie* cookie,
                     const std::function<void(const Band&)>& sink) {
    if (placement.page < 0 || placement.clip.empty() || options.dpi <= 0) {
        return;
    }
    const IRect area = deviceRect(placement.clip, options.dpi);
    if (area.empty()) {
        return;
    }
    const PixelFormat format = options.grayscale ? PixelFormat::Gray8 : PixelFormat::Rgb24;
    const int bpp = bytesPerPixel(format);
    const std::vector<IRect> bands = splitBands(area, bpp, options.maxBandBytes);
    const int stride = alignedStride(area.width(), bpp);
    int tallest = 0;
    for (const IRect& b : bands) {
        tallest = std::max(tallest, b.height());
    }
    std::vector<std::uint8_t> buffer(static_cast<std::size_t>(stride) * static_cast<std::size_t>(tallest));
    const Matrix matrix = concat(placementMatrix(placement, list.bounds()), Matrix::dpi(options.dpi));
    const int count = static_cast<int>(bands.size());
    for (int i = 0; i < count; ++i) {
        if (cookie != nullptr && cookie->abort != 0) {
            throw MuError(FZ_ERROR_ABORT, "print aborted");
        }
        const IRect& b = bands[static_cast<std::size_t>(i)];
        renderDisplayList(ctx, list, matrix, b, buffer.data(), stride, format, cookie);
        sink(Band{b, format, buffer.data(), stride, i, count});
    }
}

}  // namespace ot::print
