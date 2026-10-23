#include "services/document_registry.h"

#include <QDir>
#include <QFileInfo>

namespace ot {

DocumentRegistry::DocumentRegistry(QObject* parent) : QObject(parent) {}

DocumentRegistry::~DocumentRegistry() { closeAll(); }

QString DocumentRegistry::pathKey(const QString& path) {
    const QFileInfo info(path);
    QString key = info.exists() ? info.canonicalFilePath() : QDir::cleanPath(info.absoluteFilePath());
    return key.toLower();
}

DocumentRegistry::Opened DocumentRegistry::open(const QString& path, const QString& password) {
    Opened out;
    if (const quint64 id = findByPath(path); id != 0) {
        out.id = id;
        out.existing = true;
        out.summary = sessions_.at(id)->summary();
        return out;
    }
    auto session = std::make_shared<DocumentSession>();
    out.summary = session->openAsync(path, password).result();
    if (!out.summary.ok) {
        return out;  // сессия разрушается здесь вместе со своим потоком
    }
    out.id = session->id();
    sessions_.emplace(out.id, std::move(session));
    emit documentOpened(out.id);
    return out;
}

std::shared_ptr<DocumentSession> DocumentRegistry::session(quint64 id) const {
    const auto it = sessions_.find(id);
    return it == sessions_.end() ? nullptr : it->second;
}

quint64 DocumentRegistry::findByPath(const QString& path) const {
    const QString key = pathKey(path);
    for (const auto& [id, session] : sessions_) {
        if (pathKey(session->path()) == key) {
            return id;
        }
    }
    return 0;
}

QList<quint64> DocumentRegistry::ids() const {
    QList<quint64> out;
    out.reserve(static_cast<qsizetype>(sessions_.size()));
    for (const auto& entry : sessions_) {
        out.push_back(entry.first);
    }
    return out;
}

bool DocumentRegistry::close(quint64 id) {
    const auto it = sessions_.find(id);
    if (it == sessions_.end()) {
        return false;
    }
    std::shared_ptr<DocumentSession> session = std::move(it->second);
    sessions_.erase(it);
    session->close();  // ждёт задачи, поставленные раньше, и закрывает документ в его потоке
    emit documentClosed(id);
    return true;
}

void DocumentRegistry::closeAll() {
    while (!sessions_.empty()) {
        close(sessions_.begin()->first);
    }
}

}  // namespace ot
