#include "ui/tools/comment/inline_text_editor.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QFocusEvent>
#include <QFuture>
#include <QKeyEvent>
#include <QMenu>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextOption>

#include <algorithm>
#include <cmath>

#include "core/annot/freetext.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/view_style.h"

namespace ot::ui {

namespace {

QColor qcolorOf(const annot::AnnotColor& c) {
    return c.empty() ? QColor() : QColor::fromRgb(static_cast<QRgb>(c.toRgb()));
}

annot::AnnotColor colorOf(const QColor& c) {
    return c.isValid() ? annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(c.rgb() & 0xffffff))
                       : annot::AnnotColor{};
}

Qt::Alignment alignmentOf(int align) {
    return align == 1 ? Qt::AlignHCenter : align == 2 ? Qt::AlignRight : Qt::AlignLeft;
}

bool sameColor(const annot::AnnotColor& a, const annot::AnnotColor& b) {
    return a.toRgb() == b.toRgb() && a.n == b.n;
}

// Колено и точка у поля выноски — по рамке поля (поле выросло или сдвинулось).
void fitCallout(annot::AnnotProps& p) {
    if (p.callout.size() != 3) {
        return;
    }
    const float mid = (p.rect.y0 + p.rect.y1) / 2;
    const PointF knee = p.callout[1];
    p.callout[1] = {knee.x, mid};
    p.callout[2] = {knee.x <= p.rect.x0 ? p.rect.x0 : p.rect.x1, mid};
}

}  // namespace

InlineTextEditor::InlineTextEditor(PageView& view, int page, annot::AnnotProps props,
                                   std::optional<annot::AnnotRef> ref)
    : QTextEdit(view.viewport()), view_(view), page_(page), props_(std::move(props)), ref_(std::move(ref)) {
    setObjectName(QStringLiteral("inlineTextEditor"));
    setAccessibleName(tr("Текст поля"));
    original_ = props_;
    setAcceptRichText(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setLineWrapMode(QTextEdit::WidgetWidth);
    setFrameShape(QFrame::NoFrame);
    setPlainText(QString::fromStdString(props_.contents));
    applyFormat();
    connect(this, &QTextEdit::textChanged, this, &InlineTextEditor::grow);
    connect(view_.horizontalScrollBar(), &QScrollBar::valueChanged, this, &InlineTextEditor::reposition);
    connect(view_.verticalScrollBar(), &QScrollBar::valueChanged, this, &InlineTextEditor::reposition);
    connect(&view_, &PageView::zoomChanged, this, [this] { applyFormat(); });
    connect(&view_, &PageView::layoutChanged, this, &InlineTextEditor::reposition);
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
    moveCursor(QTextCursor::End);
}

InlineTextEditor* InlineTextEditor::startNew(PageView& view, int page, annot::AnnotProps props) {
    if (InlineTextEditor* open = active(view)) {
        open->commit();
    }
    props.contents.clear();
    return new InlineTextEditor(view, page, std::move(props), std::nullopt);
}

InlineTextEditor* InlineTextEditor::startEdit(PageView& view, const annot::AnnotRef& ref,
                                              annot::AnnotProps props) {
    if (InlineTextEditor* open = active(view)) {
        if (open->ref_ && *open->ref_ == ref) {
            return open;
        }
        open->commit();
    }
    return new InlineTextEditor(view, ref.page, std::move(props), ref);
}

InlineTextEditor* InlineTextEditor::active(const PageView& view) {
    for (InlineTextEditor* e :
         view.viewport()->findChildren<InlineTextEditor*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!e->done_) {
            return e;
        }
    }
    return nullptr;
}

double InlineTextEditor::scale() const {
    const QTransform t = view_.pageTransform(page_);
    return std::max(0.01, std::hypot(t.m11(), t.m12()));
}

void InlineTextEditor::setFormat(const QString& family, double size, const QColor& color, int align) {
    props_.fontName = family.toStdString();
    props_.fontSize = static_cast<float>(size);
    props_.textColor = colorOf(color);
    props_.align = std::clamp(align, 0, 2);
    applyFormat();
}

void InlineTextEditor::applyFormat() {
    const double s = scale();
    QFont font(QString::fromUtf8(annot::textFont(annot::textFontFamily(props_.fontName)).family));
    const double px = std::max(1.0, static_cast<double>(props_.fontSize > 0 ? props_.fontSize : 12) * s);
    font.setPointSizeF(px * 72.0 / std::max(1, logicalDpiY()));
    setFont(font);
    document()->setDefaultFont(font);
    QTextOption option = document()->defaultTextOption();
    option.setAlignment(alignmentOf(props_.align));
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    document()->setDefaultTextOption(option);
    // Отступ — как у вида поля (freetext.cpp): толщина рамки и 2 пт.
    document()->setDocumentMargin((std::max(0.0f, props_.borderWidth) + 2) * s);
    const QColor ink = props_.textColor.empty() ? QColor(Qt::black) : qcolorOf(props_.textColor);
    // Своё поле — фон заливкой поля или бумагой (вид поля под редактором не двоится); новое — прозрачно.
    QColor back = Qt::transparent;
    if (ref_) {
        back = props_.color.empty() ? paperColor(view_.renderStyle()) : qcolorOf(props_.color);
    }
    setStyleSheet(
        QStringLiteral(
            "QTextEdit#inlineTextEditor { background-color: %1; color: %2; border: 1px dashed %3; }")
            .arg(back.name(QColor::HexArgb), ink.name(QColor::HexRgb),
                 Theme::colors().accent.color().name(QColor::HexRgb)));
    reposition();
    grow();
}

void InlineTextEditor::reposition() {
    if (done_) {
        return;
    }
    const QTransform t = view_.pageTransform(page_);
    const QRectF box =
        t.mapRect(QRectF(QPointF(props_.rect.x0, props_.rect.y0), QPointF(props_.rect.x1, props_.rect.y1)));
    setGeometry(box.toAlignedRect());
}

void InlineTextEditor::grow() {
    if (done_) {
        return;
    }
    document()->setTextWidth(viewport()->width());
    const double needed = document()->size().height() + 2;  // + рамка редактора
    if (needed > height()) {
        props_.rect.y1 = props_.rect.y0 + static_cast<float>(needed / scale());
        reposition();
    }
}

bool InlineTextEditor::focusStaysNear() const {
    // Панель инструмента («Формат текста») и всплывающие меню фокус не забирают — правка продолжается.
    for (const QWidget* w = QApplication::focusWidget(); w != nullptr; w = w->parentWidget()) {
        if (w == this || w->objectName() == QLatin1String("commentToolPanel") ||
            w->objectName() == QLatin1String("fillSignToolPanel") || qobject_cast<const QMenu*>(w)) {
            return true;
        }
    }
    return false;
}

void InlineTextEditor::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape ||
        ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
         (event->modifiers() & Qt::ControlModifier) != 0)) {
        commit();
        view_.setFocus();
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void InlineTextEditor::focusOutEvent(QFocusEvent* event) {
    QTextEdit::focusOutEvent(event);
    if (event->reason() == Qt::PopupFocusReason || event->reason() == Qt::ActiveWindowFocusReason ||
        focusStaysNear()) {
        return;
    }
    commit();
}

void InlineTextEditor::commit() {
    if (done_) {
        return;
    }
    done_ = true;
    hide();
    DocumentSession* s = view_.document();
    const QString text = toPlainText();
    QPointer<InlineTextEditor> self(this);
    auto finish = [self](const annot::AnnotRef& ref) {
        if (self != nullptr) {
            emit self->committed(ref);
            self->deleteLater();
        }
    };
    if (s == nullptr || !s->isOpen()) {
        deleteLater();
        return;
    }
    annot::AnnotProps p = props_;
    p.contents = text.toStdString();
    fitCallout(p);
    if (!ref_) {
        if (text.trimmed().isEmpty()) {
            deleteLater();
            return;
        }
        annot::create(*s, page_, std::move(p)).then(this, [finish](QFuture<annot::AnnotRef> f) {
            annot::AnnotRef ref;
            try {
                ref = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): поле не создано — ref пуст
            }
            finish(ref);
        });
        return;
    }
    const annot::AnnotRef ref = *ref_;
    if (text.trimmed().isEmpty()) {
        annot::remove(*s, {ref}).then(this, [finish](QFuture<void> f) {
            try {
                f.waitForFinished();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): не удалилось — поле осталось
            }
            finish({});
        });
        return;
    }
    annot::AnnotPatch patch;
    if (p.contents != original_.contents) {
        patch.contents = p.contents;
    }
    if (std::fabs(p.rect.y1 - original_.rect.y1) > 0.01f ||
        std::fabs(p.rect.x1 - original_.rect.x1) > 0.01f) {
        patch.rect = p.rect;
        if (!p.callout.empty()) {
            patch.callout = p.callout;
        }
    }
    if (annot::textFontFamily(p.fontName) != annot::textFontFamily(original_.fontName)) {
        patch.fontName = p.fontName;
    }
    if (std::fabs(p.fontSize - original_.fontSize) > 0.01f) {
        patch.fontSize = p.fontSize;
    }
    if (!sameColor(p.textColor, original_.textColor)) {
        patch.textColor = p.textColor;
    }
    if (p.align != original_.align) {
        patch.align = p.align;
    }
    if (patch.empty()) {
        finish(ref);
        return;
    }
    annot::update(*s, ref, std::move(patch), QString::fromLatin1(annot::kEditTextBoxStep))
        .then(this, [finish, ref](QFuture<void> f) {
            try {
                f.waitForFinished();
            } catch (
                const std::exception&) {  // NOLINT(bugprone-empty-catch): правка не записалась — поле прежнее
            }
            finish(ref);
        });
}

}  // namespace ot::ui
