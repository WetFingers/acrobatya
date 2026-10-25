// «Выбор объектов» (план 2.2b; эталон 3.4 «Выделение и правка на странице»; основа comment-08): аннотации
// страницы через минимальное ядро 2.2 (services/annot_ops.h). Щелчок — хит-тест задачей документа (annotAt):
// пока ответа нет, нажатие ждёт (Drag::Pending) — ответ решает, тянуть ли попавшую аннотацию или вести рамку
// выбора; отпущенная до ответа кнопка доделывается в ответе. По выбранной рамке и её ручкам — сразу, без
// задачи (рамки известны). Рамка выбора берёт списки страниц (annot::list) — заранее при наведении.
// Перемещение и масштаб — предпросмотр рамкой, по отпусканию — setRects одной операцией журнала; правка
// страницы (и undo) приходит pageChanged — рамки выбранных читаются заново, исчезнувшие снимаются.
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFuture>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QMimeData>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <numbers>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

namespace {

constexpr double kHitTolerancePx = 3.0;  // щелчок чуть мимо рамки — ещё попадание (px экрана)
constexpr double kHandleSlopPx = 3.0;    // ручка ловится шире своего квадрата
constexpr double kMinSizePx = 4.0;       // рамку не сжать меньше (px экрана)

QRectF annotQRect(const RectF& r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); }

// Ближайшее из восьми направлений (через 45°) от from к to (Shift у вершины).
QPointF snapTo45(QPointF from, QPointF to) {
    const QPointF d = to - from;
    constexpr double kStep = std::numbers::pi / 4;
    const double step = std::round(std::atan2(d.y(), d.x()) / kStep) * kStep;
    const double length = d.x() * std::cos(step) + d.y() * std::sin(step);
    return from + QPointF(length * std::cos(step), length * std::sin(step));
}

// Тип с маркерами вершин (вместо или вместе с ручками рамки).
bool hasVertices(annot::AnnotType type) {
    return type == annot::AnnotType::Line || type == annot::AnnotType::Polygon ||
           type == annot::AnnotType::PolyLine || type == annot::AnnotType::FreeText;
}

RectF annotRectF(const QRectF& r) {
    const QRectF n = r.normalized();
    return {static_cast<float>(n.left()), static_cast<float>(n.top()), static_cast<float>(n.right()),
            static_cast<float>(n.bottom())};
}

bool annotSelectable(const annot::AnnotInfo& info) {
    return info.type != annot::AnnotType::Popup && !info.hiddenOnScreen();
}

QPointF annotHandlePoint(const QRectF& r, int handle) {
    const QPointF c = r.center();
    switch (handle) {
        case AnnotSelectTool::TopLeft:
            return r.topLeft();
        case AnnotSelectTool::Top:
            return {c.x(), r.top()};
        case AnnotSelectTool::TopRight:
            return r.topRight();
        case AnnotSelectTool::Right:
            return {r.right(), c.y()};
        case AnnotSelectTool::BottomRight:
            return r.bottomRight();
        case AnnotSelectTool::Bottom:
            return {c.x(), r.bottom()};
        case AnnotSelectTool::BottomLeft:
            return r.bottomLeft();
        default:
            return {r.left(), c.y()};
    }
}

bool handleMovesLeft(int h) {
    return h == AnnotSelectTool::TopLeft || h == AnnotSelectTool::Left || h == AnnotSelectTool::BottomLeft;
}
bool handleMovesRight(int h) {
    return h == AnnotSelectTool::TopRight || h == AnnotSelectTool::Right || h == AnnotSelectTool::BottomRight;
}
bool handleMovesTop(int h) {
    return h == AnnotSelectTool::TopLeft || h == AnnotSelectTool::Top || h == AnnotSelectTool::TopRight;
}
bool handleMovesBottom(int h) {
    return h == AnnotSelectTool::BottomLeft || h == AnnotSelectTool::Bottom ||
           h == AnnotSelectTool::BottomRight;
}

Qt::CursorShape annotHandleCursor(int h) {
    switch (h) {
        case AnnotSelectTool::TopLeft:
        case AnnotSelectTool::BottomRight:
            return Qt::SizeFDiagCursor;
        case AnnotSelectTool::TopRight:
        case AnnotSelectTool::BottomLeft:
            return Qt::SizeBDiagCursor;
        case AnnotSelectTool::Top:
        case AnnotSelectTool::Bottom:
            return Qt::SizeVerCursor;
        default:
            return Qt::SizeHorCursor;
    }
}

}  // namespace

QString AnnotSelectTool::title() const {
    return QCoreApplication::translate("ot::ui::AnnotSelectTool", "Выбор объектов");
}

void AnnotSelectTool::activate() {
    for (const int page : view_.visiblePages()) {
        annotList(page);
    }
}

void AnnotSelectTool::deactivate() {
    cancel();
    clearSelection();
}

// ---------------------------------------------------------------- выбор

bool AnnotSelectTool::isSelected(const AnnotRef& ref) const {
    return std::any_of(selected_.begin(), selected_.end(),
                       [&](const AnnotInfo& info) { return info.ref == ref; });
}

void AnnotSelectTool::setSelection(std::vector<AnnotInfo> annots) {
    selected_.clear();
    for (AnnotInfo& info : annots) {
        if (info.ref.valid() && !isSelected(info.ref)) {
            // Свежие сведения: ждущий ответ списка страницы (запрошен до правки, что создала аннотацию) не
            // должен снять её с выбора.
            refresh_.remove(info.ref.page);
            selected_.push_back(std::move(info));
        }
    }
    publish();
    selectionChanged();
}

void AnnotSelectTool::clearSelection() {
    bands_.clear();
    refresh_.clear();
    if (selected_.empty()) {
        publish();
        return;
    }
    selected_.clear();
    publish();
    selectionChanged();
}

void AnnotSelectTool::deleteSelection() {
    DocumentSession* session = view_.document();
    if (session == nullptr || selected_.empty() || !controller_.editable()) {
        return;  // только чтение — удалять нельзя
    }
    cancel();
    std::vector<AnnotRef> refs;
    refs.reserve(selected_.size());
    for (const AnnotInfo& info : selected_) {
        if (!info.locked()) {
            refs.push_back(info.ref);  // «Заблокировано» не удаляется
        }
    }
    if (refs.empty()) {
        return;
    }
    annot::remove(*session, std::move(refs));  // ошибка — taskFailed сессии
    clearSelection();
}

void AnnotSelectTool::selectionChanged() {
    loadVertices();
    loadStampAngle();
    controller_.emitAnnotSelectionChanged();
}

// Угол одной выбранной /Stamp — задачей документа; ответ на прежний выбор отбрасывается.
void AnnotSelectTool::loadStampAngle() {
    const quint64 request = ++stampRequest_;
    stampAngle_.reset();
    stampRef_ = {};
    DocumentSession* session = view_.document();
    if (selected_.size() != 1 || session == nullptr || selected_.front().type != annot::AnnotType::Stamp ||
        !movable(selected_.front())) {
        return;
    }
    const AnnotRef ref = selected_.front().ref;
    annot::stampRotation(*session, ref).then(&controller_, [this, request, ref](QFuture<float> f) {
        float angle = 0;
        try {
            angle = f.result();
        } catch (const std::exception&) {  // нет вида — маркера нет
            return;
        }
        if (request != stampRequest_) {
            return;
        }
        stampAngle_ = angle;
        stampRef_ = ref;
        view_.viewport()->update();
    });
}

std::optional<QPointF> AnnotSelectTool::rotateHandle() const {
    if (!stampAngle_ || selected_.size() != 1 || !(selected_.front().ref == stampRef_)) {
        return std::nullopt;
    }
    const auto frame = frameOf(selected_.front());
    if (!frame) {
        return std::nullopt;
    }
    return QPointF(frame->center().x(), frame->top() - Theme::sizes().handle * 3.0);
}

// Угол перетаскивания маркера (градусы по часовой на экране): от направления нажатия к текущему вокруг центра
// рамки; Shift — итоговый угол шагом 15°.
double AnnotSelectTool::rotateDelta() const {
    const auto frame = selected_.size() == 1 ? frameOf(selected_.front()) : std::nullopt;
    if (!frame || !stampAngle_) {
        return 0;
    }
    const QPointF c = frame->center();
    const QPointF a = press_.viewport - c;
    const QPointF b = current_ - c;
    double delta = (std::atan2(b.y(), b.x()) - std::atan2(a.y(), a.x())) * 180.0 / std::numbers::pi;
    if (rotateShift_) {
        delta = std::round((*stampAngle_ + delta) / 15.0) * 15.0 - *stampAngle_;
    }
    return delta;
}

void AnnotSelectTool::finishRotate() {
    drag_ = Drag::None;
    DocumentSession* session = view_.document();
    const double delta = rotateDelta();
    if (!moved() || session == nullptr || !controller_.editable() || !stampAngle_ || std::fabs(delta) < 0.5) {
        view_.viewport()->update();
        return;
    }
    double angle = std::fmod(*stampAngle_ + delta, 360.0);
    if (angle < 0) {
        angle += 360.0;
    }
    stampAngle_ = static_cast<float>(angle);
    annot::setStampRotation(*session, stampRef_, static_cast<float>(angle));
    view_.viewport()->update();
}

// Вершины одной выбранной — задачей документа (свойства); ответ на прежний выбор отбрасывается.
void AnnotSelectTool::loadVertices(bool retry) {
    if (!retry) {
        vertexTries_ = 0;
    }
    const quint64 request = ++vertexRequest_;
    vertexPoints_.clear();
    callout_.clear();
    vertexType_ = annot::AnnotType::Unknown;
    vertexRef_ = {};
    DocumentSession* session = view_.document();
    if (selected_.size() != 1 || session == nullptr || !hasVertices(selected_.front().type) ||
        !movable(selected_.front())) {
        return;
    }
    const AnnotRef ref = selected_.front().ref;
    annot::props(*session, ref)
        .then(&controller_, [this, request, ref](QFuture<std::optional<annot::AnnotProps>> f) {
            std::optional<annot::AnnotProps> p;
            try {
                p = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет свойств — нет маркеров
            }
            if (request != vertexRequest_) {
                return;
            }
            if (!p) {
                // Задача прервана правкой страницы (ответ пуст при том же выборе) — ещё раз, не больше трёх.
                if (++vertexTries_ <= 3) {
                    QTimer::singleShot(50, &controller_, [this, request] {
                        if (request == vertexRequest_) {
                            loadVertices(true);
                        }
                    });
                }
                return;
            }
            std::vector<QPointF> points;
            switch (p->type) {
                case annot::AnnotType::Line:
                    points = {QPointF(p->lineStart.x, p->lineStart.y), QPointF(p->lineEnd.x, p->lineEnd.y)};
                    break;
                case annot::AnnotType::Polygon:
                case annot::AnnotType::PolyLine:
                    for (const PointF& v : p->vertices) {
                        points.emplace_back(v.x, v.y);
                    }
                    break;
                case annot::AnnotType::FreeText:
                    if (p->callout.size() >= 2) {
                        callout_ = p->callout;
                        points = {QPointF(p->callout[0].x, p->callout[0].y),
                                  QPointF(p->callout[1].x, p->callout[1].y)};
                    }
                    break;
                default:
                    break;
            }
            vertexPoints_ = std::move(points);
            vertexType_ = p->type;
            vertexRef_ = ref;
            view_.viewport()->update();
        });
}

std::optional<int> AnnotSelectTool::vertexAt(QPointF viewportPoint) const {
    if (vertexPoints_.empty() || selected_.size() != 1 || !(selected_.front().ref == vertexRef_)) {
        return std::nullopt;
    }
    const QTransform toView = view_.pageTransform(vertexRef_.page);
    const double reach = Theme::sizes().handle / 2.0 + kHandleSlopPx;
    std::optional<int> best;
    double bestDistance = reach;
    for (std::size_t i = 0; i < vertexPoints_.size(); ++i) {
        const double d = QLineF(toView.map(vertexPoints_[i]), viewportPoint).length();
        if (d <= bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// Вершина под мышью при перетаскивании (пункты страницы); Shift — под 45° к соседней точке.
QPointF AnnotSelectTool::draggedVertex() const {
    QPointF p = view_.viewportToPage(vertexRef_.page, current_);
    if (vertexShift_ && vertexPoints_.size() >= 2) {
        const auto n = static_cast<int>(vertexPoints_.size());
        const int neighbour = vertex_ > 0 ? vertex_ - 1 : 1 % n;
        p = snapTo45(vertexPoints_[static_cast<std::size_t>(neighbour)], p);
    }
    return p;
}

void AnnotSelectTool::finishVertex() {
    drag_ = Drag::None;
    DocumentSession* session = view_.document();
    if (!moved() || session == nullptr || !controller_.editable() || vertex_ < 0 ||
        vertex_ >= static_cast<int>(vertexPoints_.size())) {
        view_.viewport()->update();
        return;
    }
    const QPointF target = draggedVertex();
    vertexPoints_[static_cast<std::size_t>(vertex_)] = target;
    const PointF p{static_cast<float>(target.x()), static_cast<float>(target.y())};
    annot::AnnotPatch patch;
    switch (vertexType_) {
        case annot::AnnotType::Line:
            patch.lineStart =
                PointF{static_cast<float>(vertexPoints_[0].x()), static_cast<float>(vertexPoints_[0].y())};
            patch.lineEnd =
                PointF{static_cast<float>(vertexPoints_[1].x()), static_cast<float>(vertexPoints_[1].y())};
            break;
        case annot::AnnotType::Polygon:
        case annot::AnnotType::PolyLine: {
            std::vector<PointF> vertices;
            for (const QPointF& v : vertexPoints_) {
                vertices.push_back({static_cast<float>(v.x()), static_cast<float>(v.y())});
            }
            patch.vertices = std::move(vertices);
            break;
        }
        case annot::AnnotType::FreeText:
            if (static_cast<std::size_t>(vertex_) < callout_.size()) {
                callout_[static_cast<std::size_t>(vertex_)] = p;
                patch.callout = callout_;
            }
            break;
        default:
            break;
    }
    if (!patch.empty()) {
        annot::update(*session, vertexRef_, std::move(patch), QString::fromLatin1(annot::kMoveVertexStep));
    }
    view_.viewport()->update();
}

void AnnotSelectTool::publish() {
    RectOverlay& overlay = view_.annotationOverlay();
    overlay.clear();
    std::map<int, std::vector<RectF>> byPage;
    for (const AnnotInfo& info : selected_) {
        byPage[info.ref.page].push_back(draggedRect(info));
    }
    for (auto& [page, rects] : byPage) {
        overlay.setRects(page, std::move(rects));
    }
    view_.viewport()->update();
}

// ---------------------------------------------------------------- геометрия

std::optional<QRectF> AnnotSelectTool::frameOf(const AnnotInfo& info) const {
    if (view_.pageViewportRect(info.ref.page).isEmpty()) {
        return std::nullopt;  // страница не на экране (постраничный режим)
    }
    return view_.pageTransform(info.ref.page).mapRect(annotQRect(draggedRect(info)));
}

std::vector<QRectF> AnnotSelectTool::frames() const {
    std::vector<QRectF> result;
    for (const AnnotInfo& info : selected_) {
        if (const auto frame = frameOf(info)) {
            result.push_back(*frame);
        }
    }
    return result;
}

std::optional<AnnotSelectTool::Handle> AnnotSelectTool::handleAt(QPointF point) const {
    if (selected_.size() != 1 || !movable(selected_.front())) {
        return std::nullopt;
    }
    if (!vertexPoints_.empty() && vertexType_ != annot::AnnotType::FreeText) {
        return std::nullopt;  // линия и многоугольник правятся вершинами
    }
    const auto frame = frameOf(selected_.front());
    if (!frame) {
        return std::nullopt;
    }
    const double reach = Theme::sizes().handle / 2.0 + kHandleSlopPx;
    std::optional<Handle> best;
    double bestDistance = reach;
    for (int h = 0; h < kHandleCount; ++h) {
        const QPointF d = point - annotHandlePoint(*frame, h);
        const double distance = std::max(std::fabs(d.x()), std::fabs(d.y()));
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = static_cast<Handle>(h);
        }
    }
    return best;
}

bool AnnotSelectTool::selectedAt(QPointF point) const {
    return std::any_of(selected_.begin(), selected_.end(), [&](const AnnotInfo& info) {
        const auto frame = frameOf(info);
        return frame && frame->adjusted(-kHitTolerancePx, -kHitTolerancePx, kHitTolerancePx, kHitTolerancePx)
                            .contains(point);
    });
}

bool AnnotSelectTool::moved() const {
    return (current_ - press_.viewport).manhattanLength() >= QApplication::startDragDistance();
}

RectF AnnotSelectTool::draggedRect(const AnnotInfo& info) const {
    if ((drag_ != Drag::Move && drag_ != Drag::Resize) || !moved() || !movable(info)) {
        return info.rect;
    }
    const QTransform toView = view_.pageTransform(info.ref.page);
    const QRectF screen = toView.mapRect(annotQRect(info.rect));
    const QPointF delta = current_ - press_.viewport;
    QRectF next;
    if (drag_ == Drag::Move) {
        next = screen.translated(delta);
    } else {
        double l = screen.left();
        double t = screen.top();
        double r = screen.right();
        double b = screen.bottom();
        // Двигаемая сторона упирается в противоположную: рамка не выворачивается и не меньше kMinSizePx.
        if (handleMovesLeft(handle_)) {
            l = std::min(l + delta.x(), r - kMinSizePx);
        } else if (handleMovesRight(handle_)) {
            r = std::max(r + delta.x(), l + kMinSizePx);
        }
        if (handleMovesTop(handle_)) {
            t = std::min(t + delta.y(), b - kMinSizePx);
        } else if (handleMovesBottom(handle_)) {
            b = std::max(b + delta.y(), t + kMinSizePx);
        }
        next = QRectF(QPointF(l, t), QPointF(r, b));
    }
    QRectF onPage = toView.inverted().mapRect(next);
    if (drag_ == Drag::Move) {
        // Перемещение — в пределах страницы (если рамка в неё помещается).
        const QSizeF page = view_.pageSizePoints(info.ref.page);
        if (onPage.width() <= page.width()) {
            onPage.translate(std::max(0.0, -onPage.left()) - std::max(0.0, onPage.right() - page.width()), 0);
        }
        if (onPage.height() <= page.height()) {
            onPage.translate(0,
                             std::max(0.0, -onPage.top()) - std::max(0.0, onPage.bottom() - page.height()));
        }
    }
    return annotRectF(onPage);
}

// ---------------------------------------------------------------- мышь и клавиши

bool AnnotSelectTool::mouseDoubleClick(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && selected_.size() == 1 && selectedAt(event->position())) {
        pressing_ = false;
        drag_ = Drag::None;
        controller_.emitAnnotDoubleClicked(selected_.front());
        return true;
    }
    return mousePress(event);
}

bool AnnotSelectTool::mousePress(QMouseEvent* event) {
    const Qt::MouseButton button = event->button();
    if (button != Qt::LeftButton && button != Qt::RightButton) {
        return false;
    }
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen()) {
        return false;
    }
    const QPointF pos = event->position();
    press_ =
        Press{pos, event->globalPosition().toPoint(), view_.pageAtViewport(pos), button, event->modifiers()};
    current_ = pos;
    pressing_ = true;
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (button == Qt::LeftButton) {
        if (const auto rotate = rotateHandle();
            rotate && QLineF(*rotate, pos).length() <= Theme::sizes().handle / 2.0 + kHandleSlopPx) {
            drag_ = Drag::Rotate;
            rotateShift_ = shift;
            return true;
        }
        if (const auto vertex = vertexAt(pos)) {
            drag_ = Drag::Vertex;
            vertex_ = *vertex;
            vertexShift_ = shift;
            return true;
        }
        if (const auto handle = handleAt(pos)) {
            drag_ = Drag::Resize;
            handle_ = *handle;
            return true;
        }
        if (!shift && selectedAt(pos)) {
            drag_ = Drag::Move;
            return true;
        }
        if (press_.page < 0) {
            drag_ = Drag::Marquee;  // мимо страниц — аннотаций там нет
            return true;
        }
    } else {
        if (selectedAt(pos)) {
            pressing_ = false;
            showContextMenu(press_.global);
            return true;
        }
        if (press_.page < 0) {
            pressing_ = false;
            return true;
        }
    }
    drag_ = Drag::Pending;
    const quint64 request = ++request_;
    const QPointF pt = view_.viewportToPage(press_.page, pos);
    const auto tolerance = static_cast<float>(kHitTolerancePx / std::max(0.01f, view_.zoom()));
    annot::at(*session, press_.page, PointF{static_cast<float>(pt.x()), static_cast<float>(pt.y())},
              tolerance)
        .then(&controller_, [this, request](QFuture<std::optional<AnnotInfo>> done) {
            std::optional<AnnotInfo> hit;
            try {
                hit = done.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — как мимо
            }
            hitArrived(hit, request);
        });
    return true;
}

void AnnotSelectTool::hitArrived(const std::optional<AnnotInfo>& found, quint64 request) {
    if (request != request_ || drag_ != Drag::Pending) {
        return;  // нажатие брошено (Esc, смена документа) — ответ устарел
    }
    // Скрытые показом/скрытием комментариев — как пустое место.
    const std::optional<AnnotInfo> hit = found && hiddenAnnot(*found) ? std::nullopt : found;
    const bool shift = (press_.modifiers & Qt::ShiftModifier) != 0;
    if (press_.button == Qt::RightButton) {
        drag_ = Drag::None;
        if (!hit) {
            clearSelection();
            return;
        }
        if (!isSelected(hit->ref)) {
            setSelection({*hit});
        }
        showContextMenu(press_.global);
        return;
    }
    if (hit) {
        if (shift) {
            // Shift+щелчок: добавить или убрать, без перетаскивания.
            drag_ = Drag::None;
            const auto it = std::find_if(selected_.begin(), selected_.end(),
                                         [&](const AnnotInfo& info) { return info.ref == hit->ref; });
            if (it != selected_.end()) {
                selected_.erase(it);
            } else {
                selected_.push_back(*hit);
            }
            publish();
            selectionChanged();
            return;
        }
        if (!isSelected(hit->ref)) {
            setSelection({*hit});
        }
        drag_ = Drag::Move;
        if (!pressing_) {
            finishDrag();  // кнопку отпустили до ответа
        } else {
            publish();
        }
        return;
    }
    drag_ = Drag::Marquee;
    if (!pressing_) {
        finishMarquee();
    } else {
        view_.viewport()->update();
    }
}

bool AnnotSelectTool::mouseMove(QMouseEvent* event) {
    if (!pressing_) {
        annotList(view_.pageAtViewport(event->position()));  // заранее — для рамки выбора
        return false;
    }
    if (press_.button != Qt::LeftButton) {
        return true;
    }
    current_ = event->position();
    if (drag_ == Drag::Vertex) {
        vertexShift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
        view_.viewport()->update();
    } else if (drag_ == Drag::Rotate) {
        rotateShift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
        view_.viewport()->update();
    } else if (drag_ == Drag::Move || drag_ == Drag::Resize) {
        publish();
    } else if (drag_ == Drag::Marquee) {
        const QRectF band = QRectF(press_.viewport, current_).normalized();
        for (const int page : view_.visiblePages()) {
            if (view_.pageViewportRect(page).intersects(band)) {
                annotList(page);
            }
        }
        view_.viewport()->update();
    }
    return true;
}

bool AnnotSelectTool::mouseRelease(QMouseEvent* event) {
    if (!pressing_ || event->button() != press_.button) {
        return false;
    }
    pressing_ = false;
    if (press_.button != Qt::LeftButton) {
        return true;
    }
    current_ = event->position();
    switch (drag_) {
        case Drag::Move:
        case Drag::Resize:
            finishDrag();
            break;
        case Drag::Marquee:
            finishMarquee();
            break;
        case Drag::Vertex:
            vertexShift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
            finishVertex();
            break;
        case Drag::Rotate:
            rotateShift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
            finishRotate();
            break;
        default:
            break;  // Pending — доделает ответ хит-теста
    }
    return true;
}

void AnnotSelectTool::finishDrag() {
    DocumentSession* session = view_.document();
    if (!moved() || session == nullptr || !controller_.editable()) {
        drag_ = Drag::None;  // только чтение — рамки остаются на месте
        publish();
        return;
    }
    std::vector<annot::RectEdit> edits;
    edits.reserve(selected_.size());
    for (AnnotInfo& info : selected_) {
        if (movable(info)) {
            const RectF rect = draggedRect(info);
            edits.push_back({info.ref, rect});
        }
    }
    drag_ = Drag::None;
    if (edits.empty()) {
        publish();  // разметка и заблокированные не двигаются
        return;
    }
    // До ответа сессии рамки — новые; pageChanged принесёт настоящие (у NoZoom-иконок рамка — /BBox вида).
    for (const annot::RectEdit& e : edits) {
        for (AnnotInfo& info : selected_) {
            if (info.ref == e.ref) {
                info.rect = e.rect;
            }
        }
    }
    annot::setRects(*session, std::move(edits));  // ошибка — taskFailed сессии
    publish();
}

void AnnotSelectTool::finishMarquee() {
    drag_ = Drag::None;
    const bool shift = (press_.modifiers & Qt::ShiftModifier) != 0;
    if (!moved()) {
        if (!shift) {
            clearSelection();  // щелчок по пустому месту
        }
        view_.viewport()->update();
        return;
    }
    if (!shift) {
        selected_.clear();
    }
    bands_.clear();
    const QRectF band = QRectF(press_.viewport, current_).normalized();
    for (const int page : view_.visiblePages()) {
        const QRectF onScreen = view_.pageViewportRect(page).intersected(band);
        if (!onScreen.isEmpty()) {
            bands_[page] = annotRectF(view_.pageTransform(page).inverted().mapRect(onScreen));
        }
    }
    applyBands();
    publish();
    selectionChanged();
}

bool AnnotSelectTool::applyBands() {
    bool changed = false;
    for (auto it = bands_.begin(); it != bands_.end();) {
        const std::vector<AnnotInfo>* list = annotList(it->first);
        if (list == nullptr) {
            ++it;  // список придёт — listArrived доделает
            continue;
        }
        const QRectF band = annotQRect(it->second);
        for (const AnnotInfo& info : *list) {
            if (annotSelectable(info) && !hiddenAnnot(info) &&
                annotQRect(info.rect).normalized().intersects(band) && !isSelected(info.ref)) {
                selected_.push_back(info);
                changed = true;
            }
        }
        it = bands_.erase(it);
    }
    return changed;
}

bool AnnotSelectTool::keyPress(QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (event->key() == Qt::Key_Delete && mods == Qt::NoModifier && !selected_.empty()) {
        deleteSelection();
        return true;
    }
    if (event->matches(QKeySequence::Copy) && !selected_.empty()) {
        copySelection();
        return true;
    }
    if (event->matches(QKeySequence::Cut) && !selected_.empty()) {
        cutSelection();
        return true;
    }
    if (event->matches(QKeySequence::Paste) && canPaste()) {
        paste();
        return true;
    }
    return false;
}

void AnnotSelectTool::showContextMenu(QPoint global) {
    // Без своего цикла событий (popup, не exec): меню живёт само и удаляется при закрытии.
    auto* menu = new QMenu(view_.viewport());
    menu->setAttribute(Qt::WA_DeleteOnClose);
    menu->setObjectName(QStringLiteral("annotContextMenu"));
    fillContextMenu(*menu);
    menu->popup(global);
}

bool AnnotSelectTool::cancel() {
    if (drag_ == Drag::None) {
        return false;
    }
    drag_ = Drag::None;
    pressing_ = false;
    ++request_;
    publish();
    return true;
}

bool AnnotSelectTool::escape(bool cancelled) {
    if (cancelled) {
        return true;  // Esc бросил перетаскивание — выбор и инструмент остаются
    }
    if (!selected_.empty()) {
        clearSelection();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- буфер обмена, блокировка, меню (5.1)

bool AnnotSelectTool::movable(const AnnotInfo& info) noexcept {
    return !annot::isTextMarkup(info.type) && !info.locked();
}

void AnnotSelectTool::copySelection() {
    DocumentSession* session = view_.document();
    if (session == nullptr || selected_.empty()) {
        return;
    }
    std::vector<AnnotRef> refs;
    for (const AnnotInfo& info : selected_) {
        refs.push_back(info.ref);
    }
    const int page = selected_.front().ref.page;
    const QString source = QString::number(reinterpret_cast<quintptr>(session), 16);
    annot::copy(*session, std::move(refs)).then(&controller_, [source, page](QFuture<annot::AnnotClip> done) {
        annot::AnnotClip clip;
        try {
            clip = done.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): не скопировалось — буфер прежний
            return;
        }
        if (clip.empty()) {
            return;
        }
        const QJsonObject o{
            {QStringLiteral("json"), QString::fromStdString(clip.json)},
            {QStringLiteral("pdf"),
             QString::fromLatin1(
                 QByteArray(clip.pdf.data(), static_cast<qsizetype>(clip.pdf.size())).toBase64())},
            {QStringLiteral("count"), clip.count},
            {QStringLiteral("source"), source},
            {QStringLiteral("page"), page}};
        auto* mime = new QMimeData;
        mime->setData(QLatin1String(kClipboardMime), QJsonDocument(o).toJson(QJsonDocument::Compact));
        QGuiApplication::clipboard()->setMimeData(mime);
    });
}

void AnnotSelectTool::cutSelection() {
    if (!controller_.editable() || selected_.empty()) {
        return;
    }
    copySelection();
    deleteSelection();  // задача удаления идёт после задачи копии — сессия выполняет по очереди
}

bool AnnotSelectTool::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime != nullptr && mime->hasFormat(QLatin1String(kClipboardMime)) && view_.document() != nullptr &&
           controller_.editable();
}

void AnnotSelectTool::paste() {
    DocumentSession* session = view_.document();
    if (!canPaste() || session == nullptr) {
        return;
    }
    const QJsonObject o =
        QJsonDocument::fromJson(QGuiApplication::clipboard()->mimeData()->data(QLatin1String(kClipboardMime)))
            .object();
    annot::AnnotClip clip;
    clip.json = o.value(QStringLiteral("json")).toString().toStdString();
    clip.pdf = QByteArray::fromBase64(o.value(QStringLiteral("pdf")).toString().toLatin1()).toStdString();
    clip.count = o.value(QStringLiteral("count")).toInt();
    const int page = std::clamp(view_.currentPage(), 0, std::max(0, view_.pageCount() - 1));
    const bool samePlace = o.value(QStringLiteral("source")).toString() ==
                               QString::number(reinterpret_cast<quintptr>(session), 16) &&
                           o.value(QStringLiteral("page")).toInt() == page;
    const PointF offset = samePlace ? PointF{kPasteOffset, kPasteOffset} : PointF{};
    annot::paste(*session, page, std::move(clip), offset)
        .then(&controller_, [this, page](QFuture<std::vector<AnnotRef>> done) {
            try {
                selectPasted(page, done.result());
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): ошибка — taskFailed сессии
            }
        });
}

void AnnotSelectTool::selectPasted(int page, std::vector<AnnotRef> refs) {
    DocumentSession* session = view_.document();
    if (session == nullptr || refs.empty()) {
        return;
    }
    const quint64 epoch = epoch_;
    annot::list(*session, page, TaskPriority::High)
        .then(&controller_, [this, epoch, refs = std::move(refs)](QFuture<std::vector<AnnotInfo>> done) {
            if (epoch != epoch_) {
                return;
            }
            std::vector<AnnotInfo> chosen;
            try {
                for (const AnnotInfo& info : done.result()) {
                    if (std::find(refs.begin(), refs.end(), info.ref) != refs.end()) {
                        chosen.push_back(info);
                    }
                }
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет списка — без выбора
            }
            if (controller_.tool() != ViewToolId::AnnotSelect) {
                controller_.setTool(ViewToolId::AnnotSelect);
            }
            setSelection(std::move(chosen));
        });
}

bool AnnotSelectTool::hiddenType(annot::AnnotType type) const noexcept {
    const int i = static_cast<int>(type);
    return i >= 0 && i < 32 && (hiddenTypes_ & (1u << i)) != 0;
}

bool AnnotSelectTool::hiddenAnnot(const AnnotInfo& info) const {
    if (hiddenType(info.type)) {
        return true;
    }
    const auto it = hiddenAnnots_.find(info.ref.page);
    return it != hiddenAnnots_.end() &&
           std::binary_search(it->second.begin(), it->second.end(), info.ref.objnum);
}

void AnnotSelectTool::setHiddenAnnots(const std::map<int, std::vector<int>>& byPage) {
    hiddenAnnots_ = byPage;
    for (auto& [page, list] : hiddenAnnots_) {
        std::sort(list.begin(), list.end());
    }
    setHiddenTypes(hiddenTypes_);
}

void AnnotSelectTool::setHiddenTypes(std::uint32_t mask) {
    hiddenTypes_ = mask;
    const auto before = selected_.size();
    selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                   [this](const AnnotInfo& info) { return hiddenAnnot(info); }),
                    selected_.end());
    if (selected_.size() != before) {
        publish();
        selectionChanged();
    }
}

void AnnotSelectTool::setSelectionLocked(bool locked) {
    DocumentSession* session = view_.document();
    if (session == nullptr || selected_.empty() || !controller_.editable()) {
        return;
    }
    std::vector<AnnotRef> refs;
    for (AnnotInfo& info : selected_) {
        refs.push_back(info.ref);
        info.flags = locked ? (info.flags | annot::kAnnotLocked) : (info.flags & ~annot::kAnnotLocked);
    }
    annot::setFlags(*session, std::move(refs), locked ? annot::kAnnotLocked : 0,
                    locked ? 0 : annot::kAnnotLocked);
}

void AnnotSelectTool::fillContextMenu(QMenu& menu) {
    const bool editable = controller_.editable();
    const bool anyLocked =
        std::any_of(selected_.begin(), selected_.end(), [](const AnnotInfo& i) { return i.locked(); });
    const bool allLocked = !selected_.empty() && std::all_of(selected_.begin(), selected_.end(),
                                                             [](const AnnotInfo& i) { return i.locked(); });
    auto tr = [](const char* text) { return QCoreApplication::translate("ot::ui::AnnotSelectTool", text); };
    QAction* copy = menu.addAction(tr("Копировать"), [this] { copySelection(); });
    copy->setObjectName(QStringLiteral("annotCopy"));
    copy->setShortcut(QKeySequence::Copy);
    QAction* cut = menu.addAction(tr("Вырезать"), [this] { cutSelection(); });
    cut->setObjectName(QStringLiteral("annotCut"));
    cut->setShortcut(QKeySequence::Cut);
    cut->setEnabled(editable && !anyLocked);
    QAction* del = menu.addAction(tr("Удалить"), [this] { deleteSelection(); });
    del->setObjectName(QStringLiteral("annotDelete"));
    del->setShortcut(QKeySequence::Delete);
    del->setEnabled(editable && !anyLocked);
    menu.addSeparator();
    QAction* lock =
        menu.addAction(tr("Заблокировать"), [this, allLocked] { setSelectionLocked(!allLocked); });
    lock->setObjectName(QStringLiteral("annotLock"));
    lock->setCheckable(true);
    lock->setChecked(allLocked);
    lock->setEnabled(editable);
    // Выровнять и распределить (5.5, comment-44) — нескольким выбранным с /Rect одной страницы.
    if (selected_.size() >= 2) {
        const std::size_t count = editable ? alignable().size() : 0;
        QMenu* alignMenu = menu.addMenu(tr("Выровнять"));
        alignMenu->setObjectName(QStringLiteral("annotAlignMenu"));
        alignMenu->setEnabled(count >= 2);
        const std::pair<annot::Align, const char*> aligns[] = {
            {annot::Align::Left, QT_TRANSLATE_NOOP("ot::ui::AnnotSelectTool", "По левому краю")},
            {annot::Align::HorizontalCenter,
             QT_TRANSLATE_NOOP("ot::ui::AnnotSelectTool", "По центру по горизонтали")},
            {annot::Align::Right, QT_TRANSLATE_NOOP("ot::ui::AnnotSelectTool", "По правому краю")},
            {annot::Align::Top, QT_TRANSLATE_NOOP("ot::ui::AnnotSelectTool", "По верхнему краю")},
            {annot::Align::VerticalCenter,
             QT_TRANSLATE_NOOP("ot::ui::AnnotSelectTool", "По центру по вертикали")},
            {annot::Align::Bottom, QT_TRANSLATE_NOOP("ot::ui::AnnotSelectTool", "По нижнему краю")},
        };
        for (const auto& [align, text] : aligns) {
            const annot::Align a = align;
            alignMenu->addAction(tr(text), [this, a] { alignSelection(a); });
        }
        QMenu* distributeMenu = menu.addMenu(tr("Распределить"));
        distributeMenu->setObjectName(QStringLiteral("annotDistributeMenu"));
        distributeMenu->setEnabled(count >= 3);
        distributeMenu->addAction(tr("По горизонтали"),
                                  [this] { distributeSelection(annot::Distribute::Horizontal); });
        distributeMenu->addAction(tr("По вертикали"),
                                  [this] { distributeSelection(annot::Distribute::Vertical); });
    }
    controller_.emitAnnotContextMenu(&menu);  // пункты инструментов (5.1 — комментарии, 6.3 — поля)
}

std::vector<AnnotSelectTool::AnnotInfo> AnnotSelectTool::alignable() const {
    std::vector<AnnotInfo> out;
    if (selected_.empty()) {
        return out;
    }
    const int page = selected_.front().ref.page;
    for (const AnnotInfo& info : selected_) {
        if (info.ref.page == page && movable(info) && !info.locked()) {
            out.push_back(info);
        }
    }
    return out;
}

void AnnotSelectTool::alignSelection(annot::Align align) {
    DocumentSession* session = view_.document();
    const std::vector<AnnotInfo> items = alignable();
    if (session == nullptr || !controller_.editable() || items.size() < 2) {
        return;
    }
    std::vector<RectF> rects;
    for (const AnnotInfo& info : items) {
        rects.push_back(info.rect);
    }
    const std::vector<RectF> placed = annot::alignRects(rects, align);
    std::vector<annot::RectEdit> edits;
    for (std::size_t i = 0; i < items.size(); ++i) {
        edits.push_back({items[i].ref, placed[i]});
    }
    for (AnnotInfo& info : selected_) {
        for (const annot::RectEdit& e : edits) {
            if (info.ref == e.ref) {
                info.rect = e.rect;  // до ответа сессии рамки — новые
            }
        }
    }
    annot::setRects(*session, std::move(edits), QString::fromLatin1(annot::kAlignStep));
    publish();
}

void AnnotSelectTool::distributeSelection(annot::Distribute distribute) {
    DocumentSession* session = view_.document();
    const std::vector<AnnotInfo> items = alignable();
    if (session == nullptr || !controller_.editable() || items.size() < 3) {
        return;
    }
    std::vector<RectF> rects;
    for (const AnnotInfo& info : items) {
        rects.push_back(info.rect);
    }
    const std::vector<RectF> placed = annot::distributeRects(rects, distribute);
    std::vector<annot::RectEdit> edits;
    for (std::size_t i = 0; i < items.size(); ++i) {
        edits.push_back({items[i].ref, placed[i]});
    }
    for (AnnotInfo& info : selected_) {
        for (const annot::RectEdit& e : edits) {
            if (info.ref == e.ref) {
                info.rect = e.rect;
            }
        }
    }
    annot::setRects(*session, std::move(edits), QString::fromLatin1(annot::kDistributeStep));
    publish();
}

bool AnnotSelectTool::wantsShortcut(const QKeyEvent* event) const {
    if ((event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) && !selected_.empty()) {
        return true;
    }
    return event->matches(QKeySequence::Paste) && canPaste();
}

// ---------------------------------------------------------------- списки страниц

const std::vector<AnnotSelectTool::AnnotInfo>* AnnotSelectTool::annotList(int page) {
    if (page < 0 || page >= view_.pageCount()) {
        return nullptr;
    }
    const auto it = lists_.constFind(page);
    if (it != lists_.constEnd()) {
        return &*it;
    }
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || listRequested_.contains(page)) {
        return nullptr;
    }
    listRequested_.insert(page);
    const quint64 generation = listGeneration_.value(page);
    const quint64 epoch = epoch_;
    annot::list(*session, page, TaskPriority::High)
        .then(&controller_, [this, page, generation, epoch](QFuture<std::vector<AnnotInfo>> done) {
            if (epoch != epoch_ || generation != listGeneration_.value(page) ||
                !listRequested_.remove(page)) {
                return;  // документ или страница изменились после запроса — придёт новый ответ
            }
            try {
                lists_.insert(page, done.result());
            } catch (const std::exception&) {
                lists_.insert(page, {});  // страница без списка — выбирать на ней нечего
            }
            listArrived(page);
        });
    return nullptr;
}

void AnnotSelectTool::listArrived(int page) {
    bool changed = false;
    if (refresh_.remove(page)) {
        const std::vector<AnnotInfo>& list = lists_[page];
        for (auto it = selected_.begin(); it != selected_.end();) {
            if (it->ref.page != page) {
                ++it;
                continue;
            }
            const auto found = std::find_if(list.begin(), list.end(),
                                            [&](const AnnotInfo& info) { return info.ref == it->ref; });
            if (found == list.end() || !annotSelectable(*found)) {
                it = selected_.erase(it);
                changed = true;
            } else {
                *it = *found;
                ++it;
            }
        }
        publish();
    }
    if (bands_.count(page) > 0 && applyBands()) {
        changed = true;
        publish();
    }
    if (changed) {
        selectionChanged();
    }
}

void AnnotSelectTool::documentChanged() {
    cancel();
    ++epoch_;
    lists_.clear();
    listRequested_.clear();
    listGeneration_.clear();
    clearSelection();
}

void AnnotSelectTool::pageChanged(int page) {
    lists_.remove(page);
    listRequested_.remove(page);
    ++listGeneration_[page];
    const bool affected = std::any_of(selected_.begin(), selected_.end(),
                                      [&](const AnnotInfo& info) { return info.ref.page == page; });
    if (affected) {
        refresh_.insert(page);
    }
    if (vertexRef_.valid() && vertexRef_.page == page && drag_ != Drag::Vertex) {
        loadVertices();  // правка или undo сдвинули вершины
    }
    if (stampRef_.valid() && stampRef_.page == page && drag_ != Drag::Rotate) {
        loadStampAngle();  // undo поворота
    }
    if (affected || bands_.count(page) > 0) {
        annotList(page);
    }
}

// ---------------------------------------------------------------- вид

void AnnotSelectTool::paintOverlay(QPainter& painter) {
    const auto& colors = Theme::colors();
    const auto& sizes = Theme::sizes();
    if (drag_ == Drag::Marquee && pressing_) {
        QPen pen(colors.accent.color(), sizes.pageBorder, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(press_.viewport, current_).normalized());
    }
    if (selected_.size() != 1) {
        return;
    }
    const auto frame = frameOf(selected_.front());
    if (!frame) {
        return;
    }
    QPen pen(colors.accent.color(), sizes.pageBorder);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(colors.surface.color());
    const double s = sizes.handle;
    const bool vertices = !vertexPoints_.empty() && selected_.front().ref == vertexRef_;
    if (!vertices || vertexType_ == annot::AnnotType::FreeText) {
        painter.setRenderHint(QPainter::Antialiasing, false);
        for (int h = 0; h < kHandleCount; ++h) {
            const QPointF c = annotHandlePoint(*frame, h);
            painter.drawRect(QRectF(c - QPointF(s / 2, s / 2), QSizeF(s, s)));
        }
    }
    if (const auto rotate = rotateHandle()) {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.drawLine(QPointF(frame->center().x(), frame->top()), *rotate + QPointF(0, s / 2));
        painter.drawEllipse(*rotate, s / 2, s / 2);
        if (drag_ == Drag::Rotate && moved()) {
            // Предпросмотр: рамка, повёрнутая вокруг центра.
            QTransform turn;
            turn.translate(frame->center().x(), frame->center().y());
            turn.rotate(rotateDelta());
            turn.translate(-frame->center().x(), -frame->center().y());
            QPen dash(colors.accent.color(), sizes.pageBorder, Qt::DashLine);
            dash.setCosmetic(true);
            painter.setPen(dash);
            painter.setBrush(Qt::NoBrush);
            painter.drawPolygon(turn.map(QPolygonF(*frame)));
        }
        return;
    }
    if (!vertices) {
        return;
    }
    const QTransform toView = view_.pageTransform(vertexRef_.page);
    std::vector<QPointF> points = vertexPoints_;
    const bool dragging =
        drag_ == Drag::Vertex && moved() && vertex_ >= 0 && vertex_ < static_cast<int>(points.size());
    if (dragging) {
        points[static_cast<std::size_t>(vertex_)] = draggedVertex();
    }
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (dragging) {
        QPen dash(colors.accent.color(), sizes.pageBorder, Qt::DashLine);
        dash.setCosmetic(true);
        painter.setPen(dash);
        painter.setBrush(Qt::NoBrush);
        QPolygonF line;
        for (const QPointF& p : points) {
            line << toView.map(p);
        }
        if (vertexType_ == annot::AnnotType::Polygon) {
            line << line.front();
        }
        painter.drawPolyline(line);
        painter.setPen(pen);
        painter.setBrush(colors.surface.color());
    }
    for (const QPointF& p : points) {
        painter.drawEllipse(toView.map(p), s / 2, s / 2);
    }
}

QCursor AnnotSelectTool::cursor(QPointF point, Qt::KeyboardModifiers /*modifiers*/) const {
    if (drag_ == Drag::Vertex || (drag_ == Drag::None && vertexAt(point))) {
        return QCursor(Qt::CrossCursor);
    }
    const auto rotate = drag_ == Drag::None ? rotateHandle() : std::nullopt;
    if (drag_ == Drag::Rotate ||
        (rotate && QLineF(*rotate, point).length() <= Theme::sizes().handle / 2.0 + kHandleSlopPx)) {
        return QCursor(Qt::PointingHandCursor);
    }
    if (drag_ == Drag::Move) {
        return QCursor(Qt::SizeAllCursor);
    }
    const std::optional<Handle> handle =
        drag_ == Drag::Resize ? std::optional<Handle>(handle_) : handleAt(point);
    if (handle) {
        return QCursor(annotHandleCursor(*handle));
    }
    return QCursor(selectedAt(point) ? Qt::SizeAllCursor : Qt::ArrowCursor);
}

}  // namespace ot::ui
