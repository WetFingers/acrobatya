#include "services/link_policy.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>

#include <mutex>

#include "services/settings.h"
#include "services/sql.h"
#include "services/state_db.h"

namespace ot {

LinkTrust::LinkTrust() : db_(StateDb::shared()), own_(std::make_unique<Settings>()), settings_(own_.get()) {}

LinkTrust::LinkTrust(StateDb& db, const Settings& settings) : db_(db), settings_(&settings) {}

LinkTrust::~LinkTrust() = default;

QString LinkTrust::hostKey(const QUrl& url) { return url.host(QUrl::FullyEncoded).toLower(); }

HostRule LinkTrust::listedRule(const QString& host) const {
    if (host.isEmpty() || !db_.isOpen()) {
        return HostRule::Ask;
    }
    const std::lock_guard lock(db_.mutex());
    SqlStatement st(db_.handle(), "SELECT rule FROM link_hosts WHERE host = ?1");
    st.text(1, host.toLower());
    if (st.step() != SQLITE_ROW) {
        return HostRule::Ask;
    }
    const qint64 rule = st.columnInt(0);
    return rule == 1 ? HostRule::Allow : rule == 2 ? HostRule::Block : HostRule::Ask;
}

HostRule LinkTrust::rule(const QString& host) const {
    const HostRule listed = listedRule(host);
    if (listed != HostRule::Ask) {
        return listed;
    }
    const QString access = settings_->get(settings::kInternetAccess);
    if (access == QLatin1String("allowAll")) {
        return HostRule::Allow;
    }
    if (access == QLatin1String("blockAll")) {
        return HostRule::Block;
    }
    return HostRule::Ask;
}

void LinkTrust::setRule(const QString& host, HostRule rule) {
    if (host.isEmpty() || !db_.isOpen()) {
        return;
    }
    const std::lock_guard lock(db_.mutex());
    if (rule == HostRule::Ask) {
        SqlStatement st(db_.handle(), "DELETE FROM link_hosts WHERE host = ?1");
        st.text(1, host.toLower()).run();
        return;
    }
    SqlStatement st(db_.handle(),
                    "INSERT INTO link_hosts(host, rule, decided_at) VALUES(?1, ?2, ?3) "
                    "ON CONFLICT(host) DO UPDATE SET rule = excluded.rule, decided_at = excluded.decided_at");
    st.text(1, host.toLower())
        .integer(2, static_cast<int>(rule))
        .integer(3, QDateTime::currentSecsSinceEpoch())
        .run();
}

QList<QPair<QString, HostRule>> LinkTrust::hosts() const {
    QList<QPair<QString, HostRule>> out;
    if (!db_.isOpen()) {
        return out;
    }
    const std::lock_guard lock(db_.mutex());
    SqlStatement st(db_.handle(), "SELECT host, rule FROM link_hosts ORDER BY host");
    while (st.step() == SQLITE_ROW) {
        out.append({st.columnText(0), st.columnInt(1) == 1 ? HostRule::Allow : HostRule::Block});
    }
    return out;
}

QUrl linkUrl(const links::LinkAction& action) {
    return QUrl::fromEncoded(QByteArray::fromStdString(action.uri), QUrl::TolerantMode);
}

bool isWebUrl(const QUrl& url) {
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == QLatin1String("http") || scheme == QLatin1String("https") ||
                             scheme == QLatin1String("mailto"));
}

LinkDecision decideLink(const links::LinkAction& action, bool protectedView, const LinkTrust& trust) {
    using links::LinkKind;
    switch (action.kind) {
        case LinkKind::GoTo:
        case LinkKind::Named:
            return LinkDecision::Navigate;
        case LinkKind::Launch:
            return LinkDecision::Forbidden;
        case LinkKind::Unsupported:
            return LinkDecision::Unsupported;
        case LinkKind::GoToR:
            return protectedView ? LinkDecision::ProtectedView : LinkDecision::AskFile;
        case LinkKind::Uri:
            break;
    }
    const QUrl url = linkUrl(action);
    if (!isWebUrl(url)) {
        return LinkDecision::Unsupported;
    }
    if (protectedView) {
        return LinkDecision::ProtectedView;
    }
    switch (trust.rule(LinkTrust::hostKey(url))) {
        case HostRule::Allow:
            return LinkDecision::OpenUrl;
        case HostRule::Block:
            return LinkDecision::BlockedUrl;
        case HostRule::Ask:
            break;
    }
    return LinkDecision::AskUrl;
}

QString remoteFilePath(const links::LinkAction& action, const QString& documentPath) {
    QString file = QString::fromStdString(action.file);
    if (file.isEmpty()) {
        QString uri = QString::fromStdString(action.uri);
        if (uri.startsWith(QLatin1String("file:"))) {
            uri = uri.mid(5);
            const qsizetype hash = uri.indexOf(QLatin1Char('#'));
            file = QUrl::fromPercentEncoding((hash >= 0 ? uri.left(hash) : uri).toUtf8());
        }
    }
    if (file.isEmpty()) {
        return {};
    }
    file.replace(QLatin1Char('\\'), QLatin1Char('/'));
    // Путь PDF «/C/Docs/a.pdf» — диск C.
    if (file.size() > 2 && file[0] == QLatin1Char('/') && file[1].isLetter() && file[2] == QLatin1Char('/')) {
        file = file.mid(1, 1) + QLatin1Char(':') + file.mid(2);
    }
    if (QDir::isAbsolutePath(file)) {
        return QDir::cleanPath(file);
    }
    return QDir::cleanPath(QFileInfo(documentPath).absoluteDir().filePath(file));
}

}  // namespace ot
