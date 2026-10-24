// Текстовое поле (план 5.2, п. 1; эталон 3.4 «Инструмент»: Текст (J, FreeText); comment-04, comment-09):
// протягивание — рамка поля; щелчок — хит-тест задачей документа: своё поле FreeText — его правка на
// странице, иначе новое поле шириной kTextBoxWidth и высотой в строку с левым верхним углом в точке щелчка (в
// пределах страницы). Набор — InlineTextEditor: поле создаётся при фиксации непустого текста (шаг «Add text
// box»), стиль — умолчания ядра + стиль инструмента «textBox» (шрифт, кегль, цвет текста, выравнивание,
// заливка, рамка).
#include <QFuture>
#include <QPainter>

#include <algorithm>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

// Высота поля в одну строку: кегль с межстрочным и отступы (рамка и 2 пт сверху и снизу).
float oneLineHeight(const annot::AnnotProps& p) {
    const float size = p.fontSize > 0 ? p.fontSize : 12;
    return size * 1.25f + 2 * (std::max(0.0f, p.borderWidth) + 2);
}

}  // namespace

void CommentMode::startTextEditor(int page, annot::AnnotProps props) {
    InlineTextEditor* editor = InlineTextEditor::startNew(view_, page, std::move(props));
    connect(editor, &InlineTextEditor::committed, this, [this](const annot::AnnotRef& ref) {
        if (ref.valid()) {
            emit created(ref);
        }
    });
    controller_.finished();
}

void CommentMode::editTextBox(const annot::AnnotRef& ref) {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const QPointer<PageView> view(&view_);
    annot::props(*s, ref).then(this, [view, ref](QFuture<std::optional<annot::AnnotProps>> f) {
        std::optional<annot::AnnotProps> p;
        try {
            p = f.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): поля нет — править нечего
        }
        if (view != nullptr && p && p->type == annot::AnnotType::FreeText) {
            InlineTextEditor::startEdit(*view, ref, *p);
        }
    });
}

void CommentMode::textBoxRelease() {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    if (!clicked()) {
        annot::AnnotProps props = newProps(CommentAction::TextBox);
        const QRectF r = QRectF(start_, end_).normalized();
        props.rect = {static_cast<float>(r.left()), static_cast<float>(r.top()),
                      static_cast<float>(std::max(r.right(), r.left() + 20)),
                      static_cast<float>(std::max(r.bottom(), r.top() + oneLineHeight(props)))};
        startTextEditor(page_, std::move(props));
        return;
    }
    const quint64 request = ++request_;
    const Locator at{page_, start_};
    const auto tolerance = static_cast<float>(2.0 / std::max(0.01f, view_.zoom()));
    annot::at(*s, at.page, PointF{static_cast<float>(at.point.x()), static_cast<float>(at.point.y())},
              tolerance)
        .then(this, [this, at, request](QFuture<std::optional<annot::AnnotInfo>> f) {
            std::optional<annot::AnnotInfo> hit;
            try {
                hit = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — как мимо
            }
            textBoxHit(hit, at, request);
        });
}

void CommentMode::textBoxHit(const std::optional<annot::AnnotInfo>& hit, Locator at, quint64 request) {
    if (request != request_ || session() == nullptr) {
        return;
    }
    if (hit && hit->type == annot::AnnotType::FreeText && !hit->locked()) {
        editTextBox(hit->ref);
        controller_.finished();
        return;
    }
    annot::AnnotProps props = newProps(CommentAction::TextBox);
    const QSizeF size = view_.pageSizePoints(at.page);
    const float w = std::min(kTextBoxWidth, static_cast<float>(size.width()));
    const float h = oneLineHeight(props);
    const float x = std::clamp(static_cast<float>(at.point.x()), 0.0f, static_cast<float>(size.width()) - w);
    const float y = std::clamp(static_cast<float>(at.point.y()), 0.0f, static_cast<float>(size.height()) - h);
    props.rect = {x, y, x + w, y + h};
    startTextEditor(at.page, std::move(props));
}

// Предпросмотр протягивания: рамка поля; выноска — линия от конца стрелки и рамка поля.
void CommentMode::paintTextPreview(QPainter& painter) {
    const QTransform toView = view_.pageTransform(page_);
    QPen pen(Theme::colors().accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
    pen.setCosmetic(true);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (action_ == CommentAction::TextBox) {
        painter.drawRect(toView.mapRect(QRectF(start_, end_).normalized()));
    } else {
        const annot::AnnotProps style = newProps(CommentAction::Callout);
        const QRectF box(end_.x(), end_.y() - oneLineHeight(style) / 2, kTextBoxWidth, oneLineHeight(style));
        painter.drawLine(toView.map(start_), toView.map(end_));
        painter.drawRect(toView.mapRect(box));
    }
    painter.restore();
}

}  // namespace ot::ui
