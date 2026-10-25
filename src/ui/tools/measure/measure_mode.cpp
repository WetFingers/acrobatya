#include "ui/tools/measure/measure_mode.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QLineF>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/theme/theme.h"
#include "ui/tools/measure/measure_info_pane.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

PointF pointF(QPointF p) { return {static_cast<float>(p.x()), static_cast<float>(p.y())}; }

std::vector<PointF> pointsF(const std::vector<QPointF>& points) {
    std::vector<PointF> out;
    out.reserve(points.size());
    for (const QPointF& p : points) {
        out.push_back(pointF(p));
    }
    return out;
}

}  // namespace

MeasureMode::MeasureMode(PageView& view, ToolController& controller) : ViewTool(view, controller) {
    settings_ = measure::settings(Settings());
    reading_.scale = settings_.scale;
}

MeasureMode::~MeasureMode() { delete info_.data(); }

MeasureInfoPanel* MeasureMode::infoPanel() const { return info_.data(); }

QString MeasureMode::title() const { return actionTitle(action_); }

QString MeasureMode::actionTitle(MeasureAction action) {
    switch (action) {
        case MeasureAction::Distance:
            return tr("Расстояние");
        case MeasureAction::Perimeter:
            return tr("Периметр");
        case MeasureAction::Area:
            return tr("Площадь");
        case MeasureAction::Calibrate:
            return tr("Калибровка");
    }
    return {};
}

measure::Kind MeasureMode::kindOf(MeasureAction action) noexcept {
    switch (action) {
        case MeasureAction::Perimeter:
            return measure::Kind::Perimeter;
        case MeasureAction::Area:
            return measure::Kind::Area;
        default:
            return measure::Kind::Distance;
    }
}

// ---------------------------------------------------------------- подписи

QString MeasureMode::unitLabel(measure::Unit unit, bool area) {
    QString label;
    switch (unit) {
        case measure::Unit::Mm:
            label = tr("мм");
            break;
        case measure::Unit::Cm:
            label = tr("см");
            break;
        case measure::Unit::M:
            label = tr("м");
            break;
        case measure::Unit::Km:
            label = tr("км");
            break;
        case measure::Unit::In:
            label = tr("дюйм");
            break;
        case measure::Unit::Ft:
            label = tr("фут");
            break;
        case measure::Unit::Yd:
            label = tr("ярд");
            break;
        case measure::Unit::Mi:
            label = tr("миля");
            break;
        case measure::Unit::Pt:
            label = tr("пт");
            break;
    }
    return area ? label + QChar(0x00B2) : label;
}

QString MeasureMode::number(double value, int precision) {
    const QString point = QLocale().decimalPoint();
    const char decimal = point.isEmpty() ? '.' : point.at(0).toLatin1();
    return QString::fromStdString(measure::formatNumber(value, precision, decimal != 0 ? decimal : '.'));
}

QString MeasureMode::formatValue(double value, const measure::Scale& scale, bool area) {
    return number(value, scale.precision) + QLatin1Char(' ') + unitLabel(scale.realUnit, area);
}

QString MeasureMode::scaleText(const measure::Scale& scale) {
    const auto plain = [](double v) {
        QString s = number(v, 4);
        const QString point = QLocale().decimalPoint();
        if (s.contains(point)) {
            while (s.endsWith(QLatin1Char('0'))) {
                s.chop(1);
            }
            if (s.endsWith(point)) {
                s.chop(point.size());
            }
        }
        return s;
    };
    return tr("%1 %2 = %3 %4")
        .arg(plain(scale.paperValue), unitLabel(scale.paperUnit), plain(scale.realValue),
             unitLabel(scale.realUnit));
}

// ---------------------------------------------------------------- состояние

DocumentSession* MeasureMode::session() const {
    DocumentSession* s = view_.document();
    return s != nullptr && s->isOpen() ? s : nullptr;
}

void MeasureMode::setAction(MeasureAction action) {
    if (action == action_) {
        return;
    }
    reset();
    action_ = action;
    updateReading();
    emit actionChanged(action);
}

void MeasureMode::setLocked(const QString& reason) {
    lock_ = reason;
    if (!lock_.isEmpty()) {
        reset();
    }
}

void MeasureMode::setSettings(const measure::MeasureSettings& settings) {
    settings_ = settings;
    updateReading();
    view_.viewport()->update();
}

std::size_t MeasureMode::needed() const noexcept { return action_ == MeasureAction::Area ? 3 : 2; }

void MeasureMode::reset() {
    points_.clear();
    page_ = -1;
    pressed_ = false;
    updateReading();
    view_.viewport()->update();
}

void MeasureMode::activate() {
    for (const int page : view_.visiblePages()) {
        snapIndex(page);
        viewports(page);
    }
    setInfoVisible(settings_.showInfo);
    updateReading();
}

void MeasureMode::deactivate() {
    reset();
    hasHover_ = false;
    if (info_ != nullptr) {
        info_->hide();
    }
}

void MeasureMode::setInfoVisible(bool visible) {
    if (!visible) {
        if (info_ != nullptr) {
            info_->hide();
        }
        return;
    }
    if (info_ == nullptr) {
        info_ = new MeasureInfoPanel(&view_);
        connect(info_, &ToolWindow::closeRequested, this, [this] {
            if (info_ != nullptr) {
                info_->hide();
            }
        });
    }
    info_->setReading(reading_);
    info_->show();
    info_->place();
}

bool MeasureMode::cancel() {
    if (points_.empty()) {
        return false;
    }
    reset();
    return true;
}

bool MeasureMode::escape(bool cancelled) { return cancelled; }

void MeasureMode::documentChanged() {
    reset();
    ++generation_;
    snaps_.clear();
    snapPending_.clear();
    viewports_.clear();
    viewportsPending_.clear();
    hasHover_ = false;
}

void MeasureMode::pageChanged(int /*page*/) {
    // Правка страницы (наши пометки) пути содержимого и /VP не меняет — кеши остаются.
}

// ---------------------------------------------------------------- кеши страниц

const measure::SnapIndex* MeasureMode::snapIndex(int page) {
    if (page < 0) {
        return nullptr;
    }
    if (const auto it = snaps_.find(page); it != snaps_.end()) {
        return &it->second;
    }
    DocumentSession* s = session();
    if (s == nullptr || snapPending_.contains(page)) {
        return nullptr;
    }
    snapPending_.insert(page);
    const quint64 generation = generation_;
    measure::snapPoints(*s, page).then(this, [this, page, generation](QFuture<std::vector<PointF>> f) {
        if (generation != generation_) {
            return;
        }
        snapPending_.erase(page);
        std::vector<PointF> points;
        try {
            points = f.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): страница без путей — без привязки
        }
        snaps_[page] = measure::SnapIndex(std::move(points));
        emit snapReady(page);
    });
    return nullptr;
}

const std::vector<measure::Viewport>* MeasureMode::viewports(int page) {
    if (page < 0) {
        return nullptr;
    }
    if (const auto it = viewports_.find(page); it != viewports_.end()) {
        return &it->second;
    }
    DocumentSession* s = session();
    if (s == nullptr || viewportsPending_.contains(page)) {
        return nullptr;
    }
    viewportsPending_.insert(page);
    const quint64 generation = generation_;
    measure::viewports(*s, page).then(
        this, [this, page, generation](QFuture<std::vector<measure::Viewport>> f) {
            if (generation != generation_) {
                return;
            }
            viewportsPending_.erase(page);
            std::vector<measure::Viewport> list;
            try {
                list = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет /VP — масштаб пользователя
            }
            viewports_[page] = std::move(list);
            updateReading();
            emit viewportsReady(page);
        });
    return nullptr;
}

measure::Scale MeasureMode::scaleAt(int page, QPointF point, bool* fromDocument) {
    if (fromDocument != nullptr) {
        *fromDocument = false;
    }
    if (settings_.useDocumentScale) {
        if (const std::vector<measure::Viewport>* list = viewports(page)) {
            if (const std::optional<measure::Scale> s = measure::viewportScale(*list, pointF(point))) {
                if (fromDocument != nullptr) {
                    *fromDocument = true;
                }
                return *s;
            }
        }
    }
    return settings_.scale;
}

QPointF MeasureMode::adjust(int page, QPointF raw, Qt::KeyboardModifiers modifiers, bool* snapped) {
    if (snapped != nullptr) {
        *snapped = false;
    }
    if (settings_.snap) {
        if (const measure::SnapIndex* index = snapIndex(page)) {
            const auto radius = static_cast<float>(kSnapRadiusPx / std::max(0.01f, view_.zoom()));
            if (const std::optional<PointF> p = index->nearest(pointF(raw), radius)) {
                if (snapped != nullptr) {
                    *snapped = true;
                }
                return {p->x, p->y};
            }
        }
    }
    if (!points_.empty() && page == page_) {
        const double step = (modifiers & Qt::ShiftModifier) != 0 ? 45.0 : (settings_.ortho ? 90.0 : 0.0);
        if (step > 0) {
            const PointF c = measure::constrain(pointF(points_.back()), pointF(raw), step);
            return {c.x, c.y};
        }
    }
    return raw;
}

// ---------------------------------------------------------------- точки

void MeasureMode::addPoint(int page, QPointF point) {
    if (!lock_.isEmpty() || page < 0 || session() == nullptr) {
        return;
    }
    if (points_.empty()) {
        page_ = page;
        points_.push_back(point);
        hover_ = point;
        hoverPage_ = page;
        snapIndex(page);
        viewports(page);
        updateReading();
        view_.viewport()->update();
        return;
    }
    if (page != page_) {
        return;  // точки — на одной странице
    }
    const QTransform toView = view_.pageTransform(page_);
    if (action_ == MeasureAction::Area && points_.size() >= 3 &&
        QLineF(toView.map(points_.front()), toView.map(point)).length() <= kClosePx) {
        finish();
        return;
    }
    if (QLineF(toView.map(points_.back()), toView.map(point)).length() >= kMinSegmentPx) {
        points_.push_back(point);
    }
    if (action_ == MeasureAction::Distance || action_ == MeasureAction::Calibrate) {
        finish();
        return;
    }
    updateReading();
    view_.viewport()->update();
}

void MeasureMode::finish() {
    DocumentSession* s = session();
    std::vector<QPointF> points = points_;
    const int page = page_;
    const MeasureAction action = action_;
    reset();
    if (s == nullptr || points.size() < needed()) {
        return;
    }
    const std::vector<PointF> pts = pointsF(points);
    if (action == MeasureAction::Calibrate) {
        const double length = measure::length(pts[0], pts[1]);
        if (length > 0) {
            emit calibrationRequested(page, length);
        }
        return;
    }
    const measure::Kind kind = kindOf(action);
    if ((kind == measure::Kind::Area && measure::polygonArea(pts) <= 0) || measure::pathLength(pts) <= 0) {
        return;
    }
    measure::MeasureProps props;
    props.kind = kind;
    props.points = pts;
    props.scale = scaleAt(page, points.front());
    props.caption =
        formatValue(measure::value(kind, pts, props.scale), props.scale, kind == measure::Kind::Area)
            .toStdString();
    props.style = comments::newProps(Settings(), measure::typeOf(kind), QStringLiteral("measure"));
    if (kind == measure::Kind::Distance) {
        const annot::LineEnding ends = measure::lineEnding(settings_.lineEnds);
        props.style.lineStartStyle = ends;
        props.style.lineEndStyle = ends;
        props.leaderLength = settings_.leaderLength;
        props.leaderExtension = settings_.leaderExtension;
        props.leaderOffset = settings_.leaderOffset;
        props.captionTop = settings_.captionTop;
    } else {
        props.style.lineStartStyle = annot::LineEnding::None;
        props.style.lineEndStyle = annot::LineEnding::None;
    }
    props.style.interiorColor = {};
    measure::create(*s, page, std::move(props)).then(this, [this](QFuture<annot::AnnotRef> f) {
        try {
            emit created(f.result());
        } catch (const std::exception& e) {
            emit failed(QString::fromUtf8(e.what()));
        }
    });
}

void MeasureMode::updateReading() {
    MeasureReading r;
    r.action = action_;
    const int page = !points_.empty() ? page_ : hoverPage_;
    const QPointF at = !points_.empty() ? points_.front() : hover_;
    r.scale = page >= 0 ? scaleAt(page, at, &r.documentScale) : settings_.scale;
    if (!points_.empty()) {
        r.active = true;
        std::vector<PointF> pts = pointsF(points_);
        const bool withHover = hasHover_ && hoverPage_ == page_;
        if (withHover) {
            pts.push_back(pointF(hover_));
        }
        const measure::Kind kind =
            action_ == MeasureAction::Calibrate ? measure::Kind::Distance : kindOf(action_);
        r.value = measure::value(kind, pts, r.scale);
        if (pts.size() >= 2) {
            const PointF a = pts[pts.size() - 2];
            const PointF b = pts.back();
            const double u = r.scale.unitsPerPoint();
            r.dx = (static_cast<double>(b.x) - a.x) * u;
            r.dy = -(static_cast<double>(b.y) - a.y) * u;
            r.angle = measure::angle(a, b);
        }
    }
    reading_ = r;
    if (info_ != nullptr) {
        info_->setReading(reading_);
    }
    emit readingChanged();
}

// ---------------------------------------------------------------- мышь и клавиши

void MeasureMode::updateHover(QPointF viewportPoint, Qt::KeyboardModifiers modifiers) {
    hoverViewport_ = viewportPoint;
    modifiers_ = modifiers;
    const int page = !points_.empty() ? page_ : view_.pageAtViewport(viewportPoint);
    if (page < 0) {
        hasHover_ = false;
        updateReading();
        view_.viewport()->update();
        return;
    }
    hover_ = adjust(page, view_.viewportToPage(page, viewportPoint), modifiers, &hoverSnapped_);
    hoverPage_ = page;
    hasHover_ = true;
    updateReading();
    view_.viewport()->update();
}

bool MeasureMode::mousePress(QMouseEvent* event) {
    if (session() == nullptr) {
        return false;
    }
    if (event->button() == Qt::RightButton) {
        if (points_.empty()) {
            return false;
        }
        finish();  // правая кнопка — конец измерения
        return true;
    }
    if (event->button() != Qt::LeftButton) {
        return false;
    }
    if (!lock_.isEmpty()) {
        return true;  // правка запрещена: щелчок поглощён, курсор «нельзя»
    }
    const int page = !points_.empty() ? page_ : view_.pageAtViewport(event->position());
    if (page < 0) {
        return true;
    }
    pressed_ = true;
    addPoint(page, adjust(page, view_.viewportToPage(page, event->position()), event->modifiers()));
    return true;
}

bool MeasureMode::mouseMove(QMouseEvent* event) {
    updateHover(event->position(), event->modifiers());
    return !points_.empty();
}

bool MeasureMode::mouseRelease(QMouseEvent* event) {
    const bool was = pressed_;
    pressed_ = false;
    return was || event->button() == Qt::RightButton;
}

bool MeasureMode::mouseDoubleClick(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !points_.empty() &&
        (action_ == MeasureAction::Perimeter || action_ == MeasureAction::Area)) {
        // Первый щелчок пары уже поставил вершину (та же точка не повторяется); двойной — конец.
        const QPointF p = adjust(page_, view_.viewportToPage(page_, event->position()), event->modifiers());
        const QTransform toView = view_.pageTransform(page_);
        if (QLineF(toView.map(points_.back()), toView.map(p)).length() >= kMinSegmentPx) {
            points_.push_back(p);
        }
        finish();
        return true;
    }
    return mousePress(event);
}

bool MeasureMode::keyPress(QKeyEvent* event) {
    if (event->key() == Qt::Key_Shift || event->key() == Qt::Key_Control) {
        updateHover(hoverViewport_, event->modifiers());
        return false;
    }
    if (points_.empty()) {
        return false;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        finish();
        return true;
    }
    if (event->key() == Qt::Key_Backspace) {
        points_.pop_back();
        if (points_.empty()) {
            page_ = -1;
        }
        updateReading();
        view_.viewport()->update();
        return true;
    }
    return false;
}

QCursor MeasureMode::cursor(QPointF /*point*/, Qt::KeyboardModifiers /*modifiers*/) const {
    return lock_.isEmpty() ? QCursor(Qt::CrossCursor) : QCursor(Qt::ForbiddenCursor);
}

// ---------------------------------------------------------------- слой

void MeasureMode::paintOverlay(QPainter& painter) {
    const Palette& c = Theme::colors();
    const Metrics& m = Theme::sizes();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int page = !points_.empty() ? page_ : hoverPage_;
    if (page >= 0) {
        const QTransform toView = view_.pageTransform(page);
        QPolygonF line;
        for (const QPointF& p : points_) {
            line << toView.map(p);
        }
        const bool withHover = hasHover_ && hoverPage_ == page;
        if (!points_.empty() && withHover) {
            line << toView.map(hover_);
        }
        if (line.size() >= 2) {
            if (action_ == MeasureAction::Area && line.size() >= 3) {
                QColor fill = c.accent.color();
                fill.setAlphaF(0.12f);
                painter.setPen(Qt::NoPen);
                painter.setBrush(fill);
                painter.drawPolygon(line);
            }
            painter.setPen(QPen(c.accent.color(), 1.5, Qt::DashLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setBrush(Qt::NoBrush);
            painter.drawPolyline(line);
            if (action_ == MeasureAction::Area && line.size() >= 3) {
                painter.setPen(QPen(c.accent.color(), 1, Qt::DotLine));
                painter.drawLine(line.back(), line.front());
            }
        }
        // Вершины — маркерами.
        const double r = m.handle / 2.0;
        painter.setPen(QPen(c.accent.color(), 1));
        painter.setBrush(c.surface.color());
        for (const QPointF& p : points_) {
            painter.drawEllipse(toView.map(p), r - 1, r - 1);
        }
        // Привязка — квадрат у вершины.
        if (withHover && hoverSnapped_) {
            const QPointF h = toView.map(hover_);
            painter.setPen(QPen(c.accent.color(), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(QRectF(h.x() - r, h.y() - r, 2 * r, 2 * r));
        }
        // Значение — у курсора.
        if (!points_.empty() && withHover) {
            const QString text = formatValue(reading_.value, reading_.scale, action_ == MeasureAction::Area);
            const QFont font = uiFont(m);
            painter.setFont(font);
            const QFontMetricsF fm(font);
            const QPointF h = toView.map(hover_);
            QRectF box(h.x() + m.grid * 2, h.y() + m.grid * 2, fm.horizontalAdvance(text) + m.grid * 2,
                       fm.height() + m.grid);
            painter.setPen(QPen(c.border.color(), 1));
            painter.setBrush(c.surface.color());
            painter.drawRoundedRect(box, m.radius, m.radius);
            painter.setPen(c.text.color());
            painter.drawText(box, Qt::AlignCenter, text);
        }
    }
    painter.restore();
}

}  // namespace ot::ui
