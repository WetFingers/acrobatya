#include "ui/view/page_view.h"

#include <QKeyEvent>
#include <QPainter>
#include <QScrollBar>
#include <QStyle>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

#include "core/render/tiles.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

constexpr int kLineStep = 40;            // логических px на строку прокрутки (↑/↓)
constexpr double kWheelZoomBase = 1.25;  // Ctrl+колесо: × 1,25 на щелчок
constexpr int kContinuousPushMs = 600;   // непрерывный зум — одна запись истории на жест
constexpr double kMinAutoSpeed = 5.0;    // автопрокрутка: px/с
constexpr double kMaxAutoSpeed = 4000.0;
constexpr double kAutoSpeedStep = 1.25;

// Повёрнутый размер страницы в пунктах.
QSizeF viewRotatedSize(const QSizeF& points, int rotation) {
    const int r = normalizeRotation(rotation);
    return (r == 90 || r == 270) ? points.transposed() : points;
}

}  // namespace

const QVector<float>& PageView::zoomPresets() {
    static const QVector<float> presets = {8.33f,  12.5f,   25.0f,   33.33f,  50.0f,   66.67f, 75.0f,
                                           100.0f, 125.0f,  150.0f,  200.0f,  300.0f,  400.0f, 600.0f,
                                           800.0f, 1200.0f, 1600.0f, 2400.0f, 3200.0f, 6400.0f};
    return presets;
}

PageView::PageView(QWidget* parent) : QAbstractScrollArea(parent) {
    setFrameShape(QFrame::NoFrame);
    setFocusPolicy(Qt::StrongFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent);
    viewport()->setMouseTracking(true);
    options_.gap = static_cast<float>(Theme::sizes().pageGap);
    autoScroll_.setInterval(16);
    connect(&autoScroll_, &QTimer::timeout, this, &PageView::autoScrollTick);
    connect(&Theme::instance(), &Theme::themeChanged, this, [this] {
        const auto gap = static_cast<float>(Theme::sizes().pageGap);
        if (gap != options_.gap) {
            const Anchor a = anchorAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
            options_.gap = gap;
            relayout();
            restoreAnchor(a);
        }
        viewport()->update();
    });
}

PageView::~PageView() = default;

void PageView::setDocument(DocumentSession* session, RenderPool* pool, TileCache* cache) {
    if (session_ != nullptr) {
        disconnect(session_, nullptr, this, nullptr);
    }
    session_ = session;
    pool_ = pool;
    cache_ = cache;
    history_.clear();
    fit_.reset();
    currentPage_ = 0;
    row_ = 0;
    if (session_ != nullptr) {
        connect(session_, &DocumentSession::opened, this, [this] { reloadSizes(); });
        connect(session_, &DocumentSession::closed, this, [this] { reloadSizes(); });
        connect(session_, &DocumentSession::documentChanged, this, [this] {
            if (session_ != nullptr && session_->summary().pageSizes != sizes_) {
                reloadSizes();
            }
            viewport()->update();
        });
        connect(session_, &DocumentSession::pageChanged, this, [this] { viewport()->update(); });
        const OpenSummary s = session_->summary();
        if (respectLayout_) {
            if (const auto l = pageModeFromPdf(s.pageLayout)) {
                options_.mode = l->mode;
                options_.coverPage = l->coverPage;
            }
        }
    }
    reloadSizes();
    setScrollPosition({0, 0});
    emit historyChanged();
}

void PageView::reloadSizes() {
    const QVector<QSizeF> sizes = session_ != nullptr ? session_->summary().pageSizes : QVector<QSizeF>{};
    if (session_ == nullptr && sizes_.isEmpty()) {
        relayout();
        return;
    }
    const Anchor a = anchorAt(QPointF(viewport()->width() / 2.0, 0));
    sizes_ = sizes;
    relayout();
    if (pageCount() == 0) {
        currentPage_ = 0;
        viewport()->update();
        return;
    }
    if (a.page >= 0 && a.page < pageCount()) {
        restoreAnchor(a);
    }
    setCurrentPage(std::clamp(currentPage_, 0, pageCount() - 1));
    viewport()->update();
}

void PageView::setPageSizes(const QVector<QSizeF>& pointSizes) {
    if (session_ != nullptr) {
        disconnect(session_, nullptr, this, nullptr);
        session_ = nullptr;
    }
    sizes_ = pointSizes;
    history_.clear();
    fit_.reset();
    row_ = 0;
    currentPage_ = 0;
    relayout();
    setScrollPosition({0, 0});
    viewport()->update();
}

QSizeF PageView::pageSizePoints(int page) const {
    if (page < 0 || page >= pageCount()) {
        return {};
    }
    return sizes_[page];
}

// ---------------------------------------------------------------- раскладка

void PageView::relayout() {
    layout_ = layoutPages(sizes_, options_);
    row_ = layout_.rowCount() == 0 ? 0 : std::clamp(row_, 0, layout_.rowCount() - 1);
    updateScrollBars();
    emit layoutChanged();
}

QSizeF PageView::contentSize() const { return layout_.contentSize(row_); }

void PageView::updateScrollBars() {
    const QSizeF content = contentSize();
    const QSize view = viewport()->size();
    const auto range = [](double content, int view) {
        return std::max(0, static_cast<int>(std::ceil(content - view - 0.01)));
    };
    horizontalScrollBar()->setRange(0, range(content.width(), view.width()));
    horizontalScrollBar()->setPageStep(std::max(1, view.width()));
    horizontalScrollBar()->setSingleStep(kLineStep);
    verticalScrollBar()->setRange(0, range(content.height(), view.height()));
    verticalScrollBar()->setPageStep(std::max(1, view.height()));
    verticalScrollBar()->setSingleStep(kLineStep);
}

int PageView::currentRow() const {
    if (layout_.paged()) {
        return row_;
    }
    return std::max(0, layout_.rowOf(currentPage_));
}

void PageView::setRow(int row) {
    if (layout_.rowCount() == 0) {
        return;
    }
    row = std::clamp(row, 0, layout_.rowCount() - 1);
    if (row == row_) {
        return;
    }
    row_ = row;
    if (layout_.paged()) {
        updateScrollBars();
        viewport()->update();
    }
}

void PageView::setPageMode(PageMode mode) {
    if (mode == options_.mode) {
        return;
    }
    pushView(false);
    const int page = currentPage_;
    options_.mode = mode;
    relayout();
    goToPage(page);
}

void PageView::setCoverPage(bool on) {
    if (on == options_.coverPage) {
        return;
    }
    pushView(false);
    const int page = currentPage_;
    options_.coverPage = on;
    relayout();
    goToPage(page);
}

void PageView::setViewRotation(int degrees) {
    const int r = normalizeRotation(degrees);
    if (r == options_.rotation) {
        return;
    }
    pushView(false);
    const Anchor a = anchorAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    options_.rotation = r;
    relayout();
    if (fit_) {
        options_.zoom = clampViewZoom(fitZoomFor(*fit_), *fit_ != FitMode::ActualSize);
        relayout();
        emit zoomChanged(options_.zoom);
    }
    // Поворот меняет доли страницы — держим её центр.
    if (a.page >= 0) {
        restoreAnchor({a.page, QPointF(0.5, 0.5), a.viewport});
    }
    viewport()->update();
}

void PageView::rotateView(int delta) { setViewRotation(options_.rotation + delta); }

// ---------------------------------------------------------------- координаты

QPointF PageView::scrollPosition() const {
    return {static_cast<double>(horizontalScrollBar()->value()),
            static_cast<double>(verticalScrollBar()->value())};
}

void PageView::setScrollPosition(QPointF scroll) {
    horizontalScrollBar()->setValue(static_cast<int>(std::lround(scroll.x())));
    verticalScrollBar()->setValue(static_cast<int>(std::lround(scroll.y())));
}

QPointF PageView::contentOrigin() const {
    const QSizeF content = contentSize();
    const QSize view = viewport()->size();
    const double ox = content.width() < view.width() ? std::floor((view.width() - content.width()) / 2) : 0.0;
    const double oy = (layout_.paged() && content.height() < view.height())
                          ? std::floor((view.height() - content.height()) / 2)
                          : 0.0;
    return QPointF(ox, oy) - scrollPosition();
}

QRectF PageView::pageViewportRect(int page) const {
    if (page < 0 || page >= layout_.pageCount()) {
        return {};
    }
    const PageSlot& slot = layout_.slot(page);
    if (layout_.paged() && slot.row != row_) {
        return {};
    }
    return slot.rect.translated(contentOrigin());
}

int PageView::pageAtViewport(QPointF point) const { return layout_.pageAt(viewportToContent(point), row_); }

QTransform PageView::pageTransform(int page) const {
    const QRectF r = pageViewportRect(page);
    const QSizeF pt = pageSizePoints(page);
    const Matrix m = pageMatrix(RectF{0, 0, static_cast<float>(pt.width()), static_cast<float>(pt.height())},
                                options_.zoom, 1.0f, options_.rotation);
    return QTransform(m.a, m.b, m.c, m.d, m.e + r.x(), m.f + r.y());
}

QPointF PageView::viewportToPage(int page, QPointF point) const {
    return pageTransform(page).inverted().map(point);
}

std::vector<int> PageView::visiblePages(double margin) const {
    const QRectF area(viewportToContent(QPointF(0, 0)), QSizeF(viewport()->size()));
    return layout_.pagesIn(area.adjusted(0, -margin, 0, margin), row_);
}

PageView::Anchor PageView::anchorAt(QPointF viewportPoint) const {
    Anchor a;
    a.viewport = viewportPoint;
    if (layout_.pageCount() == 0) {
        return a;
    }
    const QPointF c = viewportToContent(viewportPoint);
    int page = layout_.pageAt(c, row_);
    if (page < 0) {
        page = layout_.nearestPage(c, row_);
    }
    if (page < 0) {
        return a;
    }
    const QRectF& r = layout_.slot(page).rect;
    a.page = page;
    a.fraction = QPointF((c.x() - r.left()) / r.width(), (c.y() - r.top()) / r.height());
    return a;
}

void PageView::restoreAnchor(const Anchor& anchor) {
    if (anchor.page < 0 || anchor.page >= layout_.pageCount()) {
        return;
    }
    const PageSlot& slot = layout_.slot(anchor.page);
    if (layout_.paged()) {
        setRow(slot.row);
    }
    const QRectF& r = slot.rect;
    const QPointF c(r.left() + anchor.fraction.x() * r.width(), r.top() + anchor.fraction.y() * r.height());
    // contentOrigin = offset − scroll; точка c должна попасть в anchor.viewport.
    const QPointF offset = contentOrigin() + scrollPosition();
    navigating_ = true;
    setScrollPosition(c + offset - anchor.viewport);
    navigating_ = false;
    updateCurrentPage();
}

// ---------------------------------------------------------------- масштаб

void PageView::setScreenDpi(float dpi) {
    dpi_ = std::clamp(dpi, 24.0f, 1200.0f);
    emit zoomChanged(options_.zoom);
}

float PageView::clampViewZoom(float zoom, bool roundDown) const {
    const float lo = percentToZoom(kMinZoomPercent, dpi_);
    const float hi = percentToZoom(kMaxZoomPercent, dpi_);
    if (!std::isfinite(zoom)) {
        zoom = options_.zoom;
    }
    zoom = std::clamp(zoom, lo, hi);
    // Тысячные — как у ключа тайла и pageMatrix: раскладка и растр совпадают до пикселя.
    const float milli = roundDown ? std::floor(zoom * 1000.0f + 1e-3f) : std::round(zoom * 1000.0f);
    return std::max(1.0f, milli) / 1000.0f;
}

void PageView::applyZoom(float zoom, const Anchor& anchor, bool roundDown) {
    zoom = clampViewZoom(zoom, roundDown);
    if (zoom == options_.zoom) {
        return;
    }
    options_.zoom = zoom;
    relayout();
    restoreAnchor(anchor);
    viewport()->update();
    emit zoomChanged(zoom);
}

void PageView::setZoom(float zoom, std::optional<QPointF> anchor) {
    pushView(false);
    fit_.reset();
    const QPointF point = anchor.value_or(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0));
    applyZoom(zoom, anchorAt(point));
}

void PageView::setZoomPercent(float percent, std::optional<QPointF> anchor) {
    setZoom(percentToZoom(percent, dpi_), anchor);
}

void PageView::zoomIn(std::optional<QPointF> anchor) {
    const float current = zoomPercent();
    for (const float p : zoomPresets()) {
        if (p > current * 1.001f) {
            setZoomPercent(p, anchor);
            return;
        }
    }
    setZoomPercent(kMaxZoomPercent, anchor);
}

void PageView::zoomOut(std::optional<QPointF> anchor) {
    const float current = zoomPercent();
    const QVector<float>& presets = zoomPresets();
    for (auto it = presets.rbegin(); it != presets.rend(); ++it) {
        if (*it < current * 0.999f) {
            setZoomPercent(*it, anchor);
            return;
        }
    }
    setZoomPercent(kMinZoomPercent, anchor);
}

float PageView::rowFitZoom(FitMode mode, const QSizeF& view) const {
    if (mode == FitMode::ActualSize || layout_.rowCount() == 0) {
        return percentToZoom(100.0f, dpi_);
    }
    const LayoutRow& row = layout_.rows()[static_cast<std::size_t>(currentRow())];
    // Ряд в пунктах с поворотом вида: страница или разворот (одна страница разворота — как две такие).
    double width = 0;
    double height = 0;
    for (int k = 0; k < row.count; ++k) {
        const QSizeF s = viewRotatedSize(sizes_[row.first + k], options_.rotation);
        width += s.width();
        height = std::max(height, s.height());
    }
    const bool facing = isFacing(options_.mode);
    if (facing && row.count == 1) {
        width *= 2;
    }
    const double gaps = (facing ? 3.0 : 2.0) * options_.gap;
    Viewport vp;
    vp.width = static_cast<float>(std::max(1.0, view.width() - gaps));
    vp.height = static_cast<float>(std::max(1.0, view.height() - 2.0 * options_.gap));
    vp.dpi = dpi_;
    const RectF rowRect{0, 0, static_cast<float>(width), static_cast<float>(height)};
    return fitZoom(rowRect, vp, mode == FitMode::Visible ? FitMode::Width : mode, 0);
}

float PageView::fitZoomFor(FitMode mode) const {
    const QSizeF full = maximumViewportSize();
    float z = rowFitZoom(mode, full);
    if (mode == FitMode::ActualSize) {
        return z;
    }
    // Содержимое выше окна — появится вертикальная полоса: подгонка по ширине без неё.
    LayoutOptions o = options_;
    o.zoom = clampViewZoom(z, true);
    const PageLayout probe = layoutPages(sizes_, o);
    const int bar = style()->pixelMetric(QStyle::PM_ScrollBarExtent, nullptr, this);
    if (probe.contentSize(currentRow()).height() > full.height() + 0.01) {
        z = rowFitZoom(mode, QSizeF(full.width() - bar, full.height()));
    }
    return z;
}

void PageView::fit(FitMode mode) {
    pushView(false);
    const int page = currentPage_;
    applyZoom(fitZoomFor(mode), anchorAt(QPointF(viewport()->width() / 2.0, viewport()->height() / 2.0)),
              mode != FitMode::ActualSize);
    fit_ = mode;
    if (mode == FitMode::Page || mode == FitMode::Height) {
        // Страница целиком: её ряд — к верху окна.
        navigating_ = true;
        const PageSlot& slot = layout_.slot(page);
        const QRectF& r = layout_.rows()[static_cast<std::size_t>(slot.row)].rect;
        setScrollPosition({scrollPosition().x(), r.top() - options_.gap});
        navigating_ = false;
        setCurrentPage(page);
    }
    viewport()->update();
}

// ---------------------------------------------------------------- навигация

void PageView::setCurrentPage(int page) {
    if (pageCount() == 0) {
        return;
    }
    page = std::clamp(page, 0, pageCount() - 1);
    if (page != currentPage_) {
        currentPage_ = page;
        emit currentPageChanged(page);
    }
}

void PageView::updateCurrentPage() {
    if (pageCount() == 0) {
        return;
    }
    // Текущая — с наибольшей видимой площадью (равные — первая).
    const QRectF view(QPointF(0, 0), QSizeF(viewport()->size()));
    int best = -1;
    double bestArea = -1;
    for (const int page : visiblePages()) {
        const QRectF r = pageViewportRect(page).intersected(view);
        const double area = r.width() * r.height();
        if (area > bestArea + 0.5) {
            bestArea = area;
            best = page;
        }
    }
    if (best < 0) {
        best = layout_.nearestPage(viewportToContent(view.center()), row_);
    }
    if (best >= 0) {
        setCurrentPage(best);
    }
}

void PageView::goToPage(int page) {
    if (pageCount() == 0) {
        return;
    }
    page = std::clamp(page, 0, pageCount() - 1);
    if (page != currentPage_) {
        pushView(false);
    }
    const PageSlot& slot = layout_.slot(page);
    navigating_ = true;
    if (layout_.paged()) {
        setRow(slot.row);
        setScrollPosition({scrollPosition().x(), 0});
    } else {
        const QRectF& row = layout_.rows()[static_cast<std::size_t>(slot.row)].rect;
        double x = scrollPosition().x();
        const double left = slot.rect.left() - x;
        if (left < 0 || left + slot.rect.width() > viewport()->width()) {
            x = slot.rect.center().x() - viewport()->width() / 2.0;
        }
        setScrollPosition({x, row.top() - options_.gap});
    }
    navigating_ = false;
    setCurrentPage(page);
    viewport()->update();
}

void PageView::goToPagePoint(int page, QPointF pagePoint) {
    if (page < 0 || page >= pageCount()) {
        return;
    }
    pushView(false);
    navigating_ = true;
    if (layout_.paged()) {
        setRow(layout_.slot(page).row);
    }
    const QPointF v = pageTransform(page).map(pagePoint);
    setScrollPosition(scrollPosition() + v);
    navigating_ = false;
    setCurrentPage(page);
    viewport()->update();
}

void PageView::nextPage() {
    if (pageCount() == 0) {
        return;
    }
    const int row = layout_.rowOf(currentPage_);
    if (row + 1 < layout_.rowCount()) {
        goToPage(layout_.rows()[static_cast<std::size_t>(row + 1)].first);
    }
}

void PageView::previousPage() {
    if (pageCount() == 0) {
        return;
    }
    const int row = layout_.rowOf(currentPage_);
    if (row > 0) {
        goToPage(layout_.rows()[static_cast<std::size_t>(row - 1)].first);
    }
}

void PageView::firstPage() { goToPage(0); }

void PageView::lastPage() {
    if (pageCount() > 0) {
        goToPage(layout_.rows().back().first);
    }
}

void PageView::scrollLines(int lines) {
    QScrollBar* bar = verticalScrollBar();
    if (layout_.paged()) {
        if (lines > 0 && bar->value() >= bar->maximum() && row_ + 1 < layout_.rowCount()) {
            setRow(row_ + 1);
            bar->setValue(0);
            setCurrentPage(layout_.rows()[static_cast<std::size_t>(row_)].first);
            return;
        }
        if (lines < 0 && bar->value() <= 0 && row_ > 0) {
            setRow(row_ - 1);
            bar->setValue(bar->maximum());
            setCurrentPage(layout_.rows()[static_cast<std::size_t>(row_)].first);
            return;
        }
    }
    bar->setValue(bar->value() + lines * bar->singleStep());
}

// ---------------------------------------------------------------- история

ViewState PageView::viewState() const {
    ViewState s;
    s.page = currentPage_;
    s.zoom = options_.zoom;
    s.scroll = scrollPosition();
    s.mode = options_.mode;
    s.coverPage = options_.coverPage;
    s.rotation = options_.rotation;
    return s;
}

void PageView::setViewState(const ViewState& state) {
    const bool zoomed = state.zoom != options_.zoom;
    options_.mode = state.mode;
    options_.coverPage = state.coverPage;
    options_.rotation = normalizeRotation(state.rotation);
    options_.zoom = clampViewZoom(state.zoom);
    fit_.reset();
    relayout();
    if (pageCount() > 0) {
        const int page = std::clamp(state.page, 0, pageCount() - 1);
        navigating_ = true;
        if (layout_.paged()) {
            setRow(layout_.slot(page).row);
        }
        setScrollPosition(state.scroll);
        navigating_ = false;
        setCurrentPage(page);
    }
    if (zoomed) {
        emit zoomChanged(options_.zoom);
    }
    viewport()->update();
}

void PageView::pushView(bool continuous) {
    if (pageCount() == 0) {
        return;
    }
    if (continuous && lastContinuousPush_.isValid() && lastContinuousPush_.elapsed() < kContinuousPushMs) {
        lastContinuousPush_.restart();
        return;
    }
    if (continuous) {
        lastContinuousPush_.restart();
    } else {
        lastContinuousPush_.invalidate();
    }
    history_.push(viewState());
    emit historyChanged();
}

void PageView::recordView() { pushView(false); }

bool PageView::goBack() {
    const auto s = history_.back(viewState());
    if (!s) {
        return false;
    }
    lastContinuousPush_.invalidate();
    setViewState(*s);
    emit historyChanged();
    return true;
}

bool PageView::goForward() {
    const auto s = history_.forward(viewState());
    if (!s) {
        return false;
    }
    lastContinuousPush_.invalidate();
    setViewState(*s);
    emit historyChanged();
    return true;
}

// ---------------------------------------------------------------- автопрокрутка

void PageView::startAutoScroll(double pxPerSecond) {
    setAutoScrollSpeed(pxPerSecond);
    autoCarry_ = 0;
    autoClock_.start();
    if (!autoScroll_.isActive()) {
        autoScroll_.start();
        emit autoScrollChanged(true);
    }
}

void PageView::stopAutoScroll() {
    if (autoScroll_.isActive()) {
        autoScroll_.stop();
        emit autoScrollChanged(false);
    }
}

void PageView::setAutoScrollSpeed(double pxPerSecond) {
    autoSpeed_ = std::clamp(pxPerSecond, kMinAutoSpeed, kMaxAutoSpeed);
}

void PageView::autoScrollTick() {
    const double dt = static_cast<double>(autoClock_.restart()) / 1000.0;
    autoCarry_ += autoSpeed_ * dt;
    const int step = static_cast<int>(std::floor(autoCarry_));
    if (step <= 0) {
        return;
    }
    autoCarry_ -= step;
    QScrollBar* bar = verticalScrollBar();
    if (bar->value() >= bar->maximum()) {
        if (layout_.paged() && row_ + 1 < layout_.rowCount()) {
            setRow(row_ + 1);
            bar->setValue(0);
            setCurrentPage(layout_.rows()[static_cast<std::size_t>(row_)].first);
            return;
        }
        stopAutoScroll();
        return;
    }
    bar->setValue(bar->value() + step);
}

// ---------------------------------------------------------------- события

void PageView::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(viewport());
    const Palette& colors = Theme::colors();
    p.fillRect(viewport()->rect(), colors.canvas.color());
    const qreal dpr = devicePixelRatioF();
    const int border = Theme::sizes().pageBorder;
    for (const int page : visiblePages()) {
        QRectF r = pageViewportRect(page);
        // Страница — на целых пикселях устройства.
        r = QRectF(std::round(r.left() * dpr) / dpr, std::round(r.top() * dpr) / dpr,
                   std::round(r.width() * dpr) / dpr, std::round(r.height() * dpr) / dpr);
        p.fillRect(r, kPaperColor.color());
        if (border > 0) {
            p.setPen(QPen(colors.border.color(), border));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r.adjusted(-border / 2.0, -border / 2.0, border / 2.0, border / 2.0));
        }
    }
}

void PageView::resizeEvent(QResizeEvent* event) {
    QAbstractScrollArea::resizeEvent(event);
    if (fit_) {
        const Anchor a = anchorAt(QPointF(viewport()->width() / 2.0, 0));
        const float z = clampViewZoom(fitZoomFor(*fit_), *fit_ != FitMode::ActualSize);
        if (z != options_.zoom) {
            options_.zoom = z;
            relayout();
            restoreAnchor(a);
            emit zoomChanged(z);
            return;
        }
    }
    updateScrollBars();
}

void PageView::scrollContentsBy(int /*dx*/, int /*dy*/) {
    viewport()->update();
    if (!navigating_) {
        updateCurrentPage();
    }
}

void PageView::wheelEvent(QWheelEvent* event) {
    const int delta = event->angleDelta().y();
    if ((event->modifiers() & Qt::ControlModifier) != 0) {
        if (delta != 0) {
            pushView(true);
            fit_.reset();
            const float factor = static_cast<float>(std::pow(kWheelZoomBase, delta / 120.0));
            applyZoom(options_.zoom * factor, anchorAt(event->position()));
        }
        event->accept();
        return;
    }
    if (layout_.paged() && delta != 0 && (event->modifiers() & Qt::ShiftModifier) == 0) {
        QScrollBar* bar = verticalScrollBar();
        if ((delta < 0 && bar->value() >= bar->maximum()) || (delta > 0 && bar->value() <= 0)) {
            scrollLines(delta < 0 ? 1 : -1);
            event->accept();
            return;
        }
    }
    QAbstractScrollArea::wheelEvent(event);
}

void PageView::keyPressEvent(QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();

    if (isAutoScrolling()) {
        if (key == Qt::Key_Escape) {
            stopAutoScroll();
            event->accept();
            return;
        }
        if (mods == Qt::NoModifier && (key == Qt::Key_Down || key == Qt::Key_Up)) {
            setAutoScrollSpeed(key == Qt::Key_Down ? autoSpeed_ * kAutoSpeedStep
                                                   : autoSpeed_ / kAutoSpeedStep);
            event->accept();
            return;
        }
    }

    if (mods == (Qt::ControlModifier | Qt::ShiftModifier)) {
        // Повернуть вид: Ctrl+Shift+= (на английской раскладке — «+») и Ctrl+Shift+− («_»).
        if (key == Qt::Key_Equal || key == Qt::Key_Plus) {
            rotateView(90);
            event->accept();
            return;
        }
        if (key == Qt::Key_Minus || key == Qt::Key_Underscore) {
            rotateView(-90);
            event->accept();
            return;
        }
    }
    if (mods == Qt::ControlModifier) {
        switch (key) {
            case Qt::Key_Equal:
            case Qt::Key_Plus:
                zoomIn();
                event->accept();
                return;
            case Qt::Key_Minus:
                zoomOut();
                event->accept();
                return;
            case Qt::Key_0:
                fit(FitMode::Page);
                event->accept();
                return;
            case Qt::Key_1:
                fit(FitMode::ActualSize);
                event->accept();
                return;
            case Qt::Key_2:
                fit(FitMode::Width);
                event->accept();
                return;
            case Qt::Key_3:
                fit(FitMode::Visible);
                event->accept();
                return;
            default:
                break;
        }
    }
    if (mods == Qt::AltModifier) {
        if (key == Qt::Key_Left) {
            goBack();
            event->accept();
            return;
        }
        if (key == Qt::Key_Right) {
            goForward();
            event->accept();
            return;
        }
    }
    if (mods == Qt::NoModifier) {
        switch (key) {
            case Qt::Key_Home:
                firstPage();
                event->accept();
                return;
            case Qt::Key_End:
                lastPage();
                event->accept();
                return;
            case Qt::Key_PageDown:
            case Qt::Key_Right:
                nextPage();
                event->accept();
                return;
            case Qt::Key_PageUp:
            case Qt::Key_Left:
                previousPage();
                event->accept();
                return;
            case Qt::Key_Down:
                scrollLines(1);
                event->accept();
                return;
            case Qt::Key_Up:
                scrollLines(-1);
                event->accept();
                return;
            default:
                break;
        }
    }
    QAbstractScrollArea::keyPressEvent(event);
}

bool PageView::event(QEvent* event) {
    // Клавиши вида не отдаются ярлыкам окна, пока фокус у вида (Ctrl+0…3, Alt+←/→, Home/End…).
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        const Qt::KeyboardModifiers mods = key->modifiers() & ~Qt::KeypadModifier;
        const int k = key->key();
        const bool ctrlZoom =
            mods == Qt::ControlModifier && (k == Qt::Key_Equal || k == Qt::Key_Plus || k == Qt::Key_Minus ||
                                            (k >= Qt::Key_0 && k <= Qt::Key_3));
        const bool rotate =
            mods == (Qt::ControlModifier | Qt::ShiftModifier) &&
            (k == Qt::Key_Equal || k == Qt::Key_Plus || k == Qt::Key_Minus || k == Qt::Key_Underscore);
        const bool history = mods == Qt::AltModifier && (k == Qt::Key_Left || k == Qt::Key_Right);
        if (ctrlZoom || rotate || history || (isAutoScrolling() && k == Qt::Key_Escape)) {
            event->accept();
            return true;
        }
    }
    return QAbstractScrollArea::event(event);
}

}  // namespace ot::ui
