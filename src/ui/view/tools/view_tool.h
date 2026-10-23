// Инструменты просмотра PageView (план 2.2; эталон 3.1 «Масштаб», 3.2 «Выделение», 6.8; view-05, view-33,
// comment-42): состояние мыши и клавиатуры одного инструмента — ViewTool, переключение — ToolController
// (ui/view/tool_controller.h). Рука (H; пробел — временно), Выделение (V: текст по строкам, Alt — столбцы,
// двойной/тройной щелчок — слово/строка, Ctrl+A, I-курсор над текстом), Увеличение области (Z: рамка или
// щелчок, Ctrl — уменьшить), цикл зума (Shift+Z), Лупа (панель 200–800 %), Панорамирование и масштаб (панель
// с миниатюрой и рамкой), Снимок (X: область → PNG в буфер, Alt — 300 dpi; однократный). Выбор объектов
// (план 2.2b; эталон 3.4 «Выделение и правка на странице»; общий для этапов 3–6) — аннотации страницы:
// щелчок, Shift, рамка, перемещение и масштаб ручками, Del.
#pragma once

#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "core/annot/annots.h"
#include "core/text/selection.h"
#include "core/util/geometry.h"
#include "ui/view/tools/tool_panels.h"

namespace ot {
class TextPage;
}  // namespace ot

namespace ot::ui {

class PageView;
class ToolController;

enum class ViewToolId { Select, Hand, MarqueeZoom, Loupe, PanZoom, Snapshot, AnnotSelect };
inline constexpr int kViewToolCount = 7;

class ViewTool {
public:
    ViewTool(PageView& view, ToolController& controller) : view_(view), controller_(controller) {}
    virtual ~ViewTool() = default;
    ViewTool(const ViewTool&) = delete;
    ViewTool& operator=(const ViewTool&) = delete;

    virtual ViewToolId id() const noexcept = 0;
    virtual QString title() const = 0;
    // Однократный: после действия — назад к прежнему инструменту, если не «закреплён» (comment-42).
    virtual bool oneShot() const noexcept { return false; }

    virtual void activate() {}
    virtual void deactivate() {}
    // true — событие обработано.
    virtual bool mousePress(QMouseEvent* /*event*/) { return false; }
    virtual bool mouseMove(QMouseEvent* /*event*/) { return false; }
    virtual bool mouseRelease(QMouseEvent* /*event*/) { return false; }
    virtual bool mouseDoubleClick(QMouseEvent* event) { return mousePress(event); }
    virtual bool keyPress(QKeyEvent* /*event*/) { return false; }
    // Поверх вьюпорта (рамка, область лупы).
    virtual void paintOverlay(QPainter& /*painter*/) {}
    // Курсор над точкой вьюпорта (с модификаторами).
    virtual QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const = 0;
    // Esc: бросить незавершённое действие. true — было что бросать.
    virtual bool cancel() { return false; }
    // Esc после cancel(): true — инструмент поглотил Esc (бросил действие, снял выбор) и остаётся текущим.
    virtual bool escape(bool /*cancelled*/) { return false; }

protected:
    PageView& view_;
    ToolController& controller_;
};

// ---------------------------------------------------------------- Рука

class HandTool : public ViewTool {
public:
    using ViewTool::ViewTool;
    ViewToolId id() const noexcept override { return ViewToolId::Hand; }
    QString title() const override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    bool dragging() const noexcept { return dragging_; }

private:
    bool dragging_ = false;
    QPointF start_;
    QPointF startScroll_;
};

// ---------------------------------------------------------------- Выделение

class SelectTool : public ViewTool {
public:
    using ViewTool::ViewTool;
    ViewToolId id() const noexcept override { return ViewToolId::Select; }
    QString title() const override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool mouseDoubleClick(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;

    // Текстовый слой страницы: загружен — указатель, нет — запрос в сессию (придёт позже) и nullptr.
    const TextPage* textPage(int page);
    bool hasTextPage(int page) const { return pages_.contains(page); }
    void selectAll();
    void clearSelection();
    bool hasSelection() const noexcept { return !selection_.empty(); }
    // Выделение по страницам (по возрастанию) и его текст (страницы — через «\n»).
    const std::map<int, TextSelection>& selection() const noexcept { return selection_; }
    QString selectedText() const;
    // Сбросить слои (документ сменился или правка страницы).
    void forgetPages();
    void forgetPage(int page);

private:
    struct Point {
        int page = -1;
        QPointF pagePoint;  // пункты страницы
    };
    Point locate(QPointF viewportPoint) const;
    void startAt(QMouseEvent* event, int clicks);
    void recompute();
    void publish();
    void textArrived(int page);

    bool pressing_ = false;
    bool rectangle_ = false;  // Alt — столбцы
    bool selectAllPending_ = false;
    SnapMode snap_ = SnapMode::Chars;
    Point anchor_;
    Point focus_;
    QPointF rectStart_;  // вьюпорт
    QPointF rectEnd_;
    int clicks_ = 0;
    QElapsedTimer lastClick_;
    QPointF lastClickPos_;
    QHash<int, std::shared_ptr<const TextPage>> pages_;
    QSet<int> requested_;
    std::map<int, TextSelection> selection_;
};

// ---------------------------------------------------------------- Рамка (общая для зума и снимка)

class RubberBandTool : public ViewTool {
public:
    using ViewTool::ViewTool;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    bool cancel() override;
    std::optional<QRectF> band() const;

protected:
    // Рамка готова (rect — вьюпорт; меньше порога перетаскивания — щелчок: пустой rect, точка — point).
    virtual void finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers modifiers) = 0;

private:
    bool pressing_ = false;
    QPointF start_;
    QPointF end_;
};

class MarqueeZoomTool : public RubberBandTool {
public:
    using RubberBandTool::RubberBandTool;
    ViewToolId id() const noexcept override { return ViewToolId::MarqueeZoom; }
    QString title() const override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;

protected:
    void finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers modifiers) override;
};

class SnapshotTool : public RubberBandTool {
public:
    using RubberBandTool::RubberBandTool;
    ViewToolId id() const noexcept override { return ViewToolId::Snapshot; }
    QString title() const override;
    bool oneShot() const noexcept override { return true; }
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool busy() const noexcept { return busy_; }

protected:
    void finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers modifiers) override;

private:
    bool busy_ = false;
};

// ---------------------------------------------------------------- Лупа, панорамирование

class LoupeTool : public ViewTool {
public:
    static constexpr int kMinPercent = 200;
    static constexpr int kMaxPercent = 800;
    static constexpr int kDefaultPercent = 400;

    LoupeTool(PageView& view, ToolController& controller);
    ~LoupeTool() override;
    ViewToolId id() const noexcept override { return ViewToolId::Loupe; }
    QString title() const override;
    void activate() override;
    void deactivate() override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;

    LoupePanel* panel() const noexcept { return panel_; }
    int percent() const noexcept { return percent_; }
    void setPercent(int percent);
    // Область лупы: страница и рамка в пунктах страницы; нет — пусто.
    int page() const noexcept { return page_; }
    QRectF area() const;
    void setCenter(int page, QPointF pagePoint);
    // Документ сменился — области нет; правка страницы области — растр заново.
    void documentChanged();
    void pageChanged(int page);

private:
    void refresh();
    void imageArrived(const QImage& image, quint64 request);

    QPointer<LoupePanel> panel_;
    int percent_ = kDefaultPercent;
    int page_ = -1;
    QPointF center_;
    bool dragging_ = false;
    QPointF dragOffset_;
    quint64 request_ = 0;
};

class PanZoomTool : public ViewTool {
public:
    PanZoomTool(PageView& view, ToolController& controller);
    ~PanZoomTool() override;
    ViewToolId id() const noexcept override { return ViewToolId::PanZoom; }
    QString title() const override;
    void activate() override;
    void deactivate() override;
    // На самой странице — как рука.
    bool mousePress(QMouseEvent* event) override { return hand_.mousePress(event); }
    bool mouseMove(QMouseEvent* event) override { return hand_.mouseMove(event); }
    bool mouseRelease(QMouseEvent* event) override { return hand_.mouseRelease(event); }
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override {
        return hand_.cursor(point, modifiers);
    }
    PanZoomPanel* panel() const noexcept { return panel_; }
    // Миниатюра и рамка заново (документ или страница сменились).
    void refresh() {
        if (refresh_) {
            refresh_();
        }
    }

private:
    HandTool hand_;
    QPointer<PanZoomPanel> panel_;
    std::function<void()> refresh_;
};

// ---------------------------------------------------------------- Выбор объектов (аннотаций)

// Аннотации страницы — только через минимальное ядро 2.2 (services/annot_ops.h: annotAt, list, setRect,
// remove — операции журнала); вид хранит AnnotRef{page, objnum} и рамки-значения, не указатели MuPDF.
// Щелчок — верхняя аннотация под курсором (annotAt), Shift+щелчок — добавить/убрать, рамка по пустому месту —
// все, что она задевает (Shift — добавить); выбранные тянутся вместе (одна операция журнала), у одной
// выбранной — восемь ручек масштаба; Del — удалить выбранные; правая кнопка — контекстное меню, которое
// наполняют инструменты (сигнал ToolController::annotContextMenu; пустое не показывается — 5.1/6.3). Рамки
// выбранных — слой рамок аннотаций вида, ручки и рамка выбора — слой инструмента. Разметка (квады), Ink и
// вершины линий получают свой хит-тест и правку в 5.1.
class AnnotSelectTool : public ViewTool {
public:
    using AnnotInfo = annot::AnnotInfo;
    using AnnotRef = annot::AnnotRef;
    // Ручки рамки: углы и середины сторон по часовой стрелке от левого верхнего (вьюпорт).
    enum Handle { TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, kHandleCount };

    using ViewTool::ViewTool;
    ViewToolId id() const noexcept override { return ViewToolId::AnnotSelect; }
    QString title() const override;
    void activate() override;
    void deactivate() override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    bool escape(bool cancelled) override;

    // Выбранные (в порядке выбора) и их рамки в пунктах страницы.
    const std::vector<AnnotInfo>& selection() const noexcept { return selected_; }
    bool isSelected(const AnnotRef& ref) const;
    void setSelection(std::vector<AnnotInfo> annots);
    void clearSelection();
    // Del: удалить выбранные одной операцией журнала.
    void deleteSelection();
    // Идёт перетаскивание, рамка выбора или ждём хит-тест.
    bool dragging() const noexcept { return drag_ != Drag::None; }
    // Рамки выбранных на экране (с предпросмотром перетаскивания) и ручка под точкой вьюпорта.
    std::vector<QRectF> frames() const;
    std::optional<Handle> handleAt(QPointF viewportPoint) const;
    // Аннотации страницы (для рамки выбора): загружены — указатель, нет — запрос и nullptr.
    const std::vector<AnnotInfo>* annotList(int page);
    bool hasAnnotList(int page) const { return lists_.contains(page); }
    // Документ сменился — выбора нет; правка страницы — рамки выбранных на ней заново (undo тоже).
    void documentChanged();
    void pageChanged(int page);

private:
    enum class Drag { None, Pending, Move, Resize, Marquee };
    struct Press {
        QPointF viewport;
        QPoint global;
        int page = -1;
        Qt::MouseButton button = Qt::NoButton;
        Qt::KeyboardModifiers modifiers;
    };

    void hitArrived(const std::optional<AnnotInfo>& hit, quint64 request);
    void listArrived(int page);
    std::optional<QRectF> frameOf(const AnnotInfo& info) const;
    bool selectedAt(QPointF viewportPoint) const;
    bool moved() const;
    // Рамка аннотации после перетаскивания до current_ (пункты страницы).
    RectF draggedRect(const AnnotInfo& info) const;
    void finishDrag();
    void finishMarquee();
    bool applyBands();
    void showContextMenu(QPoint global);
    void publish();
    void selectionChanged();

    std::vector<AnnotInfo> selected_;
    Drag drag_ = Drag::None;
    Handle handle_ = TopLeft;
    Press press_;
    QPointF current_;
    bool pressing_ = false;
    quint64 request_ = 0;
    quint64 epoch_ = 0;  // смена документа делает старые ответы устаревшими
    QHash<int, std::vector<AnnotInfo>> lists_;
    QHash<int, quint64> listGeneration_;  // правка страницы делает ответы старше неё устаревшими
    QSet<int> listRequested_;
    QSet<int> refresh_;           // страницы, где рамки выбранных ждут нового списка
    std::map<int, RectF> bands_;  // рамка выбора по страницам (пункты), ждёт списков
};

}  // namespace ot::ui
