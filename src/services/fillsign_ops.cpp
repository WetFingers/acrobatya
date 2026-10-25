#include "services/fillsign_ops.h"

#include "core/annot/stamps.h"
#include "core/text/stext.h"
#include "services/task_context.h"

namespace ot::fillsign {

QFuture<PageGeometry> geometry(DocumentSession& session, int page, TaskPriority priority) {
    return session.post(
        [page](TaskContext& tc) {
            TextOptions options;
            options.collectVectors = true;
            const std::shared_ptr<const TextPage> text = TextPage::build(tc.document(), page, options);
            return geometryOf(text->vectors(), text->lines());
        },
        priority, QStringLiteral("Fill & Sign geometry"));
}

QFuture<std::vector<annot::AnnotRef>> createAll(DocumentSession& session, int page,
                                                std::vector<annot::AnnotProps> items, QString step) {
    const std::string operation = step.toStdString();
    return session.edit(std::move(step), [page, items = std::move(items), operation](TaskContext& tc) {
        std::vector<annot::AnnotRef> refs = ot::fillsign::createAll(tc.document(), page, items, operation);
        tc.touchPage(page);
        return refs;
    });
}

QFuture<annot::AnnotRef> createSignature(DocumentSession& session, int page, std::string png,
                                         annot::AnnotProps props, bool initials) {
    const QString step = QString::fromLatin1(initials ? kAddInitialsStep : kAddSignatureStep);
    const std::string name = initials ? kInitialsStampName : kSignatureStampName;
    return session.edit(step, [page, png = std::move(png), props = std::move(props), name](TaskContext& tc) {
        annot::AnnotRef ref = annot::createImageStamp(tc.document(), page, png, props, name);
        tc.touchPage(page);
        return ref;
    });
}

}  // namespace ot::fillsign
