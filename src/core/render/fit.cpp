#include "core/render/fit.h"

#include <algorithm>
#include <cmath>

#include "core/render/tiles.h"

namespace ot {

namespace {

bool usable(float v) noexcept { return std::isfinite(v) && v > 0.0f; }

}  // namespace

float zoomToPercent(float zoom, float dpi) noexcept {
    return zoom * 72.0f / (usable(dpi) ? dpi : 96.0f) * 100.0f;
}

float percentToZoom(float percent, float dpi) noexcept {
    return percent / 100.0f * (usable(dpi) ? dpi : 96.0f) / 72.0f;
}

float clampZoom(float zoom, float dpi) noexcept {
    const float lo = percentToZoom(kMinZoomPercent, dpi);
    const float hi = percentToZoom(kMaxZoomPercent, dpi);
    if (!std::isfinite(zoom)) {
        return percentToZoom(100.0f, dpi);
    }
    return std::clamp(zoom, lo, hi);
}

float fitZoom(const RectF& pageRect, const Viewport& viewport, FitMode mode, int rotation,
              const RectF& content) {
    const float dpi = usable(viewport.dpi) ? viewport.dpi : 96.0f;
    if (mode == FitMode::ActualSize) {
        return percentToZoom(100.0f, dpi);
    }
    const bool sideways = normalizeRotation(rotation) % 180 != 0;
    const LayoutSize size = layoutPageSize(pageRect.width(), pageRect.height());
    const float pw = sideways ? size.height : size.width;
    const float ph = sideways ? size.width : size.height;
    if (!usable(viewport.width) || !usable(viewport.height)) {
        return percentToZoom(100.0f, dpi);
    }
    float zoom = 1.0f;
    switch (mode) {
        case FitMode::Page:
            zoom = std::min(viewport.width / pw, viewport.height / ph);
            break;
        case FitMode::Height:
            zoom = viewport.height / ph;
            break;
        case FitMode::Visible: {
            // Ширина содержимого в повороте вида (пункты раскладки).
            const float cw = (sideways ? content.height() : content.width()) * size.scale;
            zoom = !content.empty() && usable(cw) ? viewport.width / cw : viewport.width / pw;
            break;
        }
        case FitMode::Width:
        default:
            zoom = viewport.width / pw;
            break;
    }
    return clampZoom(zoom, dpi);
}

LayoutSize layoutPageSize(float width, float height) noexcept {
    LayoutSize out;
    if (!usable(width) || !usable(height)) {
        out.defaulted = true;
        return out;
    }
    const float longest = std::max(width, height);
    if (longest > kMaxPagePoints) {
        out.scale = kMaxPagePoints / longest;
        out.clamped = true;
    }
    out.width = std::max(1.0f, width * out.scale);
    out.height = std::max(1.0f, height * out.scale);
    return out;
}

}  // namespace ot
