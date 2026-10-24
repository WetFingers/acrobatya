#include "ui/view/tool_controller.h"

#include <QApplication>
#include <QCursor>
#include <QInputMethod>

#include "ui/view/link_layer.h"
#include "ui/view/page_view.h"

namespace ot::ui {

ToolController::ToolController(PageView& view) : QObject(&view), view_(view) {
    tools_[static_cast<std::size_t>(ViewToolId::Select)] = std::make_unique<SelectTool>(view, *this);
    tools_[static_cast<std::size_t>(ViewToolId::Hand)] = std::make_unique<HandTool>(view, *this);
    tools_[static_cast<std::size_t>(ViewToolId::MarqueeZoom)] =
        std::make_unique<MarqueeZoomTool>(view, *this);
    tools_[static_cast<std::size_t>(ViewToolId::Loupe)] = std::make_unique<LoupeTool>(view, *this);
    tools_[static_cast<std::size_t>(ViewToolId::PanZoom)] = std::make_unique<PanZoomTool>(view, *this);
    tools_[static_cast<std::size_t>(ViewToolId::Snapshot)] = std::make_unique<SnapshotTool>(view, *this);
    tools_[static_cast<std::size_t>(ViewToolId::AnnotSelect)] =
        std::make_unique<AnnotSelectTool>(view, *this);
}

ToolController::~ToolController() = default;

ViewTool* ToolController::install(std::unique_ptr<ViewTool> tool) {
    if (!tool) {
        return nullptr;
    }
    const ViewToolId id = tool->id();
    auto& slot = tools_[static_cast<std::size_t>(id)];
    if (slot && current_ == id) {
        switchTo(defaultTool(), false);
    }
    slot = std::move(tool);
    return slot.get();
}

void ToolController::inputMethodChanged() {
    view_.setAttribute(Qt::WA_InputMethodEnabled, current().acceptsInputMethod());
    if (view_.hasFocus()) {
        QGuiApplication::inputMethod()->update(Qt::ImEnabled | Qt::ImCursorRectangle | Qt::ImQueryInput);
    }
}

void ToolController::switchTo(ViewToolId id, bool remember) {
    if (id == current_ || !tools_[static_cast<std::size_t>(id)]) {
        return;
    }
    current().cancel();
    current().deactivate();
    if (remember) {
        previous_ = current_;
    }
    current_ = id;
    current().activate();
    inputMethodChanged();
    view_.viewport()->setCursor(
        current().cursor(view_.viewport()->mapFromGlobal(QCursor::pos()), Qt::NoModifier));
    view_.viewport()->update();
    emit toolChanged(id);
}

void ToolController::setTool(ViewToolId id) {
    beforeSpace_.reset();
    switchTo(id, true);
}

void ToolController::cycleZoomTools() {
    switch (current_) {
        case ViewToolId::MarqueeZoom:
            setTool(ViewToolId::Loupe);
            break;
        case ViewToolId::Loupe:
            setTool(ViewToolId::PanZoom);
            break;
        default:
            setTool(ViewToolId::MarqueeZoom);
            break;
    }
}

void ToolController::reset() {
    const bool cancelled = current().cancel();
    if (current().escape(cancelled)) {
        return;
    }
    if (current_ != defaultTool()) {
        setTool(defaultTool());
        return;
    }
    if (!cancelled) {
        select().clearSelection();
    }
}

void ToolController::finished() {
    if (current().oneShot() && !pinned_) {
        const ViewToolId back = previous_ == current_ ? defaultTool() : previous_;
        switchTo(back, false);
    }
}

void ToolController::setPinned(bool pinned) {
    if (pinned == pinned_) {
        return;
    }
    pinned_ = pinned;
    emit pinnedChanged(pinned);
}

void ToolController::documentChanged() {
    current().cancel();
    select().forgetPages();
    loupe().documentChanged();
    panZoom().refresh();
    annotSelect().documentChanged();
    for (const ViewToolId slot : {ViewToolId::Edit, ViewToolId::Sign, ViewToolId::Comment}) {
        if (ViewTool* tool = find(slot)) {
            tool->documentChanged();
        }
    }
}

void ToolController::pageChanged(int page) {
    select().forgetPage(page);
    loupe().pageChanged(page);
    annotSelect().pageChanged(page);
    for (const ViewToolId slot : {ViewToolId::Edit, ViewToolId::Sign, ViewToolId::Comment}) {
        if (ViewTool* tool = find(slot)) {
            tool->pageChanged(page);
        }
    }
    if (page == view_.currentPage()) {
        panZoom().refresh();
    }
}

bool ToolController::followsLinks() const noexcept {
    return current_ == ViewToolId::Select || current_ == ViewToolId::Hand || current_ == ViewToolId::PanZoom;
}

bool ToolController::mousePress(QMouseEvent* event) {
    linkPress_.reset();
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (event->button() == Qt::LeftButton && mods == Qt::NoModifier && followsLinks()) {
        linkPress_ = view_.linkLayer().linkAt(event->position());
        linkPressPos_ = event->position();
    }
    const bool handled = current().mousePress(event);
    updateCursor(event->position(), event->modifiers(), event->buttons());
    return handled;
}

bool ToolController::mouseMove(QMouseEvent* event) {
    const bool handled = current().mouseMove(event);
    updateCursor(event->position(), event->modifiers(), event->buttons());
    return handled;
}

bool ToolController::mouseRelease(QMouseEvent* event) {
    const std::optional<links::LinkInfo> pressed = std::exchange(linkPress_, std::nullopt);
    const bool handled = current().mouseRelease(event);
    if (pressed && event->button() == Qt::LeftButton && followsLinks() &&
        (event->position() - linkPressPos_).manhattanLength() < QApplication::startDragDistance()) {
        // Щелчок на месте — переход (протянули — это выделение или прокрутка, не ссылка).
        const std::optional<links::LinkInfo> now = view_.linkLayer().linkAt(event->position());
        if (now && now->page == pressed->page && now->objnum == pressed->objnum &&
            now->rect.x0 == pressed->rect.x0 && now->rect.y0 == pressed->rect.y0) {
            view_.activateLink(*pressed);
        }
    }
    updateCursor(event->position(), event->modifiers(), event->buttons());
    return handled;
}

bool ToolController::mouseDoubleClick(QMouseEvent* event) { return current().mouseDoubleClick(event); }

bool ToolController::keyPress(QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    const int key = event->key();
    // Набор текста: клавиши — сначала инструменту (пробел — это пробел, буква — не однобуквенный ярлык).
    const bool captured = current().capturesKeyboard();
    if (captured && current().keyPress(event)) {
        return true;
    }
    if (key == Qt::Key_Escape && mods == Qt::NoModifier) {
        reset();
        return true;
    }
    if (key == Qt::Key_Space && mods == Qt::NoModifier) {
        if (!event->isAutoRepeat() && current_ != ViewToolId::Hand && !beforeSpace_) {
            const ViewToolId before = current_;
            switchTo(ViewToolId::Hand, false);
            beforeSpace_ = before;
        }
        return true;
    }
    if (!captured && current().keyPress(event)) {
        return true;
    }
    if (!singleKeys_ || captured) {
        return false;
    }
    if (mods == Qt::ShiftModifier && key == Qt::Key_Z) {
        cycleZoomTools();
        return true;
    }
    if (mods != Qt::NoModifier) {
        return false;
    }
    switch (key) {
        case Qt::Key_H:
            setTool(ViewToolId::Hand);
            return true;
        case Qt::Key_V:
            setTool(ViewToolId::Select);
            return true;
        case Qt::Key_Z:
            setTool(ViewToolId::MarqueeZoom);
            return true;
        case Qt::Key_X:
            setTool(ViewToolId::Snapshot);
            return true;
        default:
            return false;
    }
}

bool ToolController::keyRelease(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && beforeSpace_) {
        const ViewToolId back = *beforeSpace_;
        beforeSpace_.reset();
        switchTo(back, false);
        return true;
    }
    // Ctrl меняет курсор зума (увеличить ↔ уменьшить) без движения мыши.
    updateCursor(view_.viewport()->mapFromGlobal(QCursor::pos()), event->modifiers());
    return false;
}

bool ToolController::inputMethodEvent(QInputMethodEvent* event) { return current().inputMethodEvent(event); }

QVariant ToolController::inputMethodQuery(Qt::InputMethodQuery query) const {
    return current().inputMethodQuery(query);
}

void ToolController::updateCursor(QPointF point, Qt::KeyboardModifiers modifiers, Qt::MouseButtons buttons) {
    if (buttons == Qt::NoButton && followsLinks() && view_.linkLayer().linkAt(point)) {
        view_.viewport()->setCursor(QCursor(Qt::PointingHandCursor));
        return;
    }
    view_.viewport()->setCursor(current().cursor(point, modifiers));
}

void ToolController::paintViewport(QPainter& painter, const PageView& /*view*/) {
    current().paintOverlay(painter);
}

}  // namespace ot::ui
