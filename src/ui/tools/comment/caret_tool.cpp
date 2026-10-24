// Правки текста (план 5.1; эталон 3.4 «Инструмент»; comment-03): «Вставить текст» — щелчок ставит /Caret у
// ближайшей границы символа (каретка высотой в половину кегля на базовой линии, без текста — в точке
// щелчка) и открывает окно для вставляемого текста; «Заменить текст» — протянутое выделение зачёркивается
// (/StrikeOut с /IT /StrikeOutTextEdit), каретка — после последнего символа, StrikeOut получает /IRT на неё и
// /RT /Group (Acrobat), обе — одним шагом истории «Replace text»; окно — у каретки, для нового текста.
#include <QApplication>

#include <algorithm>
#include <cmath>

#include "core/text/stext.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

constexpr float kCaretFallback = 12;  // кегль, если текста нет

annot::AnnotProps caretProps(annot::AnnotProps props, const RectF& rect) {
    props.type = annot::AnnotType::Caret;
    props.rect = rect;
    return props;
}

}  // namespace

// Каретка у границы boundary: по высоте — от базовой линии вверх на половину кегля, ширина — 0,6 кегля.
RectF CommentMode::caretRectAt(const TextPage& text, int boundary) const {
    const auto& chars = text.chars();
    if (chars.empty()) {
        return {};
    }
    const bool after = boundary >= static_cast<int>(chars.size());
    const TextChar& c = chars[static_cast<std::size_t>(
        std::clamp(after ? boundary - 1 : boundary, 0, static_cast<int>(chars.size()) - 1))];
    const float size = c.size > 0 ? c.size : kCaretFallback;
    const RectF box = c.quad.bounds();
    const float x = after ? box.x1 : box.x0;
    const float baseline = c.origin.y;
    const float w = size * 0.6f;
    return {x - w / 2, baseline - size * 0.5f, x + w / 2, baseline + size * 0.1f};
}

void CommentMode::insertCaret(const Locator& at) {
    DocumentSession* s = session();
    RectF rect;
    if (const TextPage* text = textPage(at.page); text != nullptr && !text->chars().empty()) {
        const PointF p{static_cast<float>(at.point.x()), static_cast<float>(at.point.y())};
        rect = caretRectAt(*text, closestBoundary(*text, p));
    }
    if (rect.empty()) {
        const auto x = static_cast<float>(at.point.x());
        const auto y = static_cast<float>(at.point.y());
        rect = {x - kCaretFallback * 0.3f, y - kCaretFallback * 0.5f, x + kCaretFallback * 0.3f,
                y + kCaretFallback * 0.1f};
    }
    annot::AnnotProps props = caretProps(newProps(CommentAction::InsertText), rect);
    done(annot::create(*s, at.page, std::move(props)), true);
}

void CommentMode::replaceRelease() {
    DocumentSession* s = session();
    const TextPage* text = textPage(page_);
    if (s == nullptr || text == nullptr || preview_.empty()) {
        return;  // заменить можно только текст
    }
    annot::AnnotProps strike = newProps(CommentAction::ReplaceText);
    strike.type = annot::AnnotType::StrikeOut;
    strike.quads = preview_.quads;
    strike.intent = "StrikeOutTextEdit";
    strike.subject = "Cross-Out";
    annot::AnnotProps caret =
        caretProps(newProps(CommentAction::InsertText), caretRectAt(*text, preview_.chars.back() + 1));
    caret.subject = "Replace Text";
    std::vector<annot::AnnotProps> items{std::move(caret), std::move(strike)};
    QFuture<std::vector<annot::AnnotRef>> future =
        annot::createGroup(*s, page_, std::move(items), QString::fromLatin1(annot::kReplaceTextStep));
    done(future.then([](const std::vector<annot::AnnotRef>& refs) {
        return refs.empty() ? annot::AnnotRef{} : refs.front();
    }),
         true);
}

}  // namespace ot::ui
