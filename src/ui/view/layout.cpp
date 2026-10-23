#include "ui/view/layout.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ot::ui {

bool isPaged(PageMode mode) noexcept { return mode == PageMode::SinglePage || mode == PageMode::Facing; }

bool isFacing(PageMode mode) noexcept {
    return mode == PageMode::Facing || mode == PageMode::FacingContinuous;
}

std::optional<PdfPageLayout> pageModeFromPdf(const QString& name) {
    if (name == QLatin1String("SinglePage")) {
        return PdfPageLayout{PageMode::SinglePage, false};
    }
    if (name == QLatin1String("OneColumn")) {
        return PdfPageLayout{PageMode::Continuous, false};
    }
    if (name == QLatin1String("TwoColumnLeft")) {
        return PdfPageLayout{PageMode::FacingContinuous, false};
    }
    if (name == QLatin1String("TwoColumnRight")) {
        return PdfPageLayout{PageMode::FacingContinuous, true};
    }
    if (name == QLatin1String("TwoPageLeft")) {
        return PdfPageLayout{PageMode::Facing, false};
    }
    if (name == QLatin1String("TwoPageRight")) {
        return PdfPageLayout{PageMode::Facing, true};
    }
    return std::nullopt;
}

namespace {

QSizeF layoutDisplaySize(const QSizeF& points, const LayoutOptions& o) {
    const int r = ((o.rotation % 360) + 360) % 360;
    const bool swap = r == 90 || r == 270;
    const double w = (swap ? points.height() : points.width()) * o.zoom;
    const double h = (swap ? points.width() : points.height()) * o.zoom;
    return {std::max(w, 1.0), std::max(h, 1.0)};
}

}  // namespace

PageLayout layoutPages(const QVector<QSizeF>& pageSizes, const LayoutOptions& options) {
    PageLayout layout;
    layout.options_ = options;
    const int n = static_cast<int>(pageSizes.size());
    layout.slots_.resize(static_cast<std::size_t>(n));
    if (n == 0) {
        return layout;
    }
    const double gap = options.gap;
    const bool facing = isFacing(options.mode);
    const bool paged = isPaged(options.mode);

    std::vector<QSizeF> sizes(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        sizes[static_cast<std::size_t>(i)] = layoutDisplaySize(pageSizes[i], options);
    }

    // Ряды: по странице или парами (титул отдельно — первая одна, в правом столбце).
    if (!facing) {
        layout.rows_.reserve(static_cast<std::size_t>(n));
        for (int i = 0; i < n; ++i) {
            layout.rows_.push_back({i, 1, {}});
        }
    } else {
        int i = 0;
        if (options.coverPage) {
            layout.rows_.push_back({0, 1, {}});
            i = 1;
        }
        for (; i < n; i += 2) {
            layout.rows_.push_back({i, std::min(2, n - i), {}});
        }
    }

    // Столбцы: один (по центру) или левый и правый у корешка.
    double leftWidth = 0;
    double rightWidth = 0;
    for (const LayoutRow& row : layout.rows_) {
        for (int k = 0; k < row.count; ++k) {
            const int page = row.first + k;
            const bool right = facing && (k == 1 || (options.coverPage && page == 0));
            double& column = right ? rightWidth : leftWidth;
            column = std::max(column, sizes[static_cast<std::size_t>(page)].width());
        }
    }
    const double width = facing ? gap + leftWidth + gap + rightWidth + gap : gap + leftWidth + gap;
    const double spine = gap + leftWidth;  // правый край левого столбца

    double y = gap;
    for (int r = 0; r < static_cast<int>(layout.rows_.size()); ++r) {
        LayoutRow& row = layout.rows_[static_cast<std::size_t>(r)];
        double rowHeight = 0;
        for (int k = 0; k < row.count; ++k) {
            rowHeight = std::max(rowHeight, sizes[static_cast<std::size_t>(row.first + k)].height());
        }
        const double top = paged ? gap : y;
        QRectF bounds;
        for (int k = 0; k < row.count; ++k) {
            const int page = row.first + k;
            const QSizeF s = sizes[static_cast<std::size_t>(page)];
            double x = 0;
            int column = 0;
            if (!facing) {
                x = (width - s.width()) / 2;
            } else if (k == 1 || (options.coverPage && page == 0)) {
                x = spine + gap;
                column = 1;
            } else {
                x = spine - s.width();
            }
            PageSlot& slot = layout.slots_[static_cast<std::size_t>(page)];
            slot.rect = QRectF(x, top + (rowHeight - s.height()) / 2, s.width(), s.height());
            slot.row = r;
            slot.column = column;
            bounds = bounds.isNull() ? slot.rect : bounds.united(slot.rect);
        }
        row.rect = QRectF(bounds.left(), top, bounds.width(), rowHeight);
        y += rowHeight + gap;
    }
    layout.width_ = static_cast<float>(width);
    layout.height_ = static_cast<float>(y);
    return layout;
}

int PageLayout::rowOf(int page) const {
    if (page < 0 || page >= pageCount()) {
        return -1;
    }
    return slot(page).row;
}

QSizeF PageLayout::contentSize(int row) const {
    if (rows_.empty()) {
        return {0, 0};
    }
    if (!paged()) {
        return {width_, height_};
    }
    const int r = std::clamp(row, 0, rowCount() - 1);
    return {width_, rows_[static_cast<std::size_t>(r)].rect.height() + 2.0 * options_.gap};
}

std::vector<int> PageLayout::pagesIn(const QRectF& area, int row) const {
    std::vector<int> pages;
    if (rows_.empty()) {
        return pages;
    }
    auto collect = [&](const LayoutRow& r) {
        for (int k = 0; k < r.count; ++k) {
            if (slot(r.first + k).rect.intersects(area)) {
                pages.push_back(r.first + k);
            }
        }
    };
    if (paged()) {
        collect(rows_[static_cast<std::size_t>(std::clamp(row, 0, rowCount() - 1))]);
        return pages;
    }
    // Ряды отсортированы по y: первый ряд, чей низ ниже верха области, — двоичным поиском.
    auto it = std::lower_bound(rows_.begin(), rows_.end(), area.top(),
                               [](const LayoutRow& r, double top) { return r.rect.bottom() < top; });
    for (; it != rows_.end() && it->rect.top() <= area.bottom(); ++it) {
        collect(*it);
    }
    return pages;
}

int PageLayout::pageAt(QPointF point, int row) const {
    for (const int page : pagesIn(QRectF(point, QSizeF(0.001, 0.001)), row)) {
        if (slot(page).rect.contains(point)) {
            return page;
        }
    }
    return -1;
}

int PageLayout::nearestPage(QPointF point, int row) const {
    if (rows_.empty()) {
        return -1;
    }
    const auto distance = [&](int page) {
        const QRectF& r = slot(page).rect;
        const double dx = std::max({r.left() - point.x(), 0.0, point.x() - r.right()});
        const double dy = std::max({r.top() - point.y(), 0.0, point.y() - r.bottom()});
        return dx * dx + dy * dy;
    };
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    auto consider = [&](const LayoutRow& r) {
        for (int k = 0; k < r.count; ++k) {
            const double d = distance(r.first + k);
            if (d < bestDistance) {
                bestDistance = d;
                best = r.first + k;
            }
        }
    };
    if (paged()) {
        consider(rows_[static_cast<std::size_t>(std::clamp(row, 0, rowCount() - 1))]);
        return best;
    }
    auto it = std::lower_bound(rows_.begin(), rows_.end(), point.y(),
                               [](const LayoutRow& r, double y) { return r.rect.bottom() < y; });
    // Ряд под точкой (или следующий) и предыдущий — ближайшая страница среди них.
    if (it != rows_.end()) {
        consider(*it);
    }
    if (it != rows_.begin()) {
        consider(*std::prev(it));
    }
    return best;
}

}  // namespace ot::ui
