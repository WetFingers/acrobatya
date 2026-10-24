// Фигуры (план 5.2, п. 2; эталон 3.4 «Инструмент»: Фигуры (R) — прямоугольник, эллипс, линия, стрелка,
// многоугольник, ломаная, облако /BE; comment-07):
//   * линия, стрелка (Line /IT /LineArrow, конец — открытая стрелка, если стиль инструмента «arrow» не задал
//     иного), прямоугольник (Square), овал (Circle) — протягиванием; щелчок на месте (меньше
//     kDragThresholdPx) — ничего; Shift — линия под углом, кратным 45°, квадрат, круг;
//   * многоугольник (Polygon), облако (Polygon /BE /S /C /I 1, /IT /PolygonCloud), ломаная (PolyLine) —
//   вершина
//     на каждый щелчок, Shift — отрезок под 45°; конец — двойной щелчок, Enter или щелчок по первой вершине
//     (многоугольник и облако), Backspace — убрать последнюю вершину, Esc — отменить фигуру;
//   * стиль — умолчания ядра (красная линия 1 пт) + стиль инструмента: цвет, заливка /IC, толщина,
//     непрозрачность, концы линии /LE (services/comment_settings.h); предпросмотр — тем же цветом и толщиной.
// Каждая фигура — шаг истории (createStepName: «Add line», «Add rectangle»…).
#include <QFuture>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

constexpr double kCloseDistancePx = 6;  // щелчок у первой вершины замыкает многоугольник
constexpr float kMinShapePt = 2;        // меньше — щелчок, не фигура

// Ближайшее из восьми направлений (через 45°) от from к to, длина — проекция.
QPointF snap45(QPointF from, QPointF to) {
    const QPointF d = to - from;
    const double angle = std::atan2(d.y(), d.x());
    constexpr double kStep = std::numbers::pi / 4;
    const double step = std::round(angle / kStep) * kStep;
    const double length = d.x() * std::cos(step) + d.y() * std::sin(step);
    return from + QPointF(length * std::cos(step), length * std::sin(step));
}

// Квадрат (круг) с углом в from в сторону to: сторона — большая из разниц.
QPointF squareCorner(QPointF from, QPointF to) {
    const QPointF d = to - from;
    const double side = std::max(std::fabs(d.x()), std::fabs(d.y()));
    return from + QPointF(d.x() < 0 ? -side : side, d.y() < 0 ? -side : side);
}

PointF pointF(QPointF p) { return {static_cast<float>(p.x()), static_cast<float>(p.y())}; }

}  // namespace

// Конец протягивания с ограничением Shift.
QPointF CommentMode::shapeEnd() const {
    if (!shift_) {
        return end_;
    }
    if (action_ == CommentAction::Line || action_ == CommentAction::Arrow) {
        return snap45(start_, end_);
    }
    return squareCorner(start_, end_);
}

void CommentMode::shapeRelease() {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const QPointF end = shapeEnd();
    annot::AnnotProps props = newProps(action_);
    if (action_ == CommentAction::Line || action_ == CommentAction::Arrow) {
        if (QLineF(start_, end).length() < kMinShapePt) {
            return;
        }
        props.lineStart = pointF(start_);
        props.lineEnd = pointF(end);
        if (action_ == CommentAction::Arrow) {
            props.intent = "LineArrow";
        }
    } else {
        const QRectF r = QRectF(start_, end).normalized();
        if (r.width() < kMinShapePt || r.height() < kMinShapePt) {
            return;
        }
        props.rect = {static_cast<float>(r.left()), static_cast<float>(r.top()),
                      static_cast<float>(r.right()), static_cast<float>(r.bottom())};
    }
    done(annot::create(*s, page_, std::move(props)), false);
}

void CommentMode::polyPress(const Locator& at, bool shift) {
    if (at.page < 0 || (!poly_.empty() && at.page != polyPage_)) {
        return;  // вершины — на одной странице
    }
    if (poly_.empty()) {
        polyPage_ = at.page;
        poly_.push_back(at.point);
        hover_ = view_.pageTransform(at.page).map(at.point);
        view_.viewport()->update();
        return;
    }
    const QTransform toView = view_.pageTransform(polyPage_);
    // Щелчок у первой вершины — замкнуть (многоугольник и облако).
    if (action_ != CommentAction::PolyLine && poly_.size() >= 3 &&
        QLineF(toView.map(poly_.front()), toView.map(at.point)).length() <= kCloseDistancePx) {
        polyFinish();
        return;
    }
    const QPointF p = shift ? snap45(poly_.back(), at.point) : at.point;
    if (QLineF(toView.map(poly_.back()), toView.map(p)).length() >= 1) {
        poly_.push_back(p);
    }
    view_.viewport()->update();
}

void CommentMode::polyFinish() {
    DocumentSession* s = session();
    std::vector<QPointF> points = std::move(poly_);
    const int page = polyPage_;
    poly_.clear();
    polyPage_ = -1;
    view_.viewport()->update();
    const std::size_t need = action_ == CommentAction::PolyLine ? 2 : 3;
    if (s == nullptr || points.size() < need) {
        return;
    }
    annot::AnnotProps props = newProps(action_);
    props.vertices.clear();
    for (const QPointF& p : points) {
        props.vertices.push_back(pointF(p));
    }
    if (action_ == CommentAction::Cloud) {
        props.cloudy = true;
        props.intent = "PolygonCloud";
    }
    done(annot::create(*s, page, std::move(props)), false);
}

bool CommentMode::mouseDoubleClick(QMouseEvent* event) {
    if (isPolyShape(action_) && !poly_.empty() && event->button() == Qt::LeftButton) {
        // Первый щелчок пары обычно уже поставил вершину (та же точка — не дублируется); двойной — конец
        // фигуры.
        const Locator at = locate(event->position());
        if (at.page == polyPage_) {
            const QTransform toView = view_.pageTransform(polyPage_);
            const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
            const QPointF p = shift ? snap45(poly_.back(), at.point) : at.point;
            if (QLineF(toView.map(poly_.back()), toView.map(p)).length() >= 1) {
                poly_.push_back(p);
            }
        }
        polyFinish();
        return true;
    }
    return mousePress(event);
}

bool CommentMode::keyPress(QKeyEvent* event) {
    if (poly_.empty()) {
        return false;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        polyFinish();
        return true;
    }
    if (event->key() == Qt::Key_Backspace) {
        poly_.pop_back();
        if (poly_.empty()) {
            polyPage_ = -1;
        }
        view_.viewport()->update();
        return true;
    }
    return false;
}

// Предпросмотр фигуры цветом и толщиной стиля: протягивание — до курсора (с Shift), вершины — с резинкой к
// курсору.
void CommentMode::paintShape(QPainter& painter) {
    const bool poly = isPolyShape(action_);
    const int page = poly ? polyPage_ : page_;
    if (page < 0 || (poly && poly_.empty())) {
        return;
    }
    const QTransform toView = view_.pageTransform(page);
    const double scale = std::hypot(toView.m11(), toView.m12());
    const annot::AnnotProps style = newProps(action_);
    QColor stroke = style.color.empty() ? Theme::colors().accent.color()
                                        : QColor::fromRgb(static_cast<QRgb>(style.color.toRgb()));
    stroke.setAlphaF(style.opacity);
    QPen pen(stroke, std::max(1.0, style.borderWidth * scale),
             poly && action_ == CommentAction::Cloud ? Qt::DashLine : Qt::SolidLine, Qt::RoundCap,
             Qt::RoundJoin);
    QBrush fill(Qt::NoBrush);
    if (!style.interiorColor.empty() && action_ != CommentAction::Line && action_ != CommentAction::Arrow &&
        action_ != CommentAction::PolyLine) {
        QColor c = QColor::fromRgb(static_cast<QRgb>(style.interiorColor.toRgb()));
        c.setAlphaF(style.opacity);
        fill = QBrush(c);
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(fill);
    if (poly) {
        QPolygonF line;
        for (const QPointF& p : poly_) {
            line << toView.map(p);
        }
        QPointF next = hover_;
        if (shift_) {
            next = toView.map(snap45(poly_.back(), toView.inverted().map(hover_)));
        }
        line << next;
        painter.setBrush(Qt::NoBrush);
        painter.drawPolyline(line);
        // Вершины — маркерами.
        const double r = Theme::sizes().handle / 2.0;
        painter.setPen(QPen(Theme::colors().accent.color(), 1));
        painter.setBrush(Theme::colors().surface.color());
        for (const QPointF& p : poly_) {
            painter.drawEllipse(toView.map(p), r, r);
        }
    } else {
        const QPointF a = toView.map(start_);
        const QPointF b = toView.map(shapeEnd());
        switch (action_) {
            case CommentAction::Line:
            case CommentAction::Arrow:
                painter.drawLine(a, b);
                break;
            case CommentAction::Rectangle:
                painter.drawRect(QRectF(a, b).normalized());
                break;
            case CommentAction::Oval:
                painter.drawEllipse(QRectF(a, b).normalized());
                break;
            default:
                break;
        }
    }
    painter.restore();
}

}  // namespace ot::ui
