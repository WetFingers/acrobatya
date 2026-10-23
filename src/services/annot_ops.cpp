#include "services/annot_ops.h"

#include <algorithm>

#include "services/task_context.h"

namespace ot::annot {

QFuture<std::vector<AnnotInfo>> list(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return listAnnots(tc.document(), page); }, priority,
                        QStringLiteral("List annotations"));
}

QFuture<std::optional<AnnotInfo>> at(DocumentSession& session, int page, PointF point, float tolerance,
                                     TaskPriority priority) {
    return session.post(
        [page, point, tolerance](TaskContext& tc) { return annotAt(tc.document(), page, point, tolerance); },
        priority, QStringLiteral("Annotation at point"));
}

QFuture<void> setRect(DocumentSession& session, AnnotRef ref, RectF rect) {
    return session.edit(QString::fromLatin1(kSetRectStep), [ref, rect](TaskContext& tc) {
        ot::annot::setRect(tc.document(), ref, rect);
        tc.touchPage(ref.page);
    });
}

QFuture<void> remove(DocumentSession& session, std::vector<AnnotRef> refs) {
    return session.edit(QString::fromLatin1(kDeleteStep), [refs = std::move(refs)](TaskContext& tc) {
        ot::annot::remove(tc.document(), refs);
        std::vector<int> pages;
        for (const AnnotRef& ref : refs) {
            if (std::find(pages.begin(), pages.end(), ref.page) == pages.end()) {
                pages.push_back(ref.page);
                tc.touchPage(ref.page);
            }
        }
    });
}

}  // namespace ot::annot
