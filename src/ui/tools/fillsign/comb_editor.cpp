#include "ui/tools/fillsign/comb_editor.h"

#include <QFocusEvent>
#include <QFontMetricsF>
#include <QFuture>
#include <QKeyEvent>
#include <QScrollBar>

#include <algorithm>
#include <cmath>

#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"

namespace ot::ui {

CombEditor::CombEditor(PageView& view, int page, fillsign::Placement placement, const QColor& color)
    : QLineEdit(view.viewport()), view_(view), page_(page), placement_(std::move(placement)), color_(color) {
    setObjectName(QStringLiteral("combEditor"));
    setAccessibleName(tr("Текст по клеткам"));
    setFrame(false);
    setTextMargins(0, 0, 0, 0);
    setMaxLength(static_cast<int>(placement_.cells.size()));
    applyFormat();
    connect(view_.horizontalScrollBar(), &QScrollBar::valueChanged, this, &CombEditor::applyFormat);
    connect(view_.verticalScrollBar(), &QScrollBar::valueChanged, this, &CombEditor::applyFormat);
    connect(&view_, &PageView::zoomChanged, this, &CombEditor::applyFormat);
    connect(&view_, &PageView::layoutChanged, this, &CombEditor::applyFormat);
    show();
    raise();
    setFocus(Qt::OtherFocusReason);
}

CombEditor* CombEditor::start(PageView& view, int page, fillsign::Placement placement, const QColor& color) {
    if (CombEditor* open = active(view)) {
        open->commit();
    }
    return new CombEditor(view, page, std::move(placement), color);
}

CombEditor* CombEditor::active(const PageView& view) {
    for (CombEditor* e : view.viewport()->findChildren<CombEditor*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (!e->done_) {
            return e;
        }
    }
    return nullptr;
}

// Вид по клеткам: Courier New кеглем клетки, интервал — шаг минус ширина знака; рамка — по клеткам.
void CombEditor::applyFormat() {
    if (done_ || placement_.cells.empty()) {
        return;
    }
    const QTransform t = view_.pageTransform(page_);
    const double s = std::max(0.01, std::hypot(t.m11(), t.m12()));
    QFont font(QString::fromLatin1(fillsign::kCombFont));
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(placement_.fontSize * s))));
    const double advance = QFontMetricsF(font).horizontalAdvance(QLatin1Char('0'));
    const double step = placement_.cells.front().width() * s;
    font.setLetterSpacing(QFont::AbsoluteSpacing, std::max(0.0, step - advance));
    setFont(font);
    const RectF& f = placement_.field;
    const QRectF box = t.mapRect(QRectF(QPointF(f.x0, f.y0), QPointF(f.x1, f.y1)));
    // Первый знак — по центру первой клетки (у QLineEdit внутренний отступ 2 px).
    const double inset = std::max(0.0, (step - advance) / 2) - 2;
    setGeometry(QRectF(box.left() + inset, box.top(), box.width() - inset, box.height()).toAlignedRect());
    setStyleSheet(
        QStringLiteral("QLineEdit#combEditor { background: transparent; color: %1; border: 1px dashed %2; }")
            .arg(color_.name(QColor::HexRgb), Theme::colors().accent.color().name(QColor::HexRgb)));
}

void CombEditor::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commit();
        view_.setFocus();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void CombEditor::focusOutEvent(QFocusEvent* event) {
    QLineEdit::focusOutEvent(event);
    if (event->reason() == Qt::PopupFocusReason || event->reason() == Qt::ActiveWindowFocusReason) {
        return;
    }
    commit();
}

void CombEditor::commit() {
    if (done_) {
        return;
    }
    done_ = true;
    hide();
    DocumentSession* s = view_.document();
    const std::u32string typed = text().toStdU32String();
    if (s == nullptr || !s->isOpen() || QString::fromStdU32String(typed).trimmed().isEmpty()) {
        deleteLater();
        return;
    }
    const annot::AnnotColor color =
        annot::AnnotColor::fromRgb(static_cast<std::uint32_t>(color_.rgb() & 0xffffff));
    std::vector<annot::AnnotProps> items = fillsign::combProps(placement_, typed, color);
    QPointer<CombEditor> self(this);
    fillsign::createAll(*s, page_, std::move(items))
        .then(this, [self](QFuture<std::vector<annot::AnnotRef>> f) {
            std::vector<annot::AnnotRef> refs;
            try {
                refs = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): не записалось — пусто
            }
            if (self != nullptr) {
                emit self->committed(refs);
                self->deleteLater();
            }
        });
}

}  // namespace ot::ui
