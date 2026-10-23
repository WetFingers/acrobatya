#include "ui/view/link_layer.h"

#include <QFuture>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "services/link_ops.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

constexpr float kTabRowTolerance =
    2.0f;  // пт: ссылки с почти одной верхней кромкой — один ряд (слева направо)

}  // namespace

const std::vector<LinkLayer::LinkInfo>* LinkLayer::links(int page) {
    if (page < 0 || page >= view_.pageCount()) {
        return nullptr;
    }
    const auto it = links_.constFind(page);
    if (it != links_.constEnd()) {
        return &*it;
    }
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || requested_.contains(page)) {
        return nullptr;
    }
    requested_.insert(page);
    const quint64 generation = generation_.value(page);
    const quint64 epoch = epoch_;
    links::list(*session, page, TaskPriority::Normal)
        .then(&view_, [this, page, generation, epoch](QFuture<std::vector<LinkInfo>> done) {
            if (epoch != epoch_ || generation != generation_.value(page) || !requested_.remove(page)) {
                return;  // документ или страница изменились после запроса
            }
            try {
                links_.insert(page, done.result());
            } catch (const std::exception&) {
                links_.insert(page, {});  // страница без ссылок (повреждена) — не спрашивать снова
            }
            view_.viewport()->update();
        });
    return nullptr;
}

void LinkLayer::prefetch(const std::vector<int>& pages) {
    for (const int page : pages) {
        links(page);
    }
}

std::optional<LinkLayer::LinkInfo> LinkLayer::linkAt(QPointF viewportPoint) {
    const int page = view_.pageAtViewport(viewportPoint);
    const std::vector<LinkInfo>* list = links(page);
    if (list == nullptr || list->empty()) {
        return std::nullopt;
    }
    const QPointF pt = view_.viewportToPage(page, viewportPoint);
    const auto tolerance = static_cast<float>(kHitTolerancePx / std::max(0.01f, view_.zoom()));
    return links::linkAt(*list, PointF{static_cast<float>(pt.x()), static_cast<float>(pt.y())}, tolerance);
}

std::vector<int> LinkLayer::tabOrder(const std::vector<LinkInfo>& list) {
    std::vector<int> order(list.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        order[i] = static_cast<int>(i);
    }
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const RectF& ra = list[static_cast<std::size_t>(a)].rect;
        const RectF& rb = list[static_cast<std::size_t>(b)].rect;
        if (std::fabs(ra.y0 - rb.y0) > kTabRowTolerance) {
            return ra.y0 < rb.y0;
        }
        return ra.x0 < rb.x0;
    });
    return order;
}

bool LinkLayer::focusNext(bool forward) {
    std::vector<std::pair<int, int>> order;
    for (const int page : view_.visiblePages()) {
        if (const std::vector<LinkInfo>* list = links(page)) {
            for (const int i : tabOrder(*list)) {
                order.emplace_back(page, i);
            }
        }
    }
    const auto current = std::find(order.begin(), order.end(), std::make_pair(focusPage_, focusIndex_));
    std::ptrdiff_t next = -1;
    if (current == order.end()) {
        next = forward ? 0 : static_cast<std::ptrdiff_t>(order.size()) - 1;
    } else {
        next = (current - order.begin()) + (forward ? 1 : -1);
    }
    if (next < 0 || next >= static_cast<std::ptrdiff_t>(order.size())) {
        clearFocus();
        return false;
    }
    focusPage_ = order[static_cast<std::size_t>(next)].first;
    focusIndex_ = order[static_cast<std::size_t>(next)].second;
    view_.viewport()->update();
    return true;
}

const LinkLayer::LinkInfo* LinkLayer::focused() const {
    const auto it = links_.constFind(focusPage_);
    if (it == links_.constEnd() || focusIndex_ < 0 || focusIndex_ >= static_cast<int>(it->size())) {
        return nullptr;
    }
    return &(*it)[static_cast<std::size_t>(focusIndex_)];
}

void LinkLayer::clearFocus() {
    if (focusPage_ >= 0) {
        view_.viewport()->update();
    }
    focusPage_ = -1;
    focusIndex_ = -1;
}

void LinkLayer::documentChanged() {
    ++epoch_;
    links_.clear();
    requested_.clear();
    generation_.clear();
    clearFocus();
}

void LinkLayer::pageChanged(int page) {
    links_.remove(page);
    requested_.remove(page);
    ++generation_[page];
    if (page == focusPage_) {
        clearFocus();
    }
}

void LinkLayer::paintPage(QPainter& painter, const PageView& view, int page) {
    if (page != focusPage_) {
        return;
    }
    const LinkInfo* link = focused();
    if (link == nullptr) {
        return;
    }
    const double ring = Theme::sizes().focusRing;
    const QRectF r =
        view.pageTransform(page)
            .mapRect(QRectF(QPointF(link->rect.x0, link->rect.y0), QPointF(link->rect.x1, link->rect.y1)))
            .adjusted(-ring, -ring, ring, ring);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Theme::colors().accent.color(), ring));
    painter.drawRect(r);
}

}  // namespace ot::ui
