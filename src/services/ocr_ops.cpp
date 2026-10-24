#include "services/ocr_ops.h"

#include "services/task_context.h"

namespace ot::ocr {

QFuture<ScanCheck> scanCheck(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return checkScan(tc.document(), page); }, priority,
                        QStringLiteral("Scan check"));
}

}  // namespace ot::ocr
