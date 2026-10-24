// PageView — вид страниц документа (эталон 3.1, 4.2, 6.1 «Документ»; view-03…09): QAbstractScrollArea над
// раскладкой layoutPages() (ui/view/layout.h) на сером холсте canvas. Полосы прокрутки — в логических
// пикселях содержимого; содержимое уже вьюпорта — по центру (постраничные режимы — и по высоте).
//
// Масштаб zoom — логических px на пункт, проценты — относительно dpi/72 (100 % — «реальный размер» при
// системных 96 dpi; dpi задаёт setScreenDpi); непрерывный, 8–6400 %. Якорь масштаба — точка вьюпорта
// (Ctrl+колесо — под курсором, клавиши и кнопки — центр): точка страницы под якорем остаётся на месте.
// Подгонки (Ctrl+0/1/2/3 — страница, реальный размер, ширина, видимое) — по текущему ряду (страница или
// разворот); подгонка «липкая» — держится при смене размера окна, пока масштаб не сменят явно.
// Поворот вида (Ctrl+Shift+= / −) — матрица раскладки, документ не меняется.
//
// Навигация — клавиши эталона 6.8: Home/End, PgUp/PgDn и ←/→ (страница), ↑/↓ (строка), Alt+←/→ (история
// видов, ui/view/view_history.h — стек из 50). Автопрокрутка — таймер с регулируемой скоростью (↑/↓ меняют
// скорость, Esc — стоп). Без документа (setPageSizes) вид раскладывается и листается — так его проверяют
// тесты раскладки; рендер — с документом (setDocument).
//
// Ссылки (2.2b; view-13): слой LinkLayer — курсор-указатель и переход по щелчку у «Выделения», «Руки» и
// «Панорамирования», Tab/Shift+Tab по ссылкам видимых страниц с рамкой фокуса, Enter — переход. activateLink
// шлёт linkActivated(LinkAction) для любой ссылки (что делать с GoToR и URI — решает окно 2.4/2.5, Launch —
// никогда) и сам переходит по GoTo/Named. Совпадения поиска (view-18) — setSearchHits/setCurrentSearchHit:
// подсветка всех, текущее — обводкой, вид прокручивается к нему.
#pragma once

#include <QAbstractScrollArea>
#include <QColor>
#include <QElapsedTimer>
#include <QPointer>
#include <QSizeF>
#include <QTimer>
#include <QTransform>
#include <QVector>

#include <memory>
#include <optional>
#include <vector>

#include "core/links/links.h"
#include "core/render/fit.h"
#include "core/render/render_style.h"
#include "core/text/search.h"
#include "services/document_session.h"
#include "ui/view/layout.h"
#include "ui/view/overlay.h"
#include "ui/view/view_history.h"

namespace ot {
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class LinkLayer;
class TilePainter;
class ToolController;

class PageView : public QAbstractScrollArea {
    Q_OBJECT
public:
    static constexpr float kMinZoomPercent = 8.0f;
    static constexpr float kMaxZoomPercent = 6400.0f;
    static constexpr double kDefaultAutoScrollSpeed = 60.0;  // логических px в секунду
    // Предустановки масштаба Acrobat (проценты): шаги Ctrl+= / Ctrl+−.
    static const QVector<float>& zoomPresets();

    explicit PageView(QWidget* parent = nullptr);
    ~PageView() override;

    // Документ (не владеет; nullptr — пусто): размеры страниц и /PageLayout — из OpenSummary; пул и кеш
    // тайлов — общие на процесс (nullptr — свои у вида).
    void setDocument(DocumentSession* session, RenderPool* pool = nullptr, TileCache* cache = nullptr);
    DocumentSession* document() const noexcept { return session_; }
    // Только размеры страниц (пункты, с /Rotate) — без рендера.
    void setPageSizes(const QVector<QSizeF>& pointSizes);
    int pageCount() const noexcept { return static_cast<int>(sizes_.size()); }
    QSizeF pageSizePoints(int page) const;
    // Уважать /PageLayout файла при открытии (настройка «Раскладка: как в документе»; по умолчанию — да).
    void setRespectDocumentLayout(bool on) noexcept { respectLayout_ = on; }
    bool respectDocumentLayout() const noexcept { return respectLayout_; }

    // Раскладка.
    PageMode pageMode() const noexcept { return options_.mode; }
    void setPageMode(PageMode mode);
    bool coverPage() const noexcept { return options_.coverPage; }
    void setCoverPage(bool on);
    int viewRotation() const noexcept { return options_.rotation; }
    void setViewRotation(int degrees);
    void rotateView(int delta);  // ±90
    const PageLayout& pageLayout() const noexcept { return layout_; }
    // Ряд на экране в постраничных режимах (в непрерывных — ряд текущей страницы).
    int currentRow() const;

    // Масштаб.
    float zoom() const noexcept { return options_.zoom; }
    float zoomPercent() const noexcept { return zoomToPercent(options_.zoom, dpi_); }
    float screenDpi() const noexcept { return dpi_; }
    void setScreenDpi(float dpi);
    void setZoom(float zoom, std::optional<QPointF> anchor = std::nullopt);
    void setZoomPercent(float percent, std::optional<QPointF> anchor = std::nullopt);
    void zoomIn(std::optional<QPointF> anchor = std::nullopt);
    void zoomOut(std::optional<QPointF> anchor = std::nullopt);
    // Подгонка по текущему ряду; держится при смене размера, пока масштаб не сменят явно.
    void fit(FitMode mode);
    std::optional<FitMode> fitMode() const noexcept { return fit_; }
    // Масштаб подгонки (без применения): вьюпорт без полос и с вертикальной, если содержимое выше окна.
    float fitZoomFor(FitMode mode) const;

    // Навигация.
    int currentPage() const noexcept { return currentPage_; }
    void goToPage(int page);
    // Точку страницы (пункты страницы) — в левый верхний угол вьюпорта (переходы по ссылкам и закладкам).
    void goToPagePoint(int page, QPointF pagePoint);
    void nextPage();
    void previousPage();
    void firstPage();
    void lastPage();
    // Прокрутка на шаг строки (↑/↓) или экран.
    void scrollLines(int lines);

    // История видов (Alt+← / Alt+→).
    ViewState viewState() const;
    void setViewState(const ViewState& state);
    void recordView();
    bool goBack();
    bool goForward();
    const ViewHistory& history() const noexcept { return history_; }

    // Автопрокрутка (Ctrl+Shift+H в Acrobat): скорость — логических px в секунду, ↑/↓ меняют её, Esc — стоп.
    void startAutoScroll(double pxPerSecond = kDefaultAutoScrollSpeed);
    void stopAutoScroll();
    bool isAutoScrolling() const noexcept { return autoScroll_.isActive(); }
    double autoScrollSpeed() const noexcept { return autoSpeed_; }
    void setAutoScrollSpeed(double pxPerSecond);

    // Координаты: содержимое (раскладка) ↔ вьюпорт ↔ страница (пункты MuPDF: начало — левый верхний угол).
    QPointF contentOrigin() const;
    QPointF viewportToContent(QPointF point) const { return point - contentOrigin(); }
    QPointF contentToViewport(QPointF point) const { return point + contentOrigin(); }
    // Страница на экране; пусто — не в текущем ряду постраничного режима.
    QRectF pageViewportRect(int page) const;
    int pageAtViewport(QPointF point) const;
    // Пункты страницы → вьюпорт (масштаб, поворот вида, место страницы).
    QTransform pageTransform(int page) const;
    QPointF viewportToPage(int page, QPointF point) const;
    // Страницы, видимые во вьюпорте (с запасом margin логических px по вертикали).
    std::vector<int> visiblePages(double margin = 0) const;
    // Прокрутить так, чтобы рамка (пункты страницы) с полем margin px была во вьюпорте (наименьший сдвиг;
    // страница другого ряда постраничного режима — сначала её ряд).
    void ensureVisible(int page, const RectF& rect, double margin = 0);

    // Ссылки страниц и переход по ним.
    LinkLayer& linkLayer() const noexcept { return *links_; }
    void activateLink(const links::LinkInfo& link);

    // Совпадения поиска (SearchSession 1.4): подсветка всех; текущее — обводка и прокрутка к нему.
    void setSearchHits(std::vector<SearchHit> hits);
    void clearSearchHits();
    const std::vector<SearchHit>& searchHits() const noexcept { return hits_; }
    void setCurrentSearchHit(int index);
    int currentSearchHit() const noexcept { return currentHit_; }

    // Цвет холста вместо токена canvas (полноэкранный режим — цвет фона из его настроек); невалидный — токен.
    void setCanvasColor(const QColor& color) {
        canvasColor_ = color;
        viewport()->update();
    }
    // Рамка вокруг страницы (полный экран — без неё).
    void setPageBorder(bool on) {
        pageBorder_ = on;
        viewport()->update();
    }

    // Растр страниц (ui/view/tile_painter.h) и слои поверх них (ui/view/overlay.h): выделение текста,
    // совпадения поиска, рамки аннотаций — встроенные; инструменты добавляют свои.
    TilePainter& tilePainter() const noexcept { return *tiles_; }
    // Как рисовать страницы (2.5b: замена цветов, overprint, сглаживание; ui/view/view_style.h).
    void setRenderStyle(const RenderStyle& style);
    const RenderStyle& renderStyle() const noexcept;
    OverlayStack& overlays() noexcept { return overlays_; }
    QuadOverlay& selectionOverlay() noexcept { return selection_; }
    QuadOverlay& searchOverlay() noexcept { return search_; }
    RectOverlay& annotationOverlay() noexcept { return annotFrames_; }
    // Инструменты просмотра (ui/view/tool_controller.h): мышь и клавиши вида идут текущему инструменту.
    ToolController& tools() const noexcept { return *controller_; }

signals:
    void zoomChanged(float zoom);
    void currentPageChanged(int page);
    void layoutChanged();
    void historyChanged();
    void autoScrollChanged(bool active);
    // Щелчок или Enter по ссылке (любой вид; переход по GoTo/Named вид делает сам, Launch не исполняется
    // никогда).
    void linkActivated(const ot::links::LinkAction& action);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool event(QEvent* event) override;
    bool focusNextPrevChild(bool next) override;

private:
    struct Anchor {
        int page = -1;
        QPointF fraction;  // доля страницы под точкой (вне страницы — за пределами 0…1)
        QPointF viewport;
    };

    void reloadSizes();
    void relayout();
    void updateScrollBars();
    QSizeF contentSize() const;
    QPointF scrollPosition() const;
    void setScrollPosition(QPointF scroll);
    Anchor anchorAt(QPointF viewportPoint) const;
    void restoreAnchor(const Anchor& anchor);
    void applyZoom(float zoom, const Anchor& anchor, bool roundDown = false);
    // Ограничить 8–6400 % и округлить до тысячных (подгонка — вниз: страница не шире окна).
    float clampViewZoom(float zoom, bool roundDown = false) const;
    float rowFitZoom(FitMode mode, const QSizeF& viewport) const;
    void setRow(int row);
    void updateCurrentPage();
    void setCurrentPage(int page);
    void autoScrollTick();
    void pushView(bool continuous);

    QPointer<DocumentSession> session_;
    RenderPool* pool_ = nullptr;
    TileCache* cache_ = nullptr;
    QVector<QSizeF> sizes_;
    LayoutOptions options_;
    PageLayout layout_;
    int row_ = 0;
    int currentPage_ = 0;
    std::optional<FitMode> fit_;
    float dpi_ = 96.0f;
    bool respectLayout_ = true;
    bool navigating_ = false;  // своя прокрутка: текущая страница задана явно
    ViewHistory history_;
    QElapsedTimer lastContinuousPush_;
    QTimer autoScroll_;
    QElapsedTimer autoClock_;
    double autoSpeed_ = kDefaultAutoScrollSpeed;
    double autoCarry_ = 0;
    TilePainter* tiles_ = nullptr;          // ребёнок вида
    ToolController* controller_ = nullptr;  // ребёнок вида
    std::unique_ptr<LinkLayer> links_;
    std::vector<SearchHit> hits_;
    std::vector<std::vector<std::pair<int, int>>> hitQuads_;  // совпадение → его квады {страница, индекс}
    int currentHit_ = -1;
    OverlayStack overlays_;
    QuadOverlay selection_;
    QuadOverlay search_;
    RectOverlay annotFrames_;
    QMetaObject::Connection screenConnection_;
    QColor canvasColor_;
    bool pageBorder_ = true;
};

}  // namespace ot::ui
