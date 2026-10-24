// Режим «Добавить комментарии»: общее — действие, страница и точка под мышью, текстовый слой (у «Выделения»
// вида — общий кеш), создание комментария задачей сессии, слой предпросмотра. Действия — note_tool.cpp,
// markup_tool.cpp, caret_tool.cpp.
#include "ui/tools/comment/comment_mode.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFuture>
#include <QPainter>
#include <QTimer>

#include "core/text/stext.h"
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

CommentMode::CommentMode(PageView& view, ToolController& controller) : ViewTool(view, controller) {}

CommentMode::~CommentMode() = default;

QString CommentMode::title() const { return actionTitle(action_); }

QString CommentMode::actionTitle(CommentAction action) {
    switch (action) {
        case CommentAction::Note:
            return QCoreApplication::translate("ot::ui::CommentMode", "Записка");
        case CommentAction::Highlight:
            return QCoreApplication::translate("ot::ui::CommentMode", "Выделить текст");
        case CommentAction::Underline:
            return QCoreApplication::translate("ot::ui::CommentMode", "Подчеркнуть текст");
        case CommentAction::StrikeOut:
            return QCoreApplication::translate("ot::ui::CommentMode", "Зачеркнуть текст");
        case CommentAction::Squiggly:
            return QCoreApplication::translate("ot::ui::CommentMode", "Волнистое подчёркивание");
        case CommentAction::InsertText:
            return QCoreApplication::translate("ot::ui::CommentMode", "Вставить текст");
        case CommentAction::ReplaceText:
            return QCoreApplication::translate("ot::ui::CommentMode", "Заменить текст");
        case CommentAction::Ink:
            return QCoreApplication::translate("ot::ui::CommentMode", "Рисовать");
        case CommentAction::Eraser:
            return QCoreApplication::translate("ot::ui::CommentMode", "Ластик");
        case CommentAction::TextBox:
            return QCoreApplication::translate("ot::ui::CommentMode", "Текстовое поле");
        case CommentAction::Callout:
            return QCoreApplication::translate("ot::ui::CommentMode", "Текстовая выноска");
        case CommentAction::Line:
            return QCoreApplication::translate("ot::ui::CommentMode", "Линия");
        case CommentAction::Arrow:
            return QCoreApplication::translate("ot::ui::CommentMode", "Стрелка");
        case CommentAction::Rectangle:
            return QCoreApplication::translate("ot::ui::CommentMode", "Прямоугольник");
        case CommentAction::Oval:
            return QCoreApplication::translate("ot::ui::CommentMode", "Овал");
        case CommentAction::Polygon:
            return QCoreApplication::translate("ot::ui::CommentMode", "Многоугольник");
        case CommentAction::Cloud:
            return QCoreApplication::translate("ot::ui::CommentMode", "Облако");
        case CommentAction::PolyLine:
            return QCoreApplication::translate("ot::ui::CommentMode", "Ломаная");
        case CommentAction::Stamp:
            return QCoreApplication::translate("ot::ui::CommentMode", "Штамп");
        case CommentAction::Attach:
            return QCoreApplication::translate("ot::ui::CommentMode", "Вложить файл");
    }
    return {};
}

QString CommentMode::actionKey(CommentAction action) {
    switch (action) {
        case CommentAction::Note:
            return QStringLiteral("note");
        case CommentAction::Highlight:
            return QStringLiteral("highlight");
        case CommentAction::Underline:
            return QStringLiteral("underline");
        case CommentAction::StrikeOut:
            return QStringLiteral("strikeout");
        case CommentAction::Squiggly:
            return QStringLiteral("squiggly");
        case CommentAction::InsertText:
            return QStringLiteral("insertText");
        case CommentAction::ReplaceText:
            return QStringLiteral("replaceText");
        case CommentAction::Ink:
            return QStringLiteral("ink");
        case CommentAction::Eraser:
            return QStringLiteral("eraser");
        case CommentAction::TextBox:
            return QStringLiteral("textBox");
        case CommentAction::Callout:
            return QStringLiteral("callout");
        case CommentAction::Line:
            return QStringLiteral("line");
        case CommentAction::Arrow:
            return QStringLiteral("arrow");
        case CommentAction::Rectangle:
            return QStringLiteral("rectangle");
        case CommentAction::Oval:
            return QStringLiteral("oval");
        case CommentAction::Polygon:
            return QStringLiteral("polygon");
        case CommentAction::Cloud:
            return QStringLiteral("cloud");
        case CommentAction::PolyLine:
            return QStringLiteral("polyline");
        case CommentAction::Stamp:
            return QStringLiteral("stamp");
        case CommentAction::Attach:
            return QStringLiteral("attach");
    }
    return {};
}

annot::AnnotType CommentMode::typeOf(CommentAction action) {
    switch (action) {
        case CommentAction::Note:
            return annot::AnnotType::Text;
        case CommentAction::Highlight:
            return annot::AnnotType::Highlight;
        case CommentAction::Underline:
            return annot::AnnotType::Underline;
        case CommentAction::StrikeOut:
        case CommentAction::ReplaceText:
            return annot::AnnotType::StrikeOut;
        case CommentAction::Squiggly:
            return annot::AnnotType::Squiggly;
        case CommentAction::InsertText:
            return annot::AnnotType::Caret;
        case CommentAction::Ink:
        case CommentAction::Eraser:
            return annot::AnnotType::Ink;
        case CommentAction::TextBox:
        case CommentAction::Callout:
            return annot::AnnotType::FreeText;
        case CommentAction::Line:
        case CommentAction::Arrow:
            return annot::AnnotType::Line;
        case CommentAction::Rectangle:
            return annot::AnnotType::Square;
        case CommentAction::Oval:
            return annot::AnnotType::Circle;
        case CommentAction::Polygon:
        case CommentAction::Cloud:
            return annot::AnnotType::Polygon;
        case CommentAction::PolyLine:
            return annot::AnnotType::PolyLine;
        case CommentAction::Stamp:
            return annot::AnnotType::Stamp;
        case CommentAction::Attach:
            return annot::AnnotType::FileAttachment;
    }
    return annot::AnnotType::Unknown;
}

bool CommentMode::isMarkup(CommentAction action) noexcept {
    return action == CommentAction::Highlight || action == CommentAction::Underline ||
           action == CommentAction::StrikeOut || action == CommentAction::Squiggly;
}

bool CommentMode::isDragShape(CommentAction action) noexcept {
    return action == CommentAction::Line || action == CommentAction::Arrow ||
           action == CommentAction::Rectangle || action == CommentAction::Oval;
}

bool CommentMode::isPolyShape(CommentAction action) noexcept {
    return action == CommentAction::Polygon || action == CommentAction::Cloud ||
           action == CommentAction::PolyLine;
}

bool CommentMode::oneShot() const noexcept {
    return action_ == CommentAction::Note || action_ == CommentAction::InsertText ||
           action_ == CommentAction::ReplaceText || action_ == CommentAction::TextBox ||
           action_ == CommentAction::Callout || isDragShape(action_) || isPolyShape(action_) ||
           action_ == CommentAction::Stamp || action_ == CommentAction::Attach;
}

void CommentMode::setAction(CommentAction action) {
    if (action == action_) {
        return;
    }
    cancel();
    action_ = action;
    view_.viewport()->setCursor(cursor(view_.viewport()->mapFromGlobal(QCursor::pos()), Qt::NoModifier));
    emit actionChanged(action);
}

void CommentMode::setLocked(const QString& reason) {
    lock_ = reason;
    if (!lock_.isEmpty()) {
        cancel();
    }
}

annot::AnnotProps CommentMode::newProps(CommentAction action) const {
    return comments::newProps(Settings(), typeOf(action), actionKey(action));
}

DocumentSession* CommentMode::session() const {
    DocumentSession* s = view_.document();
    return s != nullptr && s->isOpen() ? s : nullptr;
}

void CommentMode::activate() {
    // Текстовые слои видимых страниц — заранее (разметка и каретка идут по ним).
    for (const int page : view_.visiblePages()) {
        textPage(page);
    }
}

void CommentMode::deactivate() { cancel(); }

CommentMode::Locator CommentMode::locate(QPointF viewportPoint) const {
    Locator at;
    at.page = view_.pageAtViewport(viewportPoint);
    if (at.page >= 0) {
        at.point = view_.viewportToPage(at.page, viewportPoint);
    }
    return at;
}

const TextPage* CommentMode::textPage(int page) {
    if (page < 0) {
        return nullptr;
    }
    return controller_.select().textPage(page);
}

bool CommentMode::mousePress(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || session() == nullptr) {
        return false;
    }
    if (!lock_.isEmpty()) {
        return true;  // правка запрещена: щелчок поглощён, курсор «нельзя»
    }
    const Locator at = locate(event->position());
    if (at.page < 0) {
        return true;
    }
    pressViewport_ = event->position();
    if (isPolyShape(action_)) {
        polyPress(at, (event->modifiers() & Qt::ShiftModifier) != 0);
        return true;
    }
    switch (action_) {
        case CommentAction::Note:
            notePress(at);
            return true;
        case CommentAction::InsertText:
            insertCaret(at);
            return true;
        case CommentAction::Attach:
            attachPress(at);
            return true;
        default:
            break;
    }
    // Разметка, замена, карандаш, ластик, поле, выноска и фигуры — протягивание.
    shift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
    pressing_ = true;
    page_ = at.page;
    start_ = at.point;
    end_ = at.point;
    preview_ = {};
    area_ = false;
    stroke_ = {at.point};
    if (isMarkup(action_) || action_ == CommentAction::ReplaceText) {
        textPage(page_);
    }
    return true;
}

bool CommentMode::mouseMove(QMouseEvent* event) {
    if (action_ == CommentAction::Stamp && !pressing_) {
        hover_ = event->position();
        view_.viewport()->update();  // рамка штампа за курсором
        return false;
    }
    if (!poly_.empty()) {
        hover_ = event->position();
        shift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
        view_.viewport()->update();  // резинка к следующей вершине
        return true;
    }
    if (!pressing_) {
        if (action_ == CommentAction::Eraser) {
            hover_ = event->position();
            view_.viewport()->update();  // круг ластика за курсором
        }
        return false;
    }
    const QPointF p = view_.viewportToPage(page_, event->position());
    const QSizeF size = view_.pageSizePoints(page_);
    end_ = QPointF(std::clamp(p.x(), 0.0, size.width()), std::clamp(p.y(), 0.0, size.height()));
    hover_ = event->position();
    shift_ = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (action_ == CommentAction::Ink || action_ == CommentAction::Eraser) {
        stroke_.push_back(end_);
    } else if (action_ != CommentAction::TextBox && action_ != CommentAction::Callout) {
        markupMove();
    }
    view_.viewport()->update();
    return true;
}

bool CommentMode::mouseRelease(QMouseEvent* event) {
    if (!pressing_ || event->button() != Qt::LeftButton) {
        return false;
    }
    mouseMove(event);
    pressing_ = false;
    if (action_ == CommentAction::Ink) {
        inkRelease();
    } else if (action_ == CommentAction::Eraser) {
        eraserRelease();
    } else if (action_ == CommentAction::TextBox) {
        textBoxRelease();
    } else if (action_ == CommentAction::Callout) {
        calloutRelease();
    } else if (isDragShape(action_)) {
        shapeRelease();
    } else if (action_ == CommentAction::Stamp) {
        stampRelease();
    } else {
        attempts_ = 0;
        finishDrag(request_);
        return true;
    }
    stroke_.clear();
    view_.viewport()->update();
    return true;
}

// Конец протягивания: слой текста страницы мог уйти с правкой страницы (прошлый комментарий) — тогда ждём его
// (до 3 с), а не бросаем выделение.
void CommentMode::finishDrag(quint64 request) {
    if (request != request_) {
        return;  // Esc, смена действия или документа
    }
    if (textPage(page_) == nullptr && attempts_ < kTextWaitTries) {
        ++attempts_;
        QTimer::singleShot(kTextWaitStepMs, this, [this, request] { finishDrag(request); });
        return;
    }
    markupMove();
    if (action_ == CommentAction::ReplaceText) {
        replaceRelease();
    } else {
        markupRelease();
    }
    preview_ = {};
    view_.viewport()->update();
}

bool CommentMode::clicked() const {
    const QTransform toView = view_.pageTransform(page_);
    return QLineF(toView.map(start_), toView.map(end_)).length() < kDragThresholdPx;
}

bool CommentMode::cancel() {
    const bool had = pressing_ || !poly_.empty();
    pressing_ = false;
    poly_.clear();
    polyPage_ = -1;
    preview_ = {};
    stroke_.clear();
    ++request_;
    if (had) {
        view_.viewport()->update();
    }
    return had;
}

void CommentMode::documentChanged() {
    cancel();
    if (InlineTextEditor* editor = InlineTextEditor::active(view_)) {
        editor->commit();  // набранное в поле не теряется
    }
    PopupLayer::of(view_).closeAll();
}

void CommentMode::pageChanged(int page) {
    if (pressing_ && page == page_) {
        markupMove();  // слой текста страницы пришёл заново
    }
}

void CommentMode::paintOverlay(QPainter& painter) {
    if (action_ == CommentAction::Eraser) {
        paintEraser(painter);
        return;
    }
    if (isPolyShape(action_)) {
        paintShape(painter);
        return;
    }
    if (action_ == CommentAction::Stamp) {
        paintStampPreview(painter);
        return;
    }
    if (!pressing_ || page_ < 0) {
        return;
    }
    if (action_ == CommentAction::Ink) {
        paintInk(painter);
        return;
    }
    if (action_ == CommentAction::TextBox || action_ == CommentAction::Callout) {
        paintTextPreview(painter);
        return;
    }
    if (isDragShape(action_)) {
        paintShape(painter);
        return;
    }
    const QTransform toView = view_.pageTransform(page_);
    const auto& colors = Theme::colors();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (area_ || (preview_.quads.empty() && action_ == CommentAction::Highlight)) {
        QPen pen(colors.accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(toView.mapRect(QRectF(start_, end_).normalized()));
    }
    QColor fill = colors.selection.color();
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    for (const QuadF& q : preview_.quads) {
        QPolygonF poly;
        poly << toView.map(QPointF(q.ul.x, q.ul.y)) << toView.map(QPointF(q.ur.x, q.ur.y))
             << toView.map(QPointF(q.lr.x, q.lr.y)) << toView.map(QPointF(q.ll.x, q.ll.y));
        painter.drawPolygon(poly);
    }
    painter.restore();
}

QCursor CommentMode::cursor(QPointF point, Qt::KeyboardModifiers /*modifiers*/) const {
    if (!lock_.isEmpty()) {
        return QCursor(Qt::ForbiddenCursor);
    }
    if (view_.pageAtViewport(point) < 0) {
        return QCursor(Qt::ArrowCursor);
    }
    switch (action_) {
        case CommentAction::Note:
        case CommentAction::Ink:
        case CommentAction::Eraser:
        case CommentAction::TextBox:
        case CommentAction::Callout:
        case CommentAction::Line:
        case CommentAction::Arrow:
        case CommentAction::Rectangle:
        case CommentAction::Oval:
        case CommentAction::Polygon:
        case CommentAction::Cloud:
        case CommentAction::PolyLine:
        case CommentAction::Stamp:
        case CommentAction::Attach:
            return QCursor(Qt::CrossCursor);
        default:
            return QCursor(Qt::IBeamCursor);
    }
}

void CommentMode::reportError(const QString& what) { emit failed(what); }

// Ответ создания: открыть окно записки (note) и сказать об успехе; однократный инструмент — назад.
void CommentMode::done(const QFuture<annot::AnnotRef>& future, bool openNote) {
    const QPointer<PageView> view(&view_);
    QFuture<annot::AnnotRef>(future).then(this, [this, view, openNote](QFuture<annot::AnnotRef> f) {
        annot::AnnotRef ref;
        try {
            ref = f.result();
        } catch (const std::exception& e) {
            reportError(QString::fromUtf8(e.what()));
            return;
        }
        if (view == nullptr) {
            return;
        }
        if (openNote) {
            PopupLayer::of(*view).open(ref, true);
        }
        emit created(ref);
    });
    controller_.finished();
}

void CommentMode::markupSelection(CommentAction action, const std::map<int, TextSelection>& selection,
                                  bool note) {
    DocumentSession* s = session();
    if (s == nullptr || !isMarkup(action) || !lock_.isEmpty()) {
        return;
    }
    bool first = true;
    for (const auto& [page, sel] : selection) {
        if (sel.quads.empty()) {
            continue;
        }
        annot::AnnotProps props = newProps(action);
        props.quads = sel.quads;
        done(annot::create(*s, page, std::move(props)), note && first);
        first = false;
    }
}

}  // namespace ot::ui
