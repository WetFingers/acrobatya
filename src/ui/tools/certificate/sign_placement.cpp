#include "ui/tools/certificate/sign_placement.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>

#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

SignPlacementTool::SignPlacementTool(PageView& view, ToolController& controller)
    : RubberBandTool(view, controller) {}

QString SignPlacementTool::title() const {
    return QCoreApplication::translate("ot::ui::SignPlacementTool", "Рамка подписи");
}

void SignPlacementTool::activate() {
    done_ = false;
    view_.viewport()->update();
}

void SignPlacementTool::deactivate() {
    view_.viewport()->update();
    if (!done_) {
        emit cancelled();
    }
    done_ = true;
}

void SignPlacementTool::setFields(std::vector<sign::SignatureField> fields) {
    fields_ = std::move(fields);
    view_.viewport()->update();
}

const sign::SignatureField* SignPlacementTool::fieldAt(QPointF viewportPoint) const {
    const int page = view_.pageAtViewport(viewportPoint);
    if (page < 0) {
        return nullptr;
    }
    const QPointF p = view_.viewportToPage(page, viewportPoint);
    return sign::emptyFieldAt(fields_, page, {static_cast<float>(p.x()), static_cast<float>(p.y())});
}

QCursor SignPlacementTool::cursor(QPointF point, Qt::KeyboardModifiers /*modifiers*/) const {
    return fieldAt(point) != nullptr ? QCursor(Qt::PointingHandCursor) : QCursor(Qt::CrossCursor);
}

void SignPlacementTool::paintOverlay(QPainter& painter) {
    // Пустые поля подписи — пунктиром цвета полей формы: щелчок по ним подписывает поле.
    const Palette& c = Theme::colors();
    QPen pen(c.accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
    pen.setCosmetic(true);
    for (const sign::SignatureField& f : fields_) {
        if (!f.visible || f.isSigned || f.readOnly || f.page < 0 || f.page >= view_.pageCount()) {
            continue;
        }
        const QRectF onPage(QPointF(f.rect.x0, f.rect.y0), QPointF(f.rect.x1, f.rect.y1));
        const QRectF r = view_.pageTransform(f.page).mapRect(onPage);
        if (!r.intersects(QRectF(view_.viewport()->rect()))) {
            continue;
        }
        painter.setPen(pen);
        painter.setBrush(c.formField.color());
        painter.drawRect(r);
    }
    RubberBandTool::paintOverlay(painter);
}

void SignPlacementTool::place(int page, const RectF& rect, const QString& field) {
    done_ = true;
    emit placed(page, rect, field);
    controller_.finished();
}

void SignPlacementTool::finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers /*modifiers*/) {
    if (!rect.isEmpty()) {
        const int page = view_.pageAtViewport(rect.center());
        if (page >= 0) {
            const QRectF clipped = rect.intersected(view_.pageViewportRect(page));
            const QRectF onPage = view_.pageTransform(page).inverted().mapRect(clipped);
            if (onPage.width() >= kMinSide && onPage.height() >= kMinSide) {
                place(page,
                      RectF{static_cast<float>(onPage.left()), static_cast<float>(onPage.top()),
                            static_cast<float>(onPage.right()), static_cast<float>(onPage.bottom())},
                      {});
                return;
            }
        }
    }
    // Щелчок (или слишком маленькая рамка): пустое поле подписи под точкой.
    if (const sign::SignatureField* f = fieldAt(point)) {
        place(f->page, f->rect, QString::fromStdString(f->name));
        return;
    }
    emit missed();
}

}  // namespace ot::ui
