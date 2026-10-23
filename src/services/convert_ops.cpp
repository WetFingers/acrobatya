#include "services/convert_ops.h"

#include "core/create/to_pdf.h"
#include "services/task_context.h"

namespace ot::convert {

QFuture<void> saveAsPdf(DocumentSession& session, const QString& path) {
    return session.post(
        [target = path.toStdWString()](TaskContext& tc) {
            writeAsPdf(tc.document(), std::filesystem::path(target), [&tc] { return tc.aborted(); });
        },
        TaskPriority::Normal, QStringLiteral("Save as PDF"));
}

}  // namespace ot::convert
