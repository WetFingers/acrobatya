// Разметка текста (план 5.1; эталон 3.4 «Инструмент»; comment-02): протягивание по тексту — выделение потоком
// от точки до точки (core/text/selection.h selectStream: квады высотой в строку, как у «Выделения»), по
// отпусканию — /Highlight, /Underline, /StrikeOut или /Squiggly с этими квадами (/QuadPoints) одним шагом
// истории. Текстового слоя нет (скан) или мимо текста — у «Выделить» область рамкой (один квад), у прочих —
// ничего. Щелчок без протягивания — ничего.
#include <QApplication>

#include "core/text/stext.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

TextSelection CommentMode::selectionNow() {
    const TextPage* text = textPage(page_);
    if (text == nullptr) {
        return {};
    }
    const PointF a{static_cast<float>(start_.x()), static_cast<float>(start_.y())};
    const PointF b{static_cast<float>(end_.x()), static_cast<float>(end_.y())};
    return selectStream(*text, a, b, SnapMode::Chars);
}

void CommentMode::markupMove() {
    preview_ = selectionNow();
    area_ = preview_.empty() && action_ == CommentAction::Highlight;
}

void CommentMode::markupRelease() {
    DocumentSession* s = session();
    const bool moved = (view_.pageTransform(page_).map(end_) - pressViewport_).manhattanLength() >=
                       QApplication::startDragDistance();
    if (s == nullptr || !moved) {
        return;
    }
    annot::AnnotProps props = newProps(action_);
    if (!preview_.quads.empty()) {
        props.quads = preview_.quads;
    } else if (action_ == CommentAction::Highlight) {
        const QRectF r = QRectF(start_, end_).normalized();
        if (r.width() < 1 || r.height() < 1) {
            return;
        }
        const auto x0 = static_cast<float>(r.left());
        const auto y0 = static_cast<float>(r.top());
        const auto x1 = static_cast<float>(r.right());
        const auto y1 = static_cast<float>(r.bottom());
        props.quads = {{{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}};
    } else {
        return;  // подчеркнуть и зачеркнуть можно только текст
    }
    done(annot::create(*s, page_, std::move(props)), false);
}

}  // namespace ot::ui
