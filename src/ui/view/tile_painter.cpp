#include "ui/view/tile_painter.h"

#include <QFuture>
#include <QScrollBar>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cmath>

#include "core/mu/error.h"
#include "core/render/display_cache.h"
#include "services/document_session.h"
#include "services/render_pool.h"
#include "services/tile_cache.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

// Приоритеты пула: превью видимых → тайлы видимых (ближе к центру — раньше) → превью запаса → тайлы запаса.
constexpr int kPreviewVisiblePriority = 3'000'000;
constexpr int kPreviewMarginPriority = 2'000'000;
constexpr int kTileVisiblePriority = 1'000'000;
constexpr int kTileMarginPriority = 100'000;
constexpr int kPriorityRange = 99'999;
constexpr int kPriorityHysteresis = 64;  // не переставлять задачу ради пары пикселей
constexpr int kMaxListsKept = 48;        // display lists вне видимых — сверх этого отпускаются
constexpr std::size_t kMaxLog = 20000;

// Рамка вьюпорта (логические px) → device px страницы с началом в origin (device px), округление наружу.
IRect tileDeviceArea(const QRectF& area, const QPointF& origin, qreal dpr) {
    return {static_cast<int>(std::floor(area.left() * dpr - origin.x())),
            static_cast<int>(std::floor(area.top() * dpr - origin.y())),
            static_cast<int>(std::ceil(area.right() * dpr - origin.x())),
            static_cast<int>(std::ceil(area.bottom() * dpr - origin.y()))};
}

QRectF tileViewportRect(const IRect& r, const QPointF& origin, qreal dpr) {
    return {(origin.x() + r.x0) / dpr, (origin.y() + r.y0) / dpr, r.width() / dpr, r.height() / dpr};
}

}  // namespace

TilePainter::TilePainter(PageView& view) : QObject(&view), view_(view) {}

TilePainter::~TilePainter() { dropDocument(); }

void TilePainter::dropDocument() {
    if (pool_ != nullptr && docId_ != 0) {
        pool_->cancelDocument(docId_);
    }
    if (cache_ != nullptr && docId_ != 0) {
        cache_->removeDocument(docId_);
    }
    inflight_.clear();
    priority_.clear();
    pages_.clear();
    visibleComplete_ = false;
}

void TilePainter::setDocument(DocumentSession* session, RenderPool* pool, TileCache* cache) {
    dropDocument();
    session_ = session;
    docId_ = session != nullptr ? session->id() : 0;
    layers_ = session != nullptr ? session->layersHash() : 0;
    if (pool == nullptr) {
        if (!ownPool_) {
            ownPool_ = std::make_unique<RenderPool>();
        }
        pool = ownPool_.get();
    }
    if (cache == nullptr) {
        if (!ownCache_) {
            ownCache_ = std::make_unique<TileCache>();
        }
        cache = ownCache_.get();
    }
    pool_ = pool;
    cache_ = cache;
}

quint64 TilePainter::currentRevision() const { return session_ != nullptr ? session_->revision() : 0; }

TilePainter::PageState& TilePainter::state(int page) {
    auto it = pages_.find(page);
    if (it == pages_.end()) {
        PageState s;
        s.revision = currentRevision();
        it = pages_.insert(page, s);
    }
    return *it;
}

bool TilePainter::isBroken(int page) const {
    const auto it = pages_.constFind(page);
    return it != pages_.constEnd() && it->broken;
}

QString TilePainter::brokenMessage(int page) const {
    const auto it = pages_.constFind(page);
    return it != pages_.constEnd() ? it->error : QString();
}

std::optional<TilePainter::Geometry> TilePainter::geometry(int page, const PageState& s) const {
    if (!s.list || s.bounds.empty()) {
        return std::nullopt;
    }
    Geometry g;
    // Раскладка знает размер страницы (уменьшенный до 14 400 pt, fit.h) — растр подгоняется под него.
    const QSizeF layout = view_.pageSizePoints(page);
    double scale = layout.width() > 0 ? layout.width() / s.bounds.width() : 1.0;
    if (std::fabs(scale - 1.0) < 1e-4) {
        scale = 1.0;
    }
    g.zoom = static_cast<float>(view_.zoom() * scale);
    g.dpr = static_cast<float>(view_.devicePixelRatioF());
    g.rotation = view_.viewRotation();
    g.device = pageDeviceRect(s.bounds, g.zoom, g.dpr, g.rotation);
    return g;
}

// ---------------------------------------------------------------- кадр

bool TilePainter::drawTiles(QPainter& p, int page, const PageState& s, const Geometry& g, float zoom,
                            quint64 revision, const QPointF& origin, const IRect& visible, bool exact) {
    // Растр масштаба zoom растягивается в масштаб кадра g.zoom (factor); exact — тот же масштаб.
    const double factor = g.zoom / zoom;
    const IRect device = exact ? g.device : pageDeviceRect(s.bounds, zoom, g.dpr, g.rotation);
    const IRect area = exact ? visible
                             : IRect{static_cast<int>(std::floor(visible.x0 / factor)),
                                     static_cast<int>(std::floor(visible.y0 / factor)),
                                     static_cast<int>(std::ceil(visible.x1 / factor)),
                                     static_cast<int>(std::ceil(visible.y1 / factor))};
    bool complete = true;
    for (const TileRect& tile : tilesIntersecting(device, area)) {
        const TileKey key = makeTileKey(docId_, page, zoom, g.dpr, g.rotation, tile, revision, layers_);
        const std::optional<QImage> image = cache_->find(key);
        if (!image) {
            complete = false;
            continue;
        }
        if (exact) {
            p.drawImage(QPointF((origin.x() + tile.rect.x0) / g.dpr, (origin.y() + tile.rect.y0) / g.dpr),
                        *image);
        } else {
            const QRectF target((origin.x() + tile.rect.x0 * factor) / g.dpr,
                                (origin.y() + tile.rect.y0 * factor) / g.dpr,
                                tile.rect.width() * factor / g.dpr, tile.rect.height() * factor / g.dpr);
            p.drawImage(target, *image, QRectF(QPointF(0, 0), QSizeF(image->size())));
        }
    }
    return complete;
}

void TilePainter::paintPage(QPainter& p, int page, const QRectF& rect) {
    p.fillRect(rect, kPaperColor.color());
    if (session_ == nullptr || cache_ == nullptr) {
        return;
    }
    PageState& s = state(page);
    if (s.broken) {
        paintBroken(p, rect);
        return;
    }
    const std::optional<Geometry> g = geometry(page, s);
    if (!g) {
        return;
    }
    const QPointF origin(std::round(rect.left() * g->dpr), std::round(rect.top() * g->dpr));
    const QRectF shown = rect.intersected(QRectF(view_.viewport()->rect()));
    if (shown.isEmpty()) {
        return;
    }
    IRect visible = tileDeviceArea(shown, origin, g->dpr);
    visible = {std::max(visible.x0, g->device.x0), std::max(visible.y0, g->device.y0),
               std::min(visible.x1, g->device.x1), std::min(visible.y1, g->device.y1)};

    p.save();
    p.setClipRect(rect);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // Превью — растянуть на страницу (текущей правки или прежней).
    std::optional<QImage> preview =
        cache_->findPreview(makePreviewKey(docId_, page, g->zoom, g->dpr, g->rotation, s.revision, layers_));
    if (!preview && s.fallbackRevision) {
        preview = cache_->findPreview(
            makePreviewKey(docId_, page, g->zoom, g->dpr, g->rotation, *s.fallbackRevision, layers_));
    }
    if (preview) {
        p.drawImage(rect, *preview, QRectF(QPointF(0, 0), QSizeF(preview->size())));
    }
    // Есть ли все тайлы кадра — без отметки «свежий» (contains).
    bool missing = false;
    for (const TileRect& tile : tilesIntersecting(g->device, visible)) {
        if (!cache_->contains(
                makeTileKey(docId_, page, g->zoom, g->dpr, g->rotation, tile, s.revision, layers_))) {
            missing = true;
            break;
        }
    }
    if (missing) {
        // Подмена: прежний масштаб этой правки, затем этот масштаб прежней правки.
        if (s.completeZoom && *s.completeZoom != g->zoom) {
            drawTiles(p, page, s, *g, *s.completeZoom, s.revision, origin, visible, false);
        }
        if (s.fallbackRevision) {
            drawTiles(p, page, s, *g, g->zoom, *s.fallbackRevision, origin, visible, true);
        }
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform, false);
    const bool complete = drawTiles(p, page, s, *g, g->zoom, s.revision, origin, visible, true);
    p.restore();
    if (complete) {
        s.completeZoom = g->zoom;
        s.fallbackRevision.reset();
    }
}

void TilePainter::paintBroken(QPainter& p, const QRectF& rect) const {
    p.save();
    p.setClipRect(rect);
    p.setPen(Theme::colors().textMuted.color());
    p.setFont(uiFont(Theme::sizes()));
    p.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, tr("Страница не отрисована"));
    p.restore();
}

// ---------------------------------------------------------------- запросы

void TilePainter::schedule() {
    if (session_ == nullptr || pool_ == nullptr || cache_ == nullptr || view_.pageCount() == 0) {
        visibleComplete_ = session_ == nullptr;
        return;
    }
    const QRectF viewport(view_.viewport()->rect());
    const double screen = viewport.height();
    const QRectF extended = viewport.adjusted(0, -screen, 0, screen);
    const QPointF center = viewport.center();
    const std::vector<int> visiblePages = view_.visiblePages(0);
    const std::vector<int> pages = view_.visiblePages(screen);
    std::unordered_set<TileKey, TileKeyHash> wanted;
    bool complete = true;

    for (const int page : pages) {
        const bool isVisible =
            std::find(visiblePages.begin(), visiblePages.end(), page) != visiblePages.end();
        PageState& s = state(page);
        if (s.broken) {
            continue;
        }
        if (!s.list) {
            if (!s.requested) {
                requestList(page, isVisible);
            }
            complete = complete && !isVisible;
            continue;
        }
        const std::optional<Geometry> g = geometry(page, s);
        if (!g) {
            continue;
        }
        const QRectF rect = view_.pageViewportRect(page);
        const QPointF origin(std::round(rect.left() * g->dpr), std::round(rect.top() * g->dpr));

        // Превью страницы целиком.
        const TileKey pk = makePreviewKey(docId_, page, g->zoom, g->dpr, g->rotation, s.revision, layers_);
        wanted.insert(pk);
        if (!inflight_.contains(pk) && !cache_->findPreview(pk)) {
            const float pz = previewZoom(g->zoom);
            submit(page, s, pk, pageMatrix(s.bounds, pz, g->dpr, g->rotation),
                   pageDeviceRect(s.bounds, pz, g->dpr, g->rotation),
                   isVisible ? kPreviewVisiblePriority : kPreviewMarginPriority, true, rect);
        }

        // Тайлы видимой области и запаса в экран по вертикали.
        const QRectF area = rect.intersected(extended);
        if (area.isEmpty()) {
            continue;
        }
        const Matrix matrix = pageMatrix(s.bounds, g->zoom, g->dpr, g->rotation);
        for (const TileRect& tile : tilesIntersecting(g->device, tileDeviceArea(area, origin, g->dpr))) {
            const TileKey key =
                makeTileKey(docId_, page, g->zoom, g->dpr, g->rotation, tile, s.revision, layers_);
            wanted.insert(key);
            if (cache_->contains(key)) {
                continue;
            }
            const QRectF onScreen = tileViewportRect(tile.rect, origin, g->dpr);
            const bool inView = onScreen.intersects(viewport);
            complete = complete && !inView;
            const QPointF d = onScreen.center() - center;
            const int distance = std::min(kPriorityRange, static_cast<int>(std::hypot(d.x(), d.y())));
            const int priority = (inView ? kTileVisiblePriority : kTileMarginPriority) - distance;
            const auto job = inflight_.find(key);
            if (job == inflight_.end()) {
                submit(page, s, key, matrix, tile.rect, priority, false, onScreen);
            } else if (std::abs(priority_[key] - priority) > kPriorityHysteresis) {
                if (pool_->setPriority(job->second, priority)) {
                    priority_[key] = priority;
                }
            }
        }
    }

    // Не нужные больше задачи (зум, прокрутка) — отменить.
    for (auto it = inflight_.begin(); it != inflight_.end();) {
        if (!wanted.contains(it->first)) {
            pool_->cancel(it->second);
            priority_.erase(it->first);
            it = inflight_.erase(it);
        } else {
            ++it;
        }
    }

    // Display lists далёких страниц — отпустить (у сессии свой кеш на 40 страниц).
    int lists = 0;
    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        lists += it->list ? 1 : 0;
    }
    if (lists > kMaxListsKept) {
        for (auto it = pages_.begin(); it != pages_.end(); ++it) {
            if (it->list && std::find(pages.begin(), pages.end(), it.key()) == pages.end()) {
                it->list.reset();
                it->requested = false;
                ++it->generation;
            }
        }
    }
    visibleComplete_ = complete;
}

void TilePainter::submit(int page, const PageState& s, const TileKey& key, const Matrix& matrix,
                         const IRect& rect, int priority, bool preview, const QRectF& viewportRect) {
    TileRequest request;
    request.key = key;
    request.list = s.list;
    request.matrix = matrix;
    request.rect = rect;
    request.dpr = view_.devicePixelRatioF();
    request.priority = priority;
    const quint64 job = pool_->submit(
        request, this, [this, preview](const TileResult& result) { tileArrived(result, preview); });
    inflight_[key] = job;
    priority_[key] = priority;
    if (log_.size() >= kMaxLog) {
        log_.erase(log_.begin(), log_.begin() + static_cast<std::ptrdiff_t>(kMaxLog / 2));
    }
    log_.push_back({key, preview, page, viewportRect, priority});
}

void TilePainter::tileArrived(const TileResult& result, bool preview) {
    inflight_.erase(result.key);
    priority_.erase(result.key);
    if (result.key.doc != docId_ || cache_ == nullptr) {
        return;
    }
    if (result.ok()) {
        if (preview) {
            cache_->insertPreview(result.key, result.image);
        } else {
            cache_->insert(result.key, result.image);
        }
        view_.viewport()->update();
        return;
    }
    if (result.aborted) {
        return;
    }
    markBroken(result.key.page, result.error.message);
}

void TilePainter::requestList(int page, bool visible) {
    PageState& s = state(page);
    s.requested = true;
    const quint64 generation = s.generation;
    QFuture<std::shared_ptr<const DisplayList>> future =
        session_->displayList(page, true, visible ? TaskPriority::High : TaskPriority::Normal);
    future.then(this, [this, page, generation](QFuture<std::shared_ptr<const DisplayList>> done) {
        try {
            listArrived(page, generation, done.result());
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted) {
                const auto it = pages_.find(page);
                if (it != pages_.end() && it->generation == generation) {
                    it->requested = false;  // отменено (cancelAll) — запросить снова при следующем кадре
                }
                return;
            }
            listFailed(page, generation, QString::fromUtf8(e.what()));
        } catch (const std::exception& e) {
            listFailed(page, generation, QString::fromUtf8(e.what()));
        }
    });
}

void TilePainter::listArrived(int page, quint64 generation, std::shared_ptr<const DisplayList> list) {
    const auto it = pages_.find(page);
    if (it == pages_.end() || it->generation != generation || !list) {
        return;
    }
    it->list = std::move(list);
    it->bounds = it->list->bounds();
    view_.viewport()->update();
}

void TilePainter::listFailed(int page, quint64 generation, const QString& message) {
    const auto it = pages_.find(page);
    if (it == pages_.end() || it->generation != generation) {
        return;
    }
    markBroken(page, message);
}

void TilePainter::markBroken(int page, const QString& message) {
    PageState& s = state(page);
    if (s.broken) {
        return;
    }
    s.broken = true;
    s.error = message;
    spdlog::warn("page {} not rendered: {}", page + 1, message.toStdString());
    emit pageFailed(page, message);
    view_.viewport()->update();
}

// ---------------------------------------------------------------- правки и экран

void TilePainter::invalidatePage(int page) {
    PageState& s = state(page);
    if (s.list || s.completeZoom) {
        s.fallbackRevision = s.revision;
    }
    s.revision = currentRevision();
    s.list.reset();
    s.requested = false;
    s.broken = false;
    s.error.clear();
    ++s.generation;
    view_.viewport()->update();
}

void TilePainter::setLayersHash(quint64 hash) {
    if (hash == layers_) {
        return;
    }
    // Другая видимость слоёв — другие растры и display lists (сессия их уже сбросила); прежние не подменяют.
    layers_ = hash;
    invalidateAll();
}

void TilePainter::invalidateAll() {
    // Страницы переставлены или удалены: прежние растры к номерам не подходят — без подмены.
    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        it->revision = currentRevision();
        it->fallbackRevision.reset();
        it->completeZoom.reset();
        it->list.reset();
        it->requested = false;
        it->broken = false;
        it->error.clear();
        ++it->generation;
    }
    view_.viewport()->update();
}

void TilePainter::resetDevice() {
    if (pool_ != nullptr && docId_ != 0) {
        pool_->cancelDocument(docId_);
    }
    if (cache_ != nullptr && docId_ != 0) {
        cache_->removeDocument(docId_);
    }
    inflight_.clear();
    priority_.clear();
    for (auto it = pages_.begin(); it != pages_.end(); ++it) {
        it->completeZoom.reset();
        it->fallbackRevision.reset();
    }
    view_.viewport()->update();
}

}  // namespace ot::ui
