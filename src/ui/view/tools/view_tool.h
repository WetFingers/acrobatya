// Инструменты просмотра PageView (план 2.2; эталон 3.1 «Масштаб», 3.2 «Выделение», 6.8; view-05, view-33,
// comment-42): состояние мыши и клавиатуры одного инструмента — ViewTool, переключение — ToolController
// (ui/view/tool_controller.h). Рука (H; пробел — временно), Выделение (V: текст по строкам, Alt — столбцы,
// двойной/тройной щелчок — слово/строка, Ctrl+A, I-курсор над текстом), Увеличение области (Z: рамка или
// щелчок, Ctrl — уменьшить), цикл зума (Shift+Z), Лупа (панель 200–800 %), Панорамирование и масштаб (панель
// с миниатюрой и рамкой), Снимок (X: область → PNG в буфер, Alt — 300 dpi; однократный). Выбор объектов
// (план 2.2b; эталон 3.4 «Выделение и правка на странице»; общий для этапов 3–6) — аннотации страницы:
// щелчок, Shift, рамка, перемещение и масштаб ручками, Del. Инструменты правки (3.4: «Редактировать PDF»,
// дальше — комментарии, формы) живут в своих папках ui/tools/<папка>/ и ставятся в слот ToolController
// (install) при первом включении на виде; им — клавиатура раньше ярлыков окна и ввод IME.
#pragma once

#include <QCursor>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QVariant>

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

// Edit — слот инструмента «Редактировать PDF» (3.4): экземпляр ставит EditTool (ToolController::install);
// Sign — рамка подписи «Использовать сертификат» (4.6: SignPlacementTool ставит CertificateTool); Comment —
// инструменты «Добавить комментарии» (5.1: CommentMode ставит CommentTool); FillSign — «Заполнить и
// подписать» (5.3); Measure — «Измерить» (5.5: MeasureMode ставит MeasureTool).
enum class ViewToolId {
    Select,
    Hand,
    MarqueeZoom,
    Loupe,
    PanZoom,
    Snapshot,
    AnnotSelect,
    Edit,
    Sign,
    Comment,
    FillSign,
    Measure
};
inline constexpr int kViewToolCount = 12;

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

    // Документ вида сменился / страница изменена правкой (инструменты из слотов ToolController::install).
    virtual void documentChanged() {}
    virtual void pageChanged(int /*page*/) {}
    // Набор текста (3.4): true — клавиши сначала инструменту (пробел, Esc, однобуквенные — тоже его), а те,
    // что нужны ему (wantsShortcut), — и раньше ярлыков окна (Ctrl+B, Del, Home…).
    virtual bool capturesKeyboard() const { return false; }
    virtual bool wantsShortcut(const QKeyEvent* /*event*/) const { return false; }
    // Метод ввода (IME: составные символы, рукописный ввод): включён у вида, пока acceptsInputMethod();
    // запросы — координаты вьюпорта (ImCursorRectangle).
    virtual bool acceptsInputMethod() const { return false; }
    virtual bool inputMethodEvent(QInputMethodEvent* /*event*/) { return false; }
    virtual QVariant inputMethodQuery(Qt::InputMethodQuery /*query*/) const { return {}; }

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
    // Ctrl+C: выделенное — в буфер обмена text/plain + text/html (services/clipboard_io.h). false — нечего.
    bool copySelection();

    // Клавиатурный курсор выделения (ocr-41; эталон 3.2, 6.9): граница символа на странице. Ставит щелчок,
    // сдвигает Shift+←/→ (символ), Shift+↑/↓ (строка) — выделение от начала до курсора; виден, пока у вида
    // фокус. Рамка курсора — пункты страницы (нет — пусто).
    struct Caret {
        int page = -1;
        int boundary = 0;       // 0…chars().size(): граница перед символом boundary
        bool trailing = false;  // показывать после символа boundary − 1 (конец строки)
        bool valid() const noexcept { return page >= 0; }
        bool operator==(const Caret& other) const noexcept {
            return page == other.page && boundary == other.boundary;
        }
    };
    const Caret& caret() const noexcept { return caret_; }
    std::optional<RectF> caretRect() const;
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
    // Курсор — по выделению мышью (конец протягивания) или по точке щелчка.
    void syncCaret();
    bool moveCaret(int key);
    Caret stepCaret(const Caret& from, int delta);
    Caret stepCaretLine(const Caret& from, int delta);
    void selectBetween(const Caret& a, const Caret& b);

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
    Caret caret_;
    Caret keyAnchor_;        // начало выделения клавишами
    bool keyboard_ = false;  // выделение ведут клавиши (от keyAnchor_ до caret_)
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
    // Двойной щелчок по выбранной — сигнал ToolController::annotDoubleClicked (правка поля, окно записки).
    bool mouseDoubleClick(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    bool escape(bool cancelled) override;

    // Буфер обмена (5.1, comment-43): копия выбранных — свой PDF и JSON ядра (annot::copy) в формате
    // kClipboardMime; вставка — на текущую страницу в исходные координаты (на ту же страницу того же
    // документа — со сдвигом kPasteOffset), вставленные становятся выбранными.
    static constexpr const char* kClipboardMime = "application/x-acrobatya-annotations";
    static constexpr float kPasteOffset = 12;  // пт
    void copySelection();
    void cutSelection();
    bool canPaste() const;
    void paste();
    // Тянуть и масштабировать можно не всё: разметка текста привязана к символам, «Заблокировано» (/F Locked)
    // не двигается и не удаляется.
    static bool movable(const AnnotInfo& info) noexcept;
    // Заблокировать / разблокировать выбранные (/F Locked) одним шагом.
    void setSelectionLocked(bool locked);
    // Скрытые подтипы (показ/скрытие комментариев вкладки, 5.1): не выбираются, выбранные снимаются.
    void setHiddenTypes(std::uint32_t mask);
    bool hiddenType(annot::AnnotType type) const noexcept;
    // Скрытые фильтром панели «Комментарии» (5.4): номера объектов по страницам — как скрытые подтипы.
    void setHiddenAnnots(const std::map<int, std::vector<int>>& byPage);
    bool hiddenAnnot(const AnnotInfo& info) const;
    // Контекстное меню выбранных: Копировать, Вырезать, Удалить, Заблокировать — затем пункты инструментов
    // (сигнал ToolController::annotContextMenu). Правая кнопка показывает его, тест — заполняет.
    void fillContextMenu(QMenu& menu);
    bool wantsShortcut(const QKeyEvent* event) const override;

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
    // Вершины и концы одной выбранной (5.2, comment-05, comment-07): линия — два конца, многоугольник и
    // ломаная — вершины (вместо ручек рамки), выноска FreeText — конец стрелки и колено; маркеры — кружки,
    // перетаскивание (Shift — под 45° к соседней точке) — правка геометрии шагом «Move vertex». Пункты
    // страницы выбранной; пусто — маркеров нет (или ещё не прочитаны).
    const std::vector<QPointF>& vertexPoints() const noexcept { return vertexPoints_; }
    std::optional<int> vertexAt(QPointF viewportPoint) const;
    // Поворот штампа (5.2, comment-11): у одной выбранной /Stamp — маркер над серединой верхней стороны рамки
    // (вьюпорт; нет — угол штампа ещё не прочитан или его нельзя двигать); перетаскивание — поворот вокруг
    // центра рамки (Shift — шагом 15°), по отпусканию — шаг «Rotate stamp» (annot::setStampRotation). Угол —
    // /Rotate штампа, градусы по часовой.
    std::optional<QPointF> rotateHandle() const;
    std::optional<float> stampAngle() const { return stampAngle_; }
    // Аннотации страницы (для рамки выбора): загружены — указатель, нет — запрос и nullptr.
    const std::vector<AnnotInfo>* annotList(int page);
    bool hasAnnotList(int page) const { return lists_.contains(page); }
    // Документ сменился — выбора нет; правка страницы — рамки выбранных на ней заново (undo тоже).
    void documentChanged();
    void pageChanged(int page);

private:
    enum class Drag { None, Pending, Move, Resize, Marquee, Vertex, Rotate };
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
    void selectPasted(int page, std::vector<AnnotRef> refs);
    void loadVertices(bool retry = false);
    QPointF draggedVertex() const;
    void finishVertex();
    void loadStampAngle();
    double rotateDelta() const;
    void finishRotate();

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
    std::uint32_t hiddenTypes_ = 0;
    std::map<int, std::vector<int>> hiddenAnnots_;  // по возрастанию
    // Маркеры вершин (5.2).
    std::vector<QPointF> vertexPoints_;
    std::vector<PointF> callout_;  // выноска FreeText целиком (3 точки)
    annot::AnnotType vertexType_ = annot::AnnotType::Unknown;
    AnnotRef vertexRef_;
    int vertex_ = -1;
    quint64 vertexRequest_ = 0;
    int vertexTries_ = 0;
    bool vertexShift_ = false;
    // Поворот штампа (5.2).
    std::optional<float> stampAngle_;
    AnnotRef stampRef_;
    quint64 stampRequest_ = 0;
    bool rotateShift_ = false;
};

}  // namespace ot::ui
