// Инструменты просмотра PageView (план 2.2; эталон 3.1 «Масштаб», 3.2 «Выделение», 6.8; view-05, view-33,
// comment-42): состояние мыши и клавиатуры одного инструмента — ViewTool, переключение — ToolController
// (ui/view/tool_controller.h). Рука (H; пробел — временно), Выделение (V: текст по строкам, Alt — столбцы,
// двойной/тройной щелчок — слово/строка, Ctrl+A, I-курсор над текстом), Увеличение области (Z: рамка или
// щелчок, Ctrl — уменьшить), цикл зума (Shift+Z), Лупа (панель 200–800 %), Панорамирование и масштаб (панель
// с миниатюрой и рамкой), Снимок (X: область → PNG в буфер, Alt — 300 dpi; однократный).
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

#include "core/text/selection.h"
#include "core/util/geometry.h"
#include "ui/view/tools/tool_panels.h"

namespace ot {
class TextPage;
}  // namespace ot

namespace ot::ui {

class PageView;
class ToolController;

enum class ViewToolId { Select, Hand, MarqueeZoom, Loupe, PanZoom, Snapshot };
inline constexpr int kViewToolCount = 6;

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

}  // namespace ot::ui
