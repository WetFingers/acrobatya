#include "services/trust_service.h"

#include <QDir>

#include "services/app_info.h"

namespace ot {

TrustService& TrustService::shared() {
    static TrustService service;
    return service;
}

void TrustService::ensureLoaded() {
    const QString dir = QDir(app::dataDir()).filePath(QStringLiteral("trust"));
    if (store_ && dir == dir_) {
        return;
    }
    dir_ = dir;
    store_ = std::make_shared<const sign::TrustStore>(sign::TrustStore::load(dir_.toStdWString()));
}

QString TrustService::dir() {
    const std::lock_guard lock(mutex_);
    ensureLoaded();
    return dir_;
}

std::shared_ptr<const sign::TrustStore> TrustService::snapshot() {
    const std::lock_guard lock(mutex_);
    ensureLoaded();
    return store_;
}

void TrustService::commit(std::shared_ptr<sign::TrustStore> next) {
    {
        const std::lock_guard lock(mutex_);
        ensureLoaded();
        next->save(dir_.toStdWString());
        store_ = std::move(next);
    }
    emit changed();
}

void TrustService::reload() {
    {
        const std::lock_guard lock(mutex_);
        store_.reset();
        ensureLoaded();
    }
    emit changed();
}

}  // namespace ot
