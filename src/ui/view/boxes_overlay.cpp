#include "ui/view/boxes_overlay.h"

#include <QPen>

#include <exception>

#include "services/box_ops.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/view/page_view.h"

namespace ot::ui {

void BoxesOverlay::setShown(bool on) {
    if (shown_ == on) {
        return;
    }
    shown_ = on;
    view_.viewport()->update();
}

std::optional<pages::PageBoxes> BoxesOverlay::boxes(int page) const {
    const auto it = boxes_.constFind(page);
    if (it == boxes_.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

void BoxesOverlay::documentChanged() {
    boxes_.clear();
    requested_.clear();
    ++epoch_;
}

void BoxesOverlay::pageChanged(int page) {
    boxes_.remove(page);
    requested_.remove(page);
}

void BoxesOverlay::request(int page) {
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || requested_.contains(page)) {
        return;
    }
    requested_.insert(page);
    const quint64 epoch = epoch_;
    boxes::read(*session, page).then(&view_, [this, page, epoch](QFuture<pages::PageBoxes> done) {
        if (epoch != epoch_ || !requested_.contains(page)) {
            return;  // документ или страница изменились после запроса
        }
        try {
            boxes_.insert(page, done.result());
        } catch (const std::exception&) {
            // Повреждённая страница — без рамок, не спрашивать снова.
            boxes_.insert(page, pages::PageBoxes{});
        }
        view_.viewport()->update();
    });
}

void BoxesOverlay::paintPage(QPainter& painter, const PageView& view, int page) {
    if (!shown_) {
        return;
    }
    const auto it = boxes_.constFind(page);
    if (it == boxes_.constEnd()) {
        request(page);
        return;
    }
    const pages::PageBoxes& b = *it;
    const QTransform t = view.pageTransform(page);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const std::pair<bool, const RectF*> frames[] = {
        {b.hasBleed, &b.bleed}, {b.hasTrim, &b.trim}, {b.hasArt, &b.art}};
    for (std::size_t i = 0; i < std::size(frames); ++i) {
        if (!frames[i].first) {
            continue;
        }
        const RectF& r = *frames[i].second;
        QPen pen(kPageBoxColors[i].color());
        pen.setCosmetic(true);
        pen.setWidthF(Theme::sizes().pageBorder);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawPolygon(t.map(QPolygonF(QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)))));
    }
    painter.restore();
}

}  // namespace ot::ui
