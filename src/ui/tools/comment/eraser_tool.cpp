// Ластик (план 5.1; эталон 3.4 «Рисовать и Ластик (по штрихам)»; comment-06): круг r = 6 px экрана за
// курсором; протянутый путь стирает у аннотаций Ink страницы части штрихов в этих кругах — ядро режет штрих и
// пишет /InkList заново (annot::eraseInk; штрих, от которого ничего не осталось, удаляется вместе с
// аннотацией). Каждая задетая аннотация — шаг «Erase ink» (подряд за 1 с сливаются в один).
#include <QFuture>
#include <QPainter>

#include <cmath>
#include <limits>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

void CommentMode::paintEraser(QPainter& painter) {
    if (view_.pageAtViewport(hover_) < 0 && !pressing_) {
        return;
    }
    const auto& colors = Theme::colors();
    QPen pen(colors.text.color(), Theme::sizes().pageBorder);
    pen.setCosmetic(true);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(pen);
    painter.setBrush(colors.accentSoft.color());
    painter.drawEllipse(hover_, kEraserRadiusPx, kEraserRadiusPx);
    painter.restore();
}

void CommentMode::eraserRelease() {
    DocumentSession* s = session();
    if (s == nullptr || stroke_.empty()) {
        return;
    }
    const QTransform toView = view_.pageTransform(page_);
    const double scale = std::hypot(toView.m11(), toView.m12());
    const auto radius = static_cast<float>(kEraserRadiusPx / std::max(0.01, scale));
    std::vector<PointF> path;
    RectF bounds{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
                 std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
    for (const QPointF& p : stroke_) {
        const PointF q{static_cast<float>(p.x()), static_cast<float>(p.y())};
        path.push_back(q);
        bounds = {std::min(bounds.x0, q.x - radius), std::min(bounds.y0, q.y - radius),
                  std::max(bounds.x1, q.x + radius), std::max(bounds.y1, q.y + radius)};
    }
    annot::list(*s, page_, TaskPriority::High)
        .then(this, [this, path, bounds, radius](QFuture<std::vector<annot::AnnotInfo>> f) {
            DocumentSession* session = this->session();
            std::vector<annot::AnnotInfo> list;
            try {
                list = f.result();
            } catch (const std::exception& e) {
                reportError(QString::fromUtf8(e.what()));
                return;
            }
            if (session == nullptr) {
                return;
            }
            for (const annot::AnnotInfo& a : list) {
                const bool overlaps = a.rect.x0 <= bounds.x1 && a.rect.x1 >= bounds.x0 &&
                                      a.rect.y0 <= bounds.y1 && a.rect.y1 >= bounds.y0;
                if (a.type == annot::AnnotType::Ink && overlaps && !a.locked()) {
                    annot::eraseInk(*session, a.ref, path, radius);
                }
            }
        });
}

}  // namespace ot::ui
