#include <QPolygonF>

#include <algorithm>

#include "ui/theme/theme.h"
#include "ui/view/overlay.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

const std::vector<QuadF>& noQuads() {
    static const std::vector<QuadF> empty;
    return empty;
}

const std::vector<RectF>& noRects() {
    static const std::vector<RectF> empty;
    return empty;
}

QPolygonF overlayQuadPolygon(const QTransform& t, const QuadF& q) {
    QPolygonF poly;
    poly << t.map(QPointF(q.ul.x, q.ul.y)) << t.map(QPointF(q.ur.x, q.ur.y)) << t.map(QPointF(q.lr.x, q.lr.y))
         << t.map(QPointF(q.ll.x, q.ll.y));
    return poly;
}

}  // namespace

// ---------------------------------------------------------------- OverlayStack

void OverlayStack::add(Overlay* overlay, int z) {
    if (overlay == nullptr || contains(overlay)) {
        return;
    }
    const auto at =
        std::upper_bound(layers_.begin(), layers_.end(), z,
                         [](int value, const std::pair<int, Overlay*>& l) { return value < l.first; });
    layers_.insert(at, {z, overlay});
}

void OverlayStack::remove(Overlay* overlay) {
    std::erase_if(layers_, [overlay](const std::pair<int, Overlay*>& l) { return l.second == overlay; });
}

bool OverlayStack::contains(const Overlay* overlay) const {
    return std::any_of(layers_.begin(), layers_.end(),
                       [overlay](const std::pair<int, Overlay*>& l) { return l.second == overlay; });
}

void OverlayStack::paintPage(QPainter& painter, const PageView& view, int page) const {
    for (const auto& [z, overlay] : layers_) {
        painter.save();
        overlay->paintPage(painter, view, page);
        painter.restore();
    }
}

void OverlayStack::paintViewport(QPainter& painter, const PageView& view) const {
    for (const auto& [z, overlay] : layers_) {
        painter.save();
        overlay->paintViewport(painter, view);
        painter.restore();
    }
}

// ---------------------------------------------------------------- QuadOverlay

void QuadOverlay::setQuads(int page, std::vector<QuadF> quads) {
    if (quads.empty()) {
        quads_.remove(page);
    } else {
        quads_.insert(page, std::move(quads));
    }
}

void QuadOverlay::clear() {
    quads_.clear();
    currentPage_ = -1;
    currentIndex_ = -1;
}

void QuadOverlay::clearPage(int page) { quads_.remove(page); }

const std::vector<QuadF>& QuadOverlay::quads(int page) const {
    const auto it = quads_.constFind(page);
    return it == quads_.constEnd() ? noQuads() : *it;
}

void QuadOverlay::setCurrent(int page, int index) {
    currentPage_ = page;
    currentIndex_ = index;
}

void QuadOverlay::paintPage(QPainter& painter, const PageView& view, int page) {
    const auto it = quads_.constFind(page);
    if (it == quads_.constEnd() || it->empty()) {
        return;
    }
    const QTransform t = view.pageTransform(page);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill_ ? fill_() : QColor());
    for (const QuadF& q : *it) {
        painter.drawPolygon(overlayQuadPolygon(t, q));
    }
    if (page == currentPage_ && stroke_ && currentIndex_ >= 0 &&
        currentIndex_ < static_cast<int>(it->size())) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(stroke_(), Theme::sizes().focusRing));
        painter.drawPolygon(overlayQuadPolygon(t, (*it)[static_cast<std::size_t>(currentIndex_)]));
    }
}

// ---------------------------------------------------------------- RectOverlay

void RectOverlay::setRects(int page, std::vector<RectF> rects) {
    if (rects.empty()) {
        rects_.remove(page);
    } else {
        rects_.insert(page, std::move(rects));
    }
}

void RectOverlay::clear() { rects_.clear(); }

const std::vector<RectF>& RectOverlay::rects(int page) const {
    const auto it = rects_.constFind(page);
    return it == rects_.constEnd() ? noRects() : *it;
}

void RectOverlay::paintPage(QPainter& painter, const PageView& view, int page) {
    const auto it = rects_.constFind(page);
    if (it == rects_.constEnd() || it->empty()) {
        return;
    }
    const QTransform t = view.pageTransform(page);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    QPen pen(stroke_ ? stroke_() : QColor(), Theme::sizes().pageBorder);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (const RectF& r : *it) {
        painter.drawRect(t.mapRect(QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1))));
    }
}

}  // namespace ot::ui
