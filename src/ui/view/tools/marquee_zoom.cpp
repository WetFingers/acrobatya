// «Увеличение области» (Z; эталон 3.1 «Масштаб»): рамка — область во всё окно (по меньшей из сторон, по
// центру); щелчок — шаг масштаба вверх в точке, с Ctrl — вниз. Рамка мыши (RubberBandTool) — общая с
// «Снимком».
#include <QApplication>
#include <QScrollBar>

#include <algorithm>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

// ---------------------------------------------------------------- RubberBandTool

bool RubberBandTool::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    pressing_ = true;
    start_ = end_ = event->position();
    return true;
}

bool RubberBandTool::mouseMove(QMouseEvent* event) {
    if (!pressing_) {
        return false;
    }
    end_ = event->position();
    view_.viewport()->update();
    return true;
}

bool RubberBandTool::mouseRelease(QMouseEvent* event) {
    if (!pressing_ || event->button() != Qt::LeftButton) {
        return false;
    }
    pressing_ = false;
    end_ = event->position();
    view_.viewport()->update();
    const QRectF rect = QRectF(start_, end_).normalized();
    const bool click = (end_ - start_).manhattanLength() < QApplication::startDragDistance();
    finish(click ? QRectF() : rect, end_, event->modifiers());
    return true;
}

std::optional<QRectF> RubberBandTool::band() const {
    if (!pressing_) {
        return std::nullopt;
    }
    return QRectF(start_, end_).normalized();
}

void RubberBandTool::paintOverlay(QPainter& painter) {
    const auto rect = band();
    if (!rect || rect->isEmpty()) {
        return;
    }
    QPen pen(Theme::colors().accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Theme::colors().selection.color());
    painter.drawRect(*rect);
}

bool RubberBandTool::cancel() {
    if (!pressing_) {
        return false;
    }
    pressing_ = false;
    view_.viewport()->update();
    return true;
}

// ---------------------------------------------------------------- MarqueeZoomTool

QString MarqueeZoomTool::title() const {
    return QCoreApplication::translate("ot::ui::MarqueeZoomTool", "Увеличение области");
}

QCursor MarqueeZoomTool::cursor(QPointF /*point*/, Qt::KeyboardModifiers modifiers) const {
    const int size = Theme::sizes().iconPanel;
    const bool out = (modifiers & Qt::ControlModifier) != 0;
    const QPixmap icon = Icons::get(out ? QStringLiteral("zoom-out") : QStringLiteral("zoom-in"),
                                    IconTone::Text, size, view_.devicePixelRatioF());
    // Горячая точка — центр линзы (у Lucide zoom-in круг с центром в (11, 11) из 24).
    return QCursor(icon, size * 11 / 24, size * 11 / 24);
}

void MarqueeZoomTool::finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers modifiers) {
    if (rect.isEmpty()) {
        if ((modifiers & Qt::ControlModifier) != 0) {
            view_.zoomOut(point);
        } else {
            view_.zoomIn(point);
        }
        controller_.finished();
        return;
    }
    // Рамка — во всё окно: масштаб по меньшей стороне, центр рамки — в центр окна.
    const QSizeF viewport(view_.viewport()->size());
    const double k = std::min(viewport.width() / rect.width(), viewport.height() / rect.height());
    const QPointF center = rect.center();
    view_.setZoom(static_cast<float>(view_.zoom() * k), center);
    const QPointF shift = center - QPointF(viewport.width() / 2.0, viewport.height() / 2.0);
    view_.horizontalScrollBar()->setValue(view_.horizontalScrollBar()->value() + qRound(shift.x()));
    view_.verticalScrollBar()->setValue(view_.verticalScrollBar()->value() + qRound(shift.y()));
    controller_.finished();
}

}  // namespace ot::ui
