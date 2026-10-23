#include "services/link_ops.h"

#include "services/task_context.h"

namespace ot::links {

QFuture<std::vector<LinkInfo>> list(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return pageLinks(tc.document(), page); }, priority,
                        QStringLiteral("List links"));
}

}  // namespace ot::links
