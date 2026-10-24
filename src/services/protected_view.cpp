#include "services/protected_view.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include <spdlog/spdlog.h>

#include "services/settings.h"
#include "services/sql.h"
#include "services/state_db.h"

namespace ot::protected_view {

namespace {

constexpr qint64 kKeyBytes = 64 * 1024;
constexpr qint64 kZoneStreamMax = 64 * 1024;
constexpr char kScope[] = "protectedView";

std::filesystem::path zoneStream(const QString& path) {
    return std::filesystem::path(QDir::toNativeSeparators(path).toStdWString() + L":Zone.Identifier");
}

QString decodeZoneText(const std::string& bytes) {
    if (bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
        static_cast<unsigned char>(bytes[1]) == 0xFE) {
        return QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.data() + 2),
                                  static_cast<qsizetype>((bytes.size() - 2) / 2));
    }
    if (bytes.size() >= 3 && bytes.compare(0, 3, "\xEF\xBB\xBF") == 0) {
        return QString::fromUtf8(bytes.data() + 3, static_cast<qsizetype>(bytes.size() - 3));
    }
    return QString::fromUtf8(bytes.data(), static_cast<qsizetype>(bytes.size()));
}

std::optional<QStringList>& tempOverride() {
    static std::optional<QStringList> folders;
    return folders;
}

QString normalized(const QString& path) {
    const QFileInfo info(path);
    QString p = info.exists() ? info.canonicalFilePath() : QString();
    if (p.isEmpty()) {
        p = QDir::cleanPath(info.absoluteFilePath());
    }
    return p.toCaseFolded();
}

}  // namespace

int zoneId(const QString& path) {
    std::ifstream in(zoneStream(path), std::ios::binary);
    if (!in) {
        return -1;
    }
    std::string bytes(static_cast<std::size_t>(kZoneStreamMax), '\0');
    in.read(bytes.data(), kZoneStreamMax);
    bytes.resize(static_cast<std::size_t>(in.gcount()));
    static const QRegularExpression re(
        QStringLiteral("^\\s*ZoneId\\s*=\\s*(\\d+)\\s*$"),
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);
    const QRegularExpressionMatch m = re.match(decodeZoneText(bytes));
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

bool writeZoneIdentifier(const QString& path, int zone, const QString& hostUrl) {
    std::ofstream out(zoneStream(path), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    QByteArray text = QByteArrayLiteral("[ZoneTransfer]\r\nZoneId=") + QByteArray::number(zone) + "\r\n";
    if (!hostUrl.isEmpty()) {
        text += "HostUrl=" + hostUrl.toUtf8() + "\r\n";
    }
    out.write(text.constData(), text.size());
    return static_cast<bool>(out);
}

QStringList tempFolders() {
    if (tempOverride()) {
        return *tempOverride();
    }
    QStringList folders{QDir::tempPath()};
    const QString local = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (!local.isEmpty()) {
        folders << QDir(local).filePath(QStringLiteral("Microsoft/Windows/INetCache"));  // Content.Outlook
    }
    return folders;
}

void setTempFoldersForTesting(std::optional<QStringList> folders) { tempOverride() = std::move(folders); }

bool isUnder(const QString& path, const QString& folder) {
    if (folder.trimmed().isEmpty()) {
        return false;
    }
    QString f = normalized(folder);
    if (!f.endsWith(QLatin1Char('/'))) {
        f += QLatin1Char('/');
    }
    return normalized(path).startsWith(f);
}

QString fileKey(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(f.read(kKeyBytes));
    return QString::fromLatin1(hash.result().toHex()) + QLatin1Char(':') + QString::number(f.size());
}

bool isTrusted(const QString& path) { return isTrusted(path, StateDb::shared()); }

bool isTrusted(const QString& path, StateDb& db) {
    const QString key = fileKey(path);
    if (key.isEmpty() || !db.isOpen()) {
        return false;
    }
    const std::lock_guard lock(db.mutex());
    SqlStatement st(db.handle(), "SELECT 1 FROM trusted_files WHERE key = ?1 AND scope = ?2");
    st.text(1, key).text(2, QLatin1String(kScope));
    return st.step() == SQLITE_ROW;
}

bool trust(const QString& path) { return trust(path, StateDb::shared()); }

bool trust(const QString& path, StateDb& db) {
    const QString key = fileKey(path);
    if (key.isEmpty() || !db.isOpen()) {
        return false;
    }
    const std::lock_guard lock(db.mutex());
    SqlStatement st(
        db.handle(),
        "INSERT INTO trusted_files(key, scope, path, trusted_at) VALUES(?1, ?2, ?3, ?4) "
        "ON CONFLICT(key, scope) DO UPDATE SET path = excluded.path, trusted_at = excluded.trusted_at");
    st.text(1, key)
        .text(2, QLatin1String(kScope))
        .text(3, QFileInfo(path).absoluteFilePath())
        .integer(4, QDateTime::currentSecsSinceEpoch());
    const bool ok = st.run();
    if (ok) {
        spdlog::info("protected view: all features enabled for {}", path.toStdString());
    }
    return ok;
}

Origin check(const QString& path, bool attachment) {
    const Settings settings;
    return check(path, attachment, settings, StateDb::shared());
}

Origin check(const QString& path, bool attachment, const Settings& settings, StateDb& db) {
    const QString mode = settings.get(settings::kProtectedView);
    if (mode == QLatin1String("off")) {
        return Origin::None;
    }
    for (const QString& folder : settings.get(settings::kTrustedFolders)) {
        if (isUnder(path, folder)) {
            return Origin::None;
        }
    }
    if (isTrusted(path, db)) {
        return Origin::None;
    }
    if (attachment) {
        return Origin::Attachment;
    }
    if (mode == QLatin1String("all")) {
        return Origin::AllFiles;
    }
    if (zoneId(path) >= 3) {
        return Origin::Internet;
    }
    for (const QString& folder : tempFolders()) {
        if (isUnder(path, folder)) {
            return Origin::TempFolder;
        }
    }
    return Origin::None;
}

}  // namespace ot::protected_view
