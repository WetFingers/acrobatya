// TilePainter — растр страниц PageView (план 2.2; эталон 4.1–4.2, 6.1 HiDPI; view-01 — экран): display list
// страницы — задачей DocumentSession (единственное, что уходит из потока документа), тайлы 512 × 512 device
// px — в RenderPool, растры — в TileCache (общие на процесс или свои у вида). ui хранит только номер
// страницы, TileKey и shared_ptr<const DisplayList> — указателей MuPDF нет.
//
// Кадр страницы: бумага → превью 1/8 (растянуто, пока тайлов нет) → тайлы прежнего масштаба или прежней
// правки (растянуты QPainter до прихода новых) → тайлы. Тайл рисуется на целых пикселях устройства
// (devicePixelRatioF, QImage::setDevicePixelRatio) — без швов. Повреждённая страница (display list или
// растр — ошибка) — заглушка «Страница не отрисована» и строка в журнале, остальные живут.
//
// schedule() после кадра: недостающие тайлы видимых страниц и запаса в 1 экран по вертикали — в пул с
// приоритетом по расстоянию до центра окна (видимые выше запаса, превью выше всех); задачи, которые больше
// не нужны (зум, прокрутка), отменяются (cookie abort). Ключ — {сессия, страница, масштаб, dpr, поворот
// вида, тайл, правка страницы}: правка страницы (pageChanged) меняет только её ключи; смена экрана
// (screenChanged) сбрасывает растры документа.
#pragma once

#include <QHash>
#include <QObject>
#include <QPainter>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QString>

#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/render/render_style.h"
#include "core/render/tiles.h"
#include "core/util/geometry.h"

namespace ot {
class DisplayList;
class DocumentSession;
class RenderPool;
class TileCache;
struct TileResult;
}  // namespace ot

namespace ot::ui {

class PageView;

class TilePainter : public QObject {
    Q_OBJECT
public:
    // Запрос в пул (журнал для проверок): ключ, страница, рамка тайла на экране в момент запроса.
    struct Requested {
        TileKey key;
        bool preview = false;
        int page = 0;
        QRectF viewportRect;
        int priority = 0;
    };

    explicit TilePainter(PageView& view);
    ~TilePainter() override;

    TilePainter(const TilePainter&) = delete;
    TilePainter& operator=(const TilePainter&) = delete;

    // Документ вида; pool/cache — общие (nullptr — свои). Прежнему документу — отмена и сброс растров.
    void setDocument(DocumentSession* session, RenderPool* pool, TileCache* cache);
    RenderPool* pool() const noexcept { return pool_; }
    TileCache* cache() const noexcept { return cache_; }

    // Страница page в рамке rect (вьюпорт, на целых пикселях устройства).
    void paintPage(QPainter& painter, int page, const QRectF& rect);
    // После кадра: запросить недостающее, отменить лишнее.
    void schedule();

    // Правка страницы / структуры: новые ключи, прежние растры — подмена до прихода новых.
    void invalidatePage(int page);
    void invalidateAll();
    // Видимость слоёв документа (DocumentSession::layersHash, поле ключа тайлов): другая — всё заново.
    void setLayersHash(quint64 hash);
    quint64 layersHash() const noexcept { return layers_; }
    // Как рисовать (2.5b: замена цветов, overprint, сглаживание — RenderStyle::key() в поле style ключа):
    // другой стиль — все растры заново, прежние не подменяют; бумага — paperColor(style).
    void setStyle(const RenderStyle& style);
    const RenderStyle& style() const noexcept { return style_; }
    // Смена экрана (dpr): растры документа и задачи — сброс.
    void resetDevice();

    bool isBroken(int page) const;
    QString brokenMessage(int page) const;
    // Все тайлы видимых страниц прошлого кадра были в кеше (растр готов целиком).
    bool visibleComplete() const noexcept { return visibleComplete_; }
    int inflight() const noexcept { return static_cast<int>(inflight_.size()); }
    const std::vector<Requested>& requestLog() const noexcept { return log_; }
    void clearRequestLog() { log_.clear(); }

signals:
    // Страница не отрисована (display list или растр — ошибка): номер и английский текст для журнала.
    void pageFailed(int page, const QString& message);

private:
    struct PageState {
        std::shared_ptr<const DisplayList> list;
        RectF bounds;            // рамка страницы в пунктах (DisplayList::bounds)
        bool requested = false;  // display list запрошен
        bool broken = false;
        QString error;
        quint64 revision = 0;                     // поле revision ключей страницы
        std::optional<quint64> fallbackRevision;  // прежняя правка — подмена
        quint64 generation = 0;                   // отбросить устаревший ответ display list
        std::optional<float> completeZoom;        // масштаб, при котором страница была нарисована целиком
    };

    struct Geometry {
        float zoom = 1;  // масштаб растра страницы (вид × раскладка / рамка)
        float dpr = 1;
        int rotation = 0;
        IRect device;  // рамка страницы в device px этого масштаба
    };

    PageState& state(int page);
    std::optional<Geometry> geometry(int page, const PageState& s) const;
    void requestList(int page, bool visible);
    void listArrived(int page, quint64 generation, std::shared_ptr<const DisplayList> list);
    void listFailed(int page, quint64 generation, const QString& message);
    void submit(int page, const PageState& s, const TileKey& key, const Matrix& matrix, const IRect& rect,
                int priority, bool preview, const QRectF& viewportRect);
    void tileArrived(const TileResult& result, bool preview);
    void markBroken(int page, const QString& message);
    void dropDocument();
    bool drawTiles(QPainter& p, int page, const PageState& s, const Geometry& g, float zoom, quint64 revision,
                   const QPointF& origin, const IRect& visible, bool exact);
    void paintBroken(QPainter& p, const QRectF& rect) const;
    quint64 currentRevision() const;

    PageView& view_;
    QPointer<DocumentSession> session_;
    quint64 docId_ = 0;
    quint64 layers_ = 0;  // DocumentSession::layersHash() — поле layersHash ключей
    RenderStyle style_;
    quint64 styleKey_ = 0;  // style_.key() — поле style ключей
    RenderPool* pool_ = nullptr;
    TileCache* cache_ = nullptr;
    std::unique_ptr<RenderPool> ownPool_;
    std::unique_ptr<TileCache> ownCache_;
    QHash<int, PageState> pages_;
    std::unordered_map<TileKey, quint64, TileKeyHash> inflight_;  // ключ → задача пула
    std::unordered_map<TileKey, int, TileKeyHash> priority_;      // приоритет поставленной задачи
    std::vector<Requested> log_;
    bool visibleComplete_ = false;
};

}  // namespace ot::ui
