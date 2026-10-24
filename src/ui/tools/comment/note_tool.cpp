// Записка (план 5.1; эталон 3.4 «Инструмент»; comment-01): щелчок — хит-тест задачей документа; по своей
// записке (/Text) — открыть её окно, иначе — новая записка значком 20 × 20 пт с центром в точке щелчка (в
// пределах страницы), стиль инструмента (иконка Comment… и цвет), окно записки справа от значка; окно
// открывается для набора (Установки → Комментирование: «Открывать окно при создании»).
#include <QFuture>

#include <algorithm>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

void CommentMode::notePress(const Locator& at) {
    DocumentSession* s = session();
    const quint64 request = ++request_;
    const auto tolerance = static_cast<float>(2.0 / std::max(0.01f, view_.zoom()));
    annot::at(*s, at.page, PointF{static_cast<float>(at.point.x()), static_cast<float>(at.point.y())},
              tolerance)
        .then(this, [this, at, request](QFuture<std::optional<annot::AnnotInfo>> f) {
            std::optional<annot::AnnotInfo> hit;
            try {
                hit = f.result();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет ответа — как мимо
            }
            noteHit(hit, at, request);
        });
}

void CommentMode::noteHit(const std::optional<annot::AnnotInfo>& hit, Locator at, quint64 request) {
    DocumentSession* s = session();
    if (request != request_ || s == nullptr) {
        return;
    }
    if (hit && hit->type == annot::AnnotType::Text) {
        PopupLayer::of(view_).open(hit->ref, true);
        controller_.finished();
        return;
    }
    const QSizeF size = view_.pageSizePoints(at.page);
    const float half = kNoteIconSize / 2;
    const float x =
        std::clamp(static_cast<float>(at.point.x()), half, static_cast<float>(size.width()) - half);
    const float y =
        std::clamp(static_cast<float>(at.point.y()), half, static_cast<float>(size.height()) - half);
    annot::AnnotProps props = newProps(CommentAction::Note);
    props.rect = {x - half, y - half, x + half, y + half};
    const bool open = Settings().get(settings::kCommentOpenPopupOnCreate);
    done(annot::create(*s, at.page, std::move(props)), open);
}

}  // namespace ot::ui
