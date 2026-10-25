// Режим «Заполнить и подписать»: общее — действие, страница и точка под мышью, геометрия бланка (кеш по
// страницам), пунктир найденной клетки, передача нажатий «Выбору объектов» над поставленным. Действия —
// text_placer.cpp, marks.cpp, signature_placer.cpp.
#include "ui/tools/fillsign/fillsign_mode.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/fillsign/comb_editor.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

// Точка у маркера поворота выбранного штампа (подпись Fill & Sign — тоже штамп).
bool nearRotateHandle(const AnnotSelectTool& select, QPointF pos) {
    const std::optional<QPointF> handle = select.rotateHandle();
    return handle && QLineF(*handle, pos).length() <= Theme::sizes().handle;
}

}  // namespace

FillSignMode::FillSignMode(PageView& view, ToolController& controller) : ViewTool(view, controller) {}

FillSignMode::~FillSignMode() = default;

QString FillSignMode::title() const { return actionTitle(action_); }

QString FillSignMode::actionTitle(FillSignAction action) {
    switch (action) {
        case FillSignAction::Text:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Добавить текст");
        case FillSignAction::Check:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Галочка");
        case FillSignAction::Cross:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Крестик");
        case FillSignAction::Dot:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Точка");
        case FillSignAction::Line:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Линия");
        case FillSignAction::Box:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Рамка");
        case FillSignAction::Signature:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Подпись");
        case FillSignAction::Initials:
            return QCoreApplication::translate("ot::ui::FillSignMode", "Инициалы");
    }
    return {};
}

bool FillSignMode::isMark(FillSignAction action) noexcept {
    return action == FillSignAction::Check || action == FillSignAction::Cross ||
           action == FillSignAction::Dot || action == FillSignAction::Line || action == FillSignAction::Box;
}

fillsign::Mark FillSignMode::markOf(FillSignAction action) noexcept {
    switch (action) {
        case FillSignAction::Cross:
            return fillsign::Mark::Cross;
        case FillSignAction::Dot:
            return fillsign::Mark::Dot;
        case FillSignAction::Line:
            return fillsign::Mark::Line;
        case FillSignAction::Box:
            return fillsign::Mark::Box;
        default:
            return fillsign::Mark::Check;
    }
}

void FillSignMode::setAction(FillSignAction action) {
    if (action == action_) {
        return;
    }
    cancel();
    action_ = action;
    hover_.reset();
    view_.viewport()->update();
    emit actionChanged(action);
}

void FillSignMode::setLocked(const QString& reason) {
    lock_ = reason;
    if (!lock_.isEmpty()) {
        cancel();
    }
}

void FillSignMode::setInkColor(const QColor& color) {
    if (color.isValid()) {
        ink_ = color;
    }
}

void FillSignMode::setSignatureImage(fillsign::SignatureKind kind, const QImage& image) {
    if (image.isNull()) {
        images_.erase(kind);
    } else {
        images_[kind] = image;
    }
}

QImage FillSignMode::signatureImage(fillsign::SignatureKind kind) const {
    const auto it = images_.find(kind);
    return it != images_.end() ? it->second : fillsign::SavedSignatures().load(kind);
}

annot::AnnotColor FillSignMode::annotColor() const {
    return annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(ink_.rgb() & 0xffffff));
}

DocumentSession* FillSignMode::session() const {
    DocumentSession* s = view_.document();
    return s != nullptr && s->isOpen() ? s : nullptr;
}

void FillSignMode::reportError(const QString& what) { emit failed(what); }

void FillSignMode::activate() {
    controller_.annotSelect().activate();  // списки аннотаций видимых страниц — для перемещения
    for (const int page : view_.visiblePages()) {
        geometry(page);
    }
}

void FillSignMode::deactivate() {
    cancel();
    controller_.annotSelect().clearSelection();
    hover_.reset();
}

// ---------------------------------------------------------------- геометрия

const fillsign::PageGeometry* FillSignMode::geometry(int page) {
    if (page < 0) {
        return nullptr;
    }
    if (const auto it = geometry_.find(page); it != geometry_.end()) {
        return &it->second;
    }
    DocumentSession* s = session();
    if (s == nullptr || pending_.contains(page)) {
        return nullptr;
    }
    const quint64 generation = generation_;
    QFuture<fillsign::PageGeometry> future = fillsign::geometry(*s, page);
    pending_[page] = future;
    future.then(this, [this, page, generation](QFuture<fillsign::PageGeometry> f) {
        if (generation != generation_) {
            return;  // документ сменился
        }
        pending_.erase(page);
        try {
            geometry_[page] = f.result();
        } catch (const std::exception&) {
            geometry_[page] = {};  // страница без текстового слоя — без привязки
        }
        emit geometryReady(page);
        if (hoverPage_ == page) {
            updateHover(hoverViewport_);
        }
    });
    return nullptr;
}

void FillSignMode::withGeometry(int page, std::function<void(const fillsign::PageGeometry&)> then) {
    if (const fillsign::PageGeometry* g = geometry(page)) {
        then(*g);
        return;
    }
    const quint64 generation = generation_;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(this, &FillSignMode::geometryReady, this,
                          [this, page, generation, then = std::move(then), connection](int ready) {
                              if (ready != page) {
                                  return;
                              }
                              QObject::disconnect(*connection);
                              if (generation == generation_) {
                                  if (const fillsign::PageGeometry* g = geometry(page)) {
                                      then(*g);
                                  }
                              }
                          });
}

void FillSignMode::documentChanged() {
    cancel();
    ++generation_;
    geometry_.clear();
    pending_.clear();
    hover_.reset();
    if (InlineTextEditor* editor = InlineTextEditor::active(view_)) {
        editor->commit();  // набранное не теряется
    }
    if (CombEditor* comb = CombEditor::active(view_)) {
        comb->commit();
    }
}

void FillSignMode::pageChanged(int /*page*/) {
    // Правка страницы (наш текст) векторы бланка не меняет — кеш остаётся.
}

// ---------------------------------------------------------------- мышь

bool FillSignMode::forwardPress(QMouseEvent* event, int page, QPointF point) {
    AnnotSelectTool& select = controller_.annotSelect();
    const QPointF pos = event->position();
    bool hit = select.handleAt(pos).has_value() || nearRotateHandle(select, pos);
    for (const QRectF& frame : select.frames()) {
        hit = hit || frame.contains(pos);
    }
    pressHit_.reset();
    if (const std::vector<annot::AnnotInfo>* list = select.annotList(page)) {
        const auto tolerance = static_cast<float>(2.0 / std::max(0.01f, view_.zoom()));
        for (auto it = list->rbegin(); it != list->rend(); ++it) {
            const RectF& r = it->rect;
            if (it->type == annot::AnnotType::Popup || it->type == annot::AnnotType::Link ||
                it->type == annot::AnnotType::Widget || it->hiddenOnScreen()) {
                continue;
            }
            if (point.x() >= r.x0 - tolerance && point.x() <= r.x1 + tolerance &&
                point.y() >= r.y0 - tolerance && point.y() <= r.y1 + tolerance) {
                pressHit_ = *it;
                hit = true;
                break;
            }
        }
    }
    if (!hit) {
        if (!select.selection().empty()) {
            select.clearSelection();
        }
        return false;
    }
    forwarding_ = true;
    select.mousePress(event);
    return true;
}

bool FillSignMode::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || session() == nullptr) {
        if (event->button() == Qt::RightButton && session() != nullptr) {
            forwarding_ = true;  // контекстное меню «Выбора объектов» над поставленным
            return controller_.annotSelect().mousePress(event);
        }
        return false;
    }
    if (!lock_.isEmpty()) {
        return true;  // правка запрещена: щелчок поглощён, курсор «нельзя»
    }
    const int page = view_.pageAtViewport(event->position());
    if (page < 0) {
        return true;
    }
    const QPointF point = view_.viewportToPage(page, event->position());
    pressViewport_ = event->position();
    if (forwardPress(event, page, point)) {
        return true;
    }
    page_ = page;
    start_ = point;
    end_ = point;
    pressing_ = true;
    return true;
}

bool FillSignMode::mouseMove(QMouseEvent* event) {
    if (forwarding_) {
        return controller_.annotSelect().mouseMove(event);
    }
    if (!pressing_) {
        updateHover(event->position());
        return false;
    }
    const QPointF p = view_.viewportToPage(page_, event->position());
    const QSizeF size = view_.pageSizePoints(page_);
    end_ = QPointF(std::clamp(p.x(), 0.0, size.width()), std::clamp(p.y(), 0.0, size.height()));
    view_.viewport()->update();
    return true;
}

bool FillSignMode::mouseRelease(QMouseEvent* event) {
    if (forwarding_) {
        forwarding_ = false;
        const bool moved = QLineF(pressViewport_, event->position()).length() >= kDragThresholdPx;
        const bool handled = controller_.annotSelect().mouseRelease(event);
        // Щелчок «Текстом» по своему полю — его правка (как у Acrobat).
        if (!moved && action_ == FillSignAction::Text && pressHit_ &&
            pressHit_->type == annot::AnnotType::FreeText && !pressHit_->locked() &&
            event->button() == Qt::LeftButton) {
            controller_.annotSelect().clearSelection();
            editText(pressHit_->ref);
        }
        pressHit_.reset();
        return handled;
    }
    if (!pressing_ || event->button() != Qt::LeftButton) {
        return false;
    }
    mouseMove(event);
    pressing_ = false;
    const bool clicked = QLineF(pressViewport_, event->position()).length() < kDragThresholdPx;
    if (!clicked && (action_ == FillSignAction::Signature || action_ == FillSignAction::Initials)) {
        placeSignature(page_, QRectF(start_, end_).normalized(),
                       action_ == FillSignAction::Initials ? fillsign::SignatureKind::Initials
                                                           : fillsign::SignatureKind::Signature);
    } else {
        clickAt(page_, start_);
    }
    view_.viewport()->update();
    return true;
}

void FillSignMode::clickAt(int page, QPointF point) {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const quint64 request = ++request_;
    const auto tolerance = static_cast<float>(2.0 / std::max(0.01f, view_.zoom()));
    annot::at(*s, page, PointF{static_cast<float>(point.x()), static_cast<float>(point.y())}, tolerance)
        .then(this, [this, page, point, request](QFuture<std::optional<annot::AnnotInfo>> f) {
            if (request != request_) {
                return;  // Esc, новый щелчок или смена документа
            }
            std::optional<annot::AnnotInfo> hit;
            try {
                hit = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — как мимо
            }
            // Ссылки и поля формы — не наше: текст и отметки ставятся поверх (эталон 3.5).
            if (hit && hit->type != annot::AnnotType::Link && hit->type != annot::AnnotType::Widget) {
                if (action_ == FillSignAction::Text && hit->type == annot::AnnotType::FreeText &&
                    !hit->locked()) {
                    editText(hit->ref);
                } else {
                    controller_.annotSelect().setSelection({*hit});
                    view_.viewport()->update();
                }
                return;
            }
            placeAt(page, point);
        });
}

bool FillSignMode::mouseDoubleClick(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && !controller_.annotSelect().selection().empty()) {
        forwarding_ = false;
        return controller_.annotSelect().mouseDoubleClick(event);
    }
    return mousePress(event);
}

void FillSignMode::placeAt(int page, QPointF point) {
    if (session() == nullptr || !lock_.isEmpty() || page < 0) {
        return;
    }
    const FillSignAction action = action_;
    withGeometry(page, [this, page, point, action](const fillsign::PageGeometry& g) {
        if (action != action_) {
            return;  // действие сменили, пока шла геометрия
        }
        if (action == FillSignAction::Text) {
            placeText(page, point, g);
        } else if (isMark(action)) {
            placeMark(page, point, g);
        } else {
            placeSignatureAt(page, point, g);
        }
    });
}

void FillSignMode::select(const annot::AnnotRef& ref) {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const QPointer<FillSignMode> self(this);
    annot::list(*s, ref.page).then(this, [self, ref](QFuture<std::vector<annot::AnnotInfo>> f) {
        if (self == nullptr) {
            return;
        }
        try {
            for (const annot::AnnotInfo& a : f.result()) {
                if (a.ref == ref) {
                    self->controller_.annotSelect().setSelection({a});
                    self->view_.viewport()->update();
                    return;
                }
            }
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет списка — без выбора
        }
    });
}

void FillSignMode::finishCreate(const QFuture<annot::AnnotRef>& future, bool selectIt) {
    QFuture<annot::AnnotRef>(future).then(this, [this, selectIt](QFuture<annot::AnnotRef> f) {
        annot::AnnotRef ref;
        try {
            ref = f.result();
        } catch (const std::exception& e) {
            reportError(QString::fromUtf8(e.what()));
            return;
        }
        if (selectIt) {
            select(ref);
        }
        controller_.annotSelect().annotList(ref.page);
        emit created(ref);
    });
}

// ---------------------------------------------------------------- клавиши, курсор, слой

bool FillSignMode::keyPress(QKeyEvent* event) {
    AnnotSelectTool& select = controller_.annotSelect();
    if (!select.selection().empty() &&
        (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace ||
         event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut))) {
        return select.keyPress(event);
    }
    return false;
}

bool FillSignMode::wantsShortcut(const QKeyEvent* event) const {
    return !controller_.annotSelect().selection().empty() && controller_.annotSelect().wantsShortcut(event);
}

void FillSignMode::updateHover(QPointF viewportPoint) {
    hoverViewport_ = viewportPoint;
    std::optional<fillsign::Placement> next;
    const int page = view_.pageAtViewport(viewportPoint);
    if (page >= 0 && lock_.isEmpty() && (action_ == FillSignAction::Text || isMark(action_))) {
        if (const fillsign::PageGeometry* g = geometry(page)) {
            const QPointF p = view_.viewportToPage(page, viewportPoint);
            const PointF at{static_cast<float>(p.x()), static_cast<float>(p.y())};
            const fillsign::Placement placement = action_ == FillSignAction::Text
                                                      ? fillsign::placeText(*g, at, fontSize_)
                                                      : fillsign::placeMark(*g, at, fontSize_);
            if (placement.snapped()) {
                next = placement;
            }
        }
    }
    const bool changed = next.has_value() != hover_.has_value() || page != hoverPage_ ||
                         (next && hover_ &&
                          (next->field.x0 != hover_->field.x0 || next->field.y0 != hover_->field.y0 ||
                           next->field.x1 != hover_->field.x1 || next->field.y1 != hover_->field.y1));
    hover_ = std::move(next);
    hoverPage_ = page;
    if (changed || action_ == FillSignAction::Signature || action_ == FillSignAction::Initials) {
        view_.viewport()->update();
    }
}

void FillSignMode::paintOverlay(QPainter& painter) {
    controller_.annotSelect().paintOverlay(painter);  // рамки и ручки выбранного
    if (action_ == FillSignAction::Signature || action_ == FillSignAction::Initials) {
        paintSignaturePreview(painter);
        return;
    }
    if (!hover_ || hoverPage_ < 0 || forwarding_) {
        return;
    }
    const QTransform toView = view_.pageTransform(hoverPage_);
    const auto& colors = Theme::colors();
    QPen pen(colors.accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
    pen.setCosmetic(true);
    painter.save();
    painter.setPen(pen);
    QColor fill = colors.accent.color();
    fill.setAlphaF(0.06);
    painter.setBrush(fill);
    if (hover_->kind == fillsign::FieldKind::Comb) {
        for (const RectF& c : hover_->cells) {
            painter.drawRect(toView.mapRect(QRectF(QPointF(c.x0, c.y0), QPointF(c.x1, c.y1))));
        }
    } else {
        const RectF& f = hover_->field;
        painter.drawRect(toView.mapRect(QRectF(QPointF(f.x0, f.y0), QPointF(f.x1, f.y1))));
    }
    painter.restore();
}

QCursor FillSignMode::cursor(QPointF point, Qt::KeyboardModifiers modifiers) const {
    if (!lock_.isEmpty()) {
        return QCursor(Qt::ForbiddenCursor);
    }
    const int page = view_.pageAtViewport(point);
    if (page < 0) {
        return QCursor(Qt::ArrowCursor);
    }
    AnnotSelectTool& select = controller_.annotSelect();
    if (select.handleAt(point) || nearRotateHandle(select, point)) {
        return select.cursor(point, modifiers);
    }
    for (const QRectF& frame : select.frames()) {
        if (frame.contains(point)) {
            return QCursor(Qt::SizeAllCursor);
        }
    }
    return action_ == FillSignAction::Text ? QCursor(Qt::IBeamCursor) : QCursor(Qt::CrossCursor);
}

bool FillSignMode::cancel() {
    ++request_;
    const bool had = pressing_ || forwarding_;
    pressing_ = false;
    if (forwarding_) {
        forwarding_ = false;
        controller_.annotSelect().cancel();
    }
    if (had) {
        view_.viewport()->update();
    }
    return had;
}

bool FillSignMode::escape(bool cancelled) {
    if (cancelled) {
        return true;
    }
    AnnotSelectTool& select = controller_.annotSelect();
    if (!select.selection().empty()) {
        select.clearSelection();
        view_.viewport()->update();
        return true;
    }
    return false;
}

}  // namespace ot::ui
