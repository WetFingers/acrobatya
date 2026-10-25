#include "core/annot/align.h"

#include <algorithm>
#include <numeric>

namespace ot::annot {

namespace {

RectF moved(const RectF& r, float dx, float dy) { return {r.x0 + dx, r.y0 + dy, r.x1 + dx, r.y1 + dy}; }

}  // namespace

std::vector<RectF> alignRects(const std::vector<RectF>& rects, Align align) {
    if (rects.size() < 2) {
        return rects;
    }
    float left = rects.front().x0;
    float right = rects.front().x1;
    float top = rects.front().y0;
    float bottom = rects.front().y1;
    for (const RectF& r : rects) {
        left = std::min(left, r.x0);
        right = std::max(right, r.x1);
        top = std::min(top, r.y0);
        bottom = std::max(bottom, r.y1);
    }
    const float cx = (left + right) / 2;
    const float cy = (top + bottom) / 2;
    std::vector<RectF> out;
    out.reserve(rects.size());
    for (const RectF& r : rects) {
        switch (align) {
            case Align::Left:
                out.push_back(moved(r, left - r.x0, 0));
                break;
            case Align::Right:
                out.push_back(moved(r, right - r.x1, 0));
                break;
            case Align::HorizontalCenter:
                out.push_back(moved(r, cx - (r.x0 + r.x1) / 2, 0));
                break;
            case Align::Top:
                out.push_back(moved(r, 0, top - r.y0));
                break;
            case Align::Bottom:
                out.push_back(moved(r, 0, bottom - r.y1));
                break;
            case Align::VerticalCenter:
                out.push_back(moved(r, 0, cy - (r.y0 + r.y1) / 2));
                break;
        }
    }
    return out;
}

std::vector<RectF> distributeRects(const std::vector<RectF>& rects, Distribute distribute) {
    if (rects.size() < 3) {
        return rects;
    }
    const bool horizontal = distribute == Distribute::Horizontal;
    const auto start = [horizontal](const RectF& r) { return horizontal ? r.x0 : r.y0; };
    const auto size = [horizontal](const RectF& r) { return horizontal ? r.x1 - r.x0 : r.y1 - r.y0; };
    std::vector<std::size_t> order(rects.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return start(rects[a]) < start(rects[b]); });
    const RectF& first = rects[order.front()];
    const RectF& last = rects[order.back()];
    float total = 0;
    for (const RectF& r : rects) {
        total += size(r);
    }
    const float span = (horizontal ? last.x1 : last.y1) - start(first);
    const float gap = (span - total) / static_cast<float>(rects.size() - 1);
    std::vector<RectF> out = rects;
    float at = start(first);
    for (const std::size_t i : order) {
        const RectF& r = rects[i];
        const float d = at - start(r);
        out[i] = horizontal ? moved(r, d, 0) : moved(r, 0, d);
        at += size(r) + gap;
    }
    return out;
}

}  // namespace ot::annot
