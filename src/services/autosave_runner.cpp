#include "services/autosave_runner.h"

#include <QDir>

#include <algorithm>
#include <filesystem>
#include <memory>

#include "services/app_info.h"
#include "services/task_context.h"

namespace ot {

namespace {

std::filesystem::path toPath(const QString& p) { return std::filesystem::path(p.toStdWString()); }

}  // namespace

AutosaveRunner::AutosaveRunner(const QString& dir, QObject* parent)
    : QObject(parent), store_(std::make_shared<Autosave>(toPath(dir))) {
    timer_.setTimerType(Qt::VeryCoarseTimer);
    timer_.setInterval(minutes_ * 60 * 1000);
    connect(&timer_, &QTimer::timeout, this, &AutosaveRunner::tick);
}

QString AutosaveRunner::defaultDir() { return QDir(app::dataDir()).filePath(QStringLiteral("autosave")); }

void AutosaveRunner::setIntervalMinutes(int minutes) {
    minutes_ = std::clamp(minutes, 1, 30);
    timer_.setInterval(minutes_ * 60 * 1000);
}

void AutosaveRunner::start() { timer_.start(); }

void AutosaveRunner::stop() { timer_.stop(); }

void AutosaveRunner::watch(DocumentSession* session) {
    if (session == nullptr || watched_.contains(session->id())) {
        return;
    }
    const quint64 id = session->id();
    watched_.insert(id, Watched{session, session->revision(), session->path()});
    // Сигналы сессии приходят из её потока — сюда очередью (AutoConnection: получатель в GUI-потоке).
    connect(session, &DocumentSession::saved, this,
            [this, session](const QString& path) { onSaved(session, path); });
    connect(session, &DocumentSession::opened, this, [this, id](const QString& path) {
        if (watched_.contains(id)) {
            watched_[id].path = path;
        }
    });
    connect(session, &QObject::destroyed, this, [this, id] { watched_.remove(id); });
}

void AutosaveRunner::unwatch(DocumentSession* session) {
    if (session == nullptr) {
        return;
    }
    disconnect(session, nullptr, this, nullptr);
    watched_.remove(session->id());
}

void AutosaveRunner::tick() {
    for (auto it = watched_.begin(); it != watched_.end(); ++it) {
        DocumentSession* session = it->session.data();
        if (session != nullptr && session->isOpen() && session->revision() != it->revision) {
            (void)saveNow(session);
        }
    }
}

QFuture<AutosaveStatus> AutosaveRunner::saveNow(DocumentSession* session) {
    const quint64 id = session->id();
    const quint64 revision = session->revision();
    if (watched_.contains(id)) {
        watched_[id].revision = revision;  // повтор только после новой правки (или неудачи — ниже)
    }
    std::shared_ptr<Autosave> store = store_;
    auto error = std::make_shared<QString>();
    auto path = std::make_shared<QString>();
    QFuture<AutosaveStatus> future = session->post(
        [store, error, path](TaskContext& tc) {
            if (!tc.hasDocument()) {
                return AutosaveStatus::Skipped;
            }
            std::string message;
            const AutosaveStatus status = store->save(tc.document(), &message);
            *error = QString::fromStdString(message);
            *path = QString::fromStdWString(tc.document().path().wstring());
            return status;
        },
        TaskPriority::Background, QStringLiteral("autosave"));
    // Сигналы — в потоке runner (продолжение отменяется, если runner разрушен).
    future.then(this, [this, id, error, path](AutosaveStatus status) {
        if (status == AutosaveStatus::Saved) {
            emit autosaved(*path);
        } else if (status == AutosaveStatus::Failed) {
            if (watched_.contains(id)) {
                watched_[id].revision = 0;  // попробовать снова в следующий раз
            }
            emit autosaveFailed(*path, *error);
        }
    });
    return future;
}

void AutosaveRunner::discard(DocumentSession* session) {
    if (session == nullptr) {
        return;
    }
    discard(session->path());
    if (watched_.contains(session->id())) {
        watched_[session->id()].revision = session->revision();
    }
}

void AutosaveRunner::discard(const QString& documentPath) {
    if (!documentPath.isEmpty()) {
        store_->remove(toPath(documentPath));
    }
}

void AutosaveRunner::onSaved(DocumentSession* session, const QString& path) {
    const quint64 id = session->id();
    if (watched_.contains(id)) {
        Watched& w = watched_[id];
        if (!w.path.isEmpty() && w.path != path) {
            store_->remove(toPath(w.path));  // «Сохранить как»: запись прежнего пути тоже не нужна
        }
        w.path = path;
        w.revision = session->revision();
    }
    store_->remove(toPath(path));
}

std::vector<AutosaveEntry> AutosaveRunner::recoverable() const { return store_->entries(); }

QFuture<OpenSummary> AutosaveRunner::recover(const AutosaveEntry& entry, DocumentSession* session,
                                             const QString& password) {
    watch(session);
    if (watched_.contains(session->id())) {
        watched_[session->id()].path = QString::fromStdWString(entry.path.wstring());
    }
    return session->recoverAsync(QString::fromStdWString(store_->dir().wstring()), entry, password);
}

}  // namespace ot
