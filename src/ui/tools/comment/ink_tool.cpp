// Карандаш (план 5.1; эталон 3.4 «Инструмент»: «Рисовать (D, Ink с толщиной/цветом/сглаживанием)»;
// comment-06): штрих — путь мыши; пока ведётся — оверлей сглаженной кривой (квадратичные Безье через середины
// отрезков) цветом и толщиной стиля инструмента; по отпусканию — точки после Дугласа–Пекера (ε = 0,5 пт,
// core/annot/ink_math.h) в /InkList новой аннотации Ink (шаг «Add ink»). Щелчок без движения — ничего.
#include <QFuture>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

#include "core/annot/ink_math.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

// Масштаб страницы на экране: px на пункт.
double inkScale(const QTransform& toView) { return std::hypot(toView.m11(), toView.m12()); }

}  // namespace

void CommentMode::paintInk(QPainter& painter) {
    if (stroke_.empty()) {
        return;
    }
    const QTransform toView = view_.pageTransform(page_);
    const annot::AnnotProps style = newProps(CommentAction::Ink);
    QColor color = QColor::fromRgb(static_cast<QRgb>(style.color.toRgb()));
    color.setAlphaF(style.opacity);
    QPainterPath path(toView.map(stroke_.front()));
    for (std::size_t i = 1; i + 1 < stroke_.size(); ++i) {
        const QPointF a = toView.map(stroke_[i]);
        const QPointF b = toView.map(stroke_[i + 1]);
        path.quadTo(a, (a + b) / 2);
    }
    path.lineTo(toView.map(stroke_.back()));
    QPen pen(color, std::max(1.0, style.borderWidth * inkScale(toView)), Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

void CommentMode::inkRelease() {
    DocumentSession* s = session();
    if (s == nullptr || stroke_.size() < 2) {
        return;
    }
    annot::Stroke points;
    points.reserve(stroke_.size());
    for (const QPointF& p : stroke_) {
        points.push_back({static_cast<float>(p.x()), static_cast<float>(p.y())});
    }
    annot::Stroke simplified = annot::simplifyStroke(points, annot::kInkSimplifyEpsilon);
    if (simplified.size() < 2 || (simplified.size() == 2 && simplified.front() == simplified.back())) {
        return;  // щелчок на месте
    }
    annot::AnnotProps props = newProps(CommentAction::Ink);
    props.ink = {std::move(simplified)};
    done(annot::create(*s, page_, std::move(props)), false);
}

}  // namespace ot::ui
