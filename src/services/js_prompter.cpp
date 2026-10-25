#include "services/js_prompter.h"

#include <QMetaObject>
#include <QSemaphore>
#include <QThread>

#include <atomic>
#include <memory>

#include <spdlog/spdlog.h>

#include "services/protected_view.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/state_db.h"

namespace ot {

JsPrompter::JsPrompter(QObject* parent) : QObject(parent) {}

void JsPrompter::setPrompt(Prompt prompt) { prompt_ = std::move(prompt); }

forms::AlertReply JsPrompter::alert(const forms::AlertRequest& request) {
    spdlog::info("js: app.alert: {}", request.message);
    if (!prompt_) {
        return forms::defaultReply(request);
    }
    if (QThread::currentThread() == thread()) {
        return prompt_(request);
    }
    // Поток документа: окно — в потоке объекта, ответ — через семафор с пределом ожидания.
    struct Pending {
        QSemaphore done;
        forms::AlertReply reply;
        std::atomic<bool> abandoned{false};
    };
    auto pending = std::make_shared<Pending>();
    pending->reply = forms::defaultReply(request);
    const Prompt prompt = prompt_;
    QMetaObject::invokeMethod(
        this,
        [pending, prompt, request] {
            if (pending->abandoned.load()) {
                return;  // поток документа уже не ждёт — окно не показывать
            }
            pending->reply = prompt(request);
            pending->done.release();
        },
        Qt::QueuedConnection);
    if (!pending->done.tryAcquire(1, timeoutMs_)) {
        pending->abandoned.store(true);
        spdlog::warn("js: app.alert not answered in {} ms — default answer", timeoutMs_);
        return forms::defaultReply(request);
    }
    return pending->reply;
}

void JsPrompter::console(const std::string& line) { emit consoleLine(QString::fromStdString(line)); }

void JsPrompter::request(const forms::JsRequest& request) {
    const QString detail = QString::fromStdString(request.detail);
    if (request.kind == forms::JsRequestKind::LaunchUrl) {
        emit launchRequested(detail);
    }
    emit requested(static_cast<int>(request.kind), detail);
}

namespace js_policy {

forms::JsState decide(const QString& path, bool protectedView, bool hasScripts) {
    const Settings settings;
    return decide(path, protectedView, hasScripts, settings, StateDb::shared());
}

forms::JsState decide(const QString& path, bool protectedView, bool hasScripts, const Settings& settings,
                      StateDb& db) {
    forms::JsPolicyInput in;
    in.mode = forms::jsModeFromName(settings.get(settings::kJavaScript).toStdString());
    in.hasScripts = hasScripts;
    in.protectedView = protectedView;
    if (!path.isEmpty()) {
        for (const QString& folder : settings.get(settings::kTrustedFolders)) {
            if (protected_view::isUnder(path, folder)) {
                in.trustedFolder = true;
                break;
            }
        }
        in.trustedFile = enabledForFile(path, db);
    }
    return forms::decide(in);
}

bool enableForFile(const QString& path) { return enableForFile(path, StateDb::shared()); }

bool enableForFile(const QString& path, StateDb& db) {
    return protected_view::trustFor(path, QLatin1String(kScope), db);
}

bool enabledForFile(const QString& path, StateDb& db) {
    return protected_view::isTrustedFor(path, QLatin1String(kScope), db);
}

}  // namespace js_policy

}  // namespace ot
