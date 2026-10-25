#include "services/fillsign_ops.h"

#include <QImage>
#include "core/annot/stamps.h"
#include "core/text/stext.h"

#include "services/signature_image.h"
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

QFuture<void> setSignatureColor(DocumentSession& session, annot::AnnotRef ref, QColor color) {
    return session.edit(QString::fromLatin1(kSignatureColorStep), [ref, color](TaskContext& tc) {
        const std::optional<annot::AnnotProps> props = annot::readProps(tc.document(), ref);
        if (!props || props->type != annot::AnnotType::Stamp ||
            (props->icon != kSignatureStampName && props->icon != kInitialsStampName)) {
            throw std::invalid_argument("not a Fill & Sign signature");
        }
        const std::string png = annot::stampImage(tc.document(), ref);
        QImage image;
        if (!image.loadFromData(reinterpret_cast<const uchar*>(png.data()), static_cast<int>(png.size()),
                                "PNG")) {
            throw std::runtime_error("signature image is not readable");
        }
        const QByteArray recolored = toPng(recolor(image, color));
        annot::setStampImage(tc.document(), ref,
                             std::string(recolored.constData(), static_cast<std::size_t>(recolored.size())),
                             kSignatureColorStep);
        tc.touchPage(ref.page);
    });
}

QFuture<QImage> stampImage(DocumentSession& session, annot::AnnotRef ref) {
    return session.post(
        [ref](TaskContext& tc) {
            const std::string png = annot::stampImage(tc.document(), ref);
            QImage image;
            image.loadFromData(reinterpret_cast<const uchar*>(png.data()), static_cast<int>(png.size()),
                               "PNG");
            return image.convertToFormat(QImage::Format_ARGB32);
        },
        TaskPriority::High, QStringLiteral("Stamp image"));
}

}  // namespace ot::fillsign
