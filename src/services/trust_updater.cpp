#include "services/trust_updater.h"

#include <spdlog/spdlog.h>

#include <filesystem>

#include "core/sign/trust_store.h"
#include "services/settings.h"
#include "services/settings_keys.h"

namespace ot {

TrustUpdater::TrustUpdater(Settings& settings, net::HttpFetcher& fetcher, QString trustDir, QObject* parent)
    : QObject(parent), settings_(settings), fetcher_(fetcher), dir_(std::move(trustDir)) {}

QDateTime TrustUpdater::now() const { return clock_ ? clock_().toUTC() : QDateTime::currentDateTimeUtc(); }

QDateTime TrustUpdater::lastCheck() const {
    return QDateTime::fromString(settings_.get(settings::kTrustListsCheckedAt), Qt::ISODate);
}

bool TrustUpdater::due() const {
    const QDateTime last = lastCheck();
    return !last.isValid() || last.addDays(kIntervalDays) <= now();
}

TrustUpdater::Outcome TrustUpdater::check(bool force) {
    const bool eutl = settings_.get(settings::kTrustEutl);
    const bool aatl = settings_.get(settings::kTrustAatl);
    if (!eutl && !aatl) {
        return Outcome::Disabled;
    }
    if (!force && !due()) {
        return Outcome::NotDue;
    }
    const QDateTime at = now();
    settings_.set(settings::kTrustListsCheckedAt, at.toString(Qt::ISODate));
    settings_.sync();
    TrustListUpdate update = downloadTrustLists(fetcher_, eutl, aatl, at.toSecsSinceEpoch());
    if (update.empty()) {
        spdlog::info("trust updater: nothing downloaded ({}{}{})", update.eutlError,
                     update.eutlError.empty() || update.aatlError.empty() ? "" : "; ", update.aatlError);
        return Outcome::Failed;
    }
    const bool ask = settings_.get(settings::kTrustListsAsk) || update.needsConsent();
    if (ask) {
        const int e = update.eutlCount();
        const int a = update.aatlCount();
        const bool consent = update.needsConsent();
        pending_ = std::move(update);
        emit updateAvailable(e, a, consent);
        return Outcome::Pending;
    }
    installUpdate(update);
    return Outcome::Installed;
}

int TrustUpdater::install() {
    if (!pending_) {
        return 0;
    }
    const int n = installUpdate(*pending_);
    pending_.reset();
    return n;
}

int TrustUpdater::installUpdate(const TrustListUpdate& update) {
    const std::filesystem::path dir(dir_.toStdWString());
    sign::TrustStore store = sign::TrustStore::load(dir);
    const int n = installTrustLists(update, store, dir_);
    emit installed(n);
    return n;
}

const char* trustUpdaterOutcomeName(TrustUpdater::Outcome outcome) {
    switch (outcome) {
        case TrustUpdater::Outcome::NotDue:
            return "NotDue";
        case TrustUpdater::Outcome::Disabled:
            return "Disabled";
        case TrustUpdater::Outcome::Failed:
            return "Failed";
        case TrustUpdater::Outcome::Pending:
            return "Pending";
        case TrustUpdater::Outcome::Installed:
            return "Installed";
    }
    return "?";
}

}  // namespace ot
