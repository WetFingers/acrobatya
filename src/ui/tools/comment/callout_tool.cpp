// Текстовая выноска (план 5.2, п. 1; эталон 3.4 «Инструмент»: Выноска (FreeText с /CL); comment-05): нажатие
// — конец стрелки, отпускание — левый край поля по середине (щелчок без протягивания — поле на kCalloutOffset
// выше и правее), колено — на kCalloutKnee левее поля (поле левее стрелки — правее поля); /IT
// /FreeTextCallout, /CL — конец стрелки, колено, точка у поля, /LE — открытая стрелка, рамка — 1 пт, если
// стиль инструмента «callout» её не задал (comments::newProps). Набор — InlineTextEditor, выноска создаётся
// при фиксации непустого текста.
#include <algorithm>

#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

void CommentMode::calloutRelease() {
    if (session() == nullptr) {
        return;
    }
    annot::AnnotProps props = newProps(CommentAction::Callout);  // рамка 1 пт по умолчанию (comment_settings)
    const float size = props.fontSize > 0 ? props.fontSize : 12;
    const float h = size * 1.25f + 2 * (std::max(0.0f, props.borderWidth) + 2);
    const QSizeF page = view_.pageSizePoints(page_);
    const auto pageW = static_cast<float>(page.width());
    const auto pageH = static_cast<float>(page.height());
    const PointF tip{static_cast<float>(start_.x()), static_cast<float>(start_.y())};
    float x0 = 0;
    float mid = 0;
    if (clicked()) {
        x0 = tip.x + kCalloutOffset;
        mid = tip.y - kCalloutOffset;
        if (mid - h / 2 < 0) {
            mid = tip.y + kCalloutOffset;  // у верхнего края — ниже стрелки
        }
        if (x0 + kTextBoxWidth > pageW) {
            x0 = tip.x - kCalloutOffset - kTextBoxWidth;  // у правого края — левее
        }
    } else {
        x0 = static_cast<float>(end_.x());
        mid = static_cast<float>(end_.y());
    }
    const float w = std::min(kTextBoxWidth, pageW);
    x0 = std::clamp(x0, 0.0f, pageW - w);
    mid = std::clamp(mid, h / 2, pageH - h / 2);
    props.rect = {x0, mid - h / 2, x0 + w, mid + h / 2};
    const bool left = tip.x <= x0;
    const PointF attach{left ? x0 : x0 + w, mid};
    const PointF knee{left ? std::max(tip.x, x0 - kCalloutKnee) : std::min(tip.x, x0 + w + kCalloutKnee),
                      mid};
    props.intent = "FreeTextCallout";
    props.callout = {tip, knee, attach};
    props.lineStartStyle = annot::LineEnding::OpenArrow;
    startTextEditor(page_, std::move(props));
}

}  // namespace ot::ui
