#include "services/props_ops.h"

#include <stdexcept>
#include <string>

#include "services/task_context.h"

namespace ot::props {

namespace {

std::string* field(DocProperties& p, const QString& key) {
    if (key == QLatin1String("title")) {
        return &p.title;
    }
    if (key == QLatin1String("author")) {
        return &p.author;
    }
    if (key == QLatin1String("subject")) {
        return &p.subject;
    }
    if (key == QLatin1String("keywords")) {
        return &p.keywords;
    }
    if (key == QLatin1String("creator")) {
        return &p.creator;
    }
    if (key == QLatin1String("producer")) {
        return &p.producer;
    }
    return nullptr;
}

}  // namespace

QStringList editableKeys() {
    return {QStringLiteral("title"),    QStringLiteral("author"),  QStringLiteral("subject"),
            QStringLiteral("keywords"), QStringLiteral("creator"), QStringLiteral("producer")};
}

QFuture<DocProperties> read(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return tc.document().properties(); }, TaskPriority::High,
                        QStringLiteral("read properties"));
}

QFuture<void> setProperty(DocumentSession& session, const QString& key, const QString& value) {
    return session.edit(QStringLiteral("Set document property"), [key, value](TaskContext& tc) {
        Document& doc = tc.document();
        DocProperties p = doc.properties();
        std::string* target = field(p, key);
        if (target == nullptr) {
            throw std::invalid_argument("unknown document property: " + key.toStdString());
        }
        *target = value.toStdString();
        doc.setProperties(p);
    });
}

}  // namespace ot::props
