#include "services/nav_ops.h"

#include <QFile>

#include <stdexcept>

#include "services/task_context.h"

namespace ot::nav {

QFuture<std::vector<outline::Item>> outline(DocumentSession& session, std::vector<int> path,
                                            TaskPriority priority) {
    return session.post(
        [path = std::move(path)](TaskContext& tc) { return outline::children(tc.document(), path); },
        priority, QStringLiteral("List bookmarks"));
}

QFuture<std::vector<dests::Destination>> destinations(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return dests::list(tc.document()); }, TaskPriority::Normal,
                        QStringLiteral("List destinations"));
}

QFuture<std::vector<attachments::Attachment>> attachments(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return attachments::list(tc.document()); },
                        TaskPriority::Normal, QStringLiteral("List attachments"));
}

QFuture<void> saveAttachment(DocumentSession& session, attachments::Attachment attachment, QString path) {
    return session.post(
        [attachment = std::move(attachment), path = std::move(path)](TaskContext& tc) {
            const std::string data = attachments::contents(tc.document(), attachment);
            QFile file(path);
            if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
                file.write(data.data(), static_cast<qint64>(data.size())) !=
                    static_cast<qint64>(data.size())) {
                throw std::runtime_error("cannot write attachment: " + file.errorString().toStdString());
            }
        },
        TaskPriority::Normal, QStringLiteral("Save attachment"));
}

QFuture<std::vector<layers::LayerUi>> layers(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return layers::list(tc.document()); }, TaskPriority::Normal,
                        QStringLiteral("List layers"));
}

QFuture<std::vector<layers::LayerUi>> toggleLayer(DocumentSession& session, int index) {
    return session.post(
        [index](TaskContext& tc) {
            Document& doc = tc.document();
            if (layers::toggle(doc, index)) {
                tc.setLayersState(layers::stateHash(doc));
            }
            return layers::list(doc);
        },
        TaskPriority::High, QStringLiteral("Toggle layer"));
}

QFuture<std::vector<layers::LayerUi>> resetLayers(DocumentSession& session) {
    return session.post(
        [](TaskContext& tc) {
            Document& doc = tc.document();
            const std::uint64_t before = layers::stateHash(doc);
            layers::reset(doc);
            const std::uint64_t after = layers::stateHash(doc);
            if (after != before) {
                tc.setLayersState(after);
            }
            return layers::list(doc);
        },
        TaskPriority::High, QStringLiteral("Reset layers"));
}

}  // namespace ot::nav
