#include "services/save_ops.h"

#include "services/document_session.h"
#include "services/task_context.h"

namespace ot::saving {

QFuture<SaveReport> decide(DocumentSession& session, const SaveOptions& options) {
    return session.post([options](TaskContext& tc) { return Saver::decide(tc.document(), options); },
                        TaskPriority::High, QStringLiteral("decide save"));
}

}  // namespace ot::saving
