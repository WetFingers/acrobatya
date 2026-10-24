#include "services/box_ops.h"

#include "services/task_context.h"

namespace ot::boxes {

QFuture<pages::PageBoxes> read(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return pages::pageBoxes(tc.document(), page); }, priority,
                        QStringLiteral("Read page boxes"));
}

}  // namespace ot::boxes
