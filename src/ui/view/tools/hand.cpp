// «Рука» (H; пробел — временно): перетаскивание страницы — прокрутка вида.
#include <QCoreApplication>
#include <QScrollBar>

#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

QString HandTool::title() const { return QCoreApplication::translate("ot::ui::HandTool", "Рука"); }

bool HandTool::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    dragging_ = true;
    start_ = event->position();
    startScroll_ = QPointF(view_.horizontalScrollBar()->value(), view_.verticalScrollBar()->value());
    return true;
}

bool HandTool::mouseMove(QMouseEvent* event) {
    if (!dragging_) {
        return false;
    }
    const QPointF delta = event->position() - start_;
    view_.horizontalScrollBar()->setValue(qRound(startScroll_.x() - delta.x()));
    view_.verticalScrollBar()->setValue(qRound(startScroll_.y() - delta.y()));
    return true;
}

bool HandTool::mouseRelease(QMouseEvent* event) {
    if (!dragging_ || event->button() != Qt::LeftButton) {
        return false;
    }
    dragging_ = false;
    return true;
}

QCursor HandTool::cursor(QPointF /*point*/, Qt::KeyboardModifiers /*modifiers*/) const {
    return QCursor(dragging_ ? Qt::ClosedHandCursor : Qt::OpenHandCursor);
}

bool HandTool::cancel() {
    const bool was = dragging_;
    dragging_ = false;
    return was;
}

}  // namespace ot::ui
