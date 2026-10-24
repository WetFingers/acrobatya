// ToolController — инструменты просмотра PageView (план 2.2; view-05, view-33, comment-42): один текущий
// ViewTool (ui/view/tools/view_tool.h) получает мышь и клавиши вида и рисует свой слой поверх страниц
// (OverlayStack). «Закрепить» (comment-42): однократный инструмент (Снимок; инструменты комментариев 5.x)
// после действия возвращает прежний, если не закреплён. Однобуквенные клавиши (H, V, Z, Shift+Z, X; эталон
// 6.8) выключены по умолчанию — включает настройка. Пробел держит «Руку» временно, Esc бросает действие и
// возвращает «Выделение» (инструмент, который поглотил Esc — ViewTool::escape, — остаётся). «Выбор объектов»
// (AnnotSelectTool, 2.2b) — общий выбор аннотаций для инструментов этапов 3–6. Ссылки (2.2b): у «Выделения»,
// «Руки» и «Панорамирования» над ссылкой — курсор-указатель, щелчок без сдвига — PageView::activateLink.
#pragma once

#include <QImage>
#include <QMenu>
#include <QObject>

#include <array>
#include <memory>
#include <optional>

#include "core/links/links.h"
#include "ui/view/overlay.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

class PageView;

class ToolController : public QObject, public Overlay {
    Q_OBJECT
public:
    explicit ToolController(PageView& view);
    ~ToolController() override;

    ViewToolId tool() const noexcept { return current_; }
    ViewTool& current() const { return *tools_[static_cast<std::size_t>(current_)]; }
    ViewTool& toolFor(ViewToolId id) const { return *tools_[static_cast<std::size_t>(id)]; }
    SelectTool& select() const { return static_cast<SelectTool&>(toolFor(ViewToolId::Select)); }
    LoupeTool& loupe() const { return static_cast<LoupeTool&>(toolFor(ViewToolId::Loupe)); }
    PanZoomTool& panZoom() const { return static_cast<PanZoomTool&>(toolFor(ViewToolId::PanZoom)); }
    AnnotSelectTool& annotSelect() const {
        return static_cast<AnnotSelectTool&>(toolFor(ViewToolId::AnnotSelect));
    }
    static constexpr ViewToolId defaultTool() noexcept { return ViewToolId::Select; }
    // Инструмент правки в своём слоте (3.4: ViewToolId::Edit — «Редактировать PDF»): ставит инструмент
    // ToolRegistry при первом включении на виде, тот же id заменяет прежний; пустой слот setTool не включает.
    ViewTool* install(std::unique_ptr<ViewTool> tool);
    ViewTool* find(ViewToolId id) const { return tools_[static_cast<std::size_t>(id)].get(); }
    // Инструмент начал или кончил принимать ввод IME (acceptsInputMethod) — у вида включается метод ввода.
    void inputMethodChanged();

    void setTool(ViewToolId id);
    // Shift+Z: Увеличение области → Лупа → Панорамирование и масштаб → Увеличение области.
    void cycleZoomTools();
    // Esc: бросить действие; не «Выделение» — вернуть его, иначе снять выделение.
    void reset();
    // Однократный инструмент сделал своё: назад к прежнему, если не закреплён.
    void finished();

    bool pinned() const noexcept { return pinned_; }
    void setPinned(bool pinned);
    // Документ можно править (2.5: PDF/A в режиме просмотра, не PDF — нет): «Выбор объектов» не двигает и не
    // удаляет пометки.
    bool editable() const noexcept { return editable_; }
    void setEditable(bool on) noexcept { editable_ = on; }
    // Копирование текста и «Снимок» в буфер (2.5b): право /P «копирование» снято — Ctrl+C и снимок ничего не
    // кладут в буфер.
    bool copyAllowed() const noexcept { return copyAllowed_; }
    void setCopyAllowed(bool on) noexcept { copyAllowed_ = on; }
    bool singleKeyShortcuts() const noexcept { return singleKeys_; }
    void setSingleKeyShortcuts(bool on) noexcept { singleKeys_ = on; }

    // Документ вида сменился / страница изменена правкой: слои текста, области лупы и миниатюры — заново.
    void documentChanged();
    void pageChanged(int page);

    // События вида (координаты вьюпорта). true — обработано.
    bool mousePress(QMouseEvent* event);
    bool mouseMove(QMouseEvent* event);
    bool mouseRelease(QMouseEvent* event);
    bool mouseDoubleClick(QMouseEvent* event);
    bool keyPress(QKeyEvent* event);
    bool keyRelease(QKeyEvent* event);
    bool inputMethodEvent(QInputMethodEvent* event);
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const;
    void updateCursor(QPointF point, Qt::KeyboardModifiers modifiers,
                      Qt::MouseButtons buttons = Qt::NoButton);
    // Текущий инструмент ходит по ссылкам (Выделение, Рука, Панорамирование).
    bool followsLinks() const noexcept;

    void paintViewport(QPainter& painter, const PageView& view) override;

    // Для инструментов.
    void emitSnapshot(const QImage& image) { emit snapshotTaken(image); }
    void emitSelectionChanged() { emit selectionChanged(); }
    void emitAnnotSelectionChanged() { emit annotSelectionChanged(); }
    void emitAnnotContextMenu(QMenu* menu) { emit annotContextMenu(menu); }
    void emitAnnotDoubleClicked(const ot::annot::AnnotInfo& info) { emit annotDoubleClicked(info); }

signals:
    void toolChanged(ot::ui::ViewToolId id);
    void pinnedChanged(bool pinned);
    void snapshotTaken(const QImage& image);
    void selectionChanged();
    // «Выбор объектов»: набор выбранных аннотаций сменился.
    void annotSelectionChanged();
    // Правая кнопка по выбранным аннотациям: подписчики (инструменты 5.1, 6.3) добавляют пункты в menu
    // (прямым соединением, до показа); пустое меню не показывается.
    void annotContextMenu(QMenu* menu);
    // Двойной щелчок «Выбором объектов» по выбранной аннотации (5.2): поле FreeText — правка текста, записка
    // — окно.
    void annotDoubleClicked(const ot::annot::AnnotInfo& info);

private:
    void switchTo(ViewToolId id, bool remember);

    PageView& view_;
    std::array<std::unique_ptr<ViewTool>, kViewToolCount> tools_;
    ViewToolId current_ = defaultTool();
    ViewToolId previous_ = defaultTool();
    std::optional<ViewToolId> beforeSpace_;     // пробел — временная «Рука»
    std::optional<links::LinkInfo> linkPress_;  // нажатие над ссылкой: отпустили на месте — переход
    QPointF linkPressPos_;
    bool pinned_ = false;
    bool singleKeys_ = false;
    bool editable_ = true;
    bool copyAllowed_ = true;
};

}  // namespace ot::ui
