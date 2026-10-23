#include "services/app_info.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibraryInfo>
#include <QSysInfo>
#include <QtGlobal>

#include <exception>

#include "core/doc/document.h"
#include "core/mu/context.h"

namespace ot::app {

QString version() { return QStringLiteral(OTTISK_VERSION_STR); }

QString mupdfVersion() { return QString::fromStdString(MuContext::version()); }

QString qtVersion() { return QString::fromLatin1(qVersion()); }

QString dataDir() {
    QString dir = qEnvironmentVariable("OTTISK_DATA_DIR");
    if (dir.isEmpty()) {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        dir = local.isEmpty() ? QDir::home().filePath(QStringLiteral("AppData/Local/Ottisk"))
                              : QDir(local).filePath(QStringLiteral("Ottisk"));
    }
    QDir().mkpath(dir);
    return QDir::cleanPath(dir);
}

QString instanceName() {
    const QString suffix = qEnvironmentVariable("OTTISK_INSTANCE");
    return suffix.isEmpty() ? QStringLiteral("Ottisk") : QStringLiteral("Ottisk-") + suffix;
}

bool selfCheck(QStringList& lines) {
    bool ok = true;
    lines << QStringLiteral("product: Ottisk");
    lines << QStringLiteral("version: ") + version();
    lines << QStringLiteral("qt: ") + qtVersion() + QStringLiteral(" (built ") +
                 QStringLiteral(QT_VERSION_STR) + QStringLiteral(")");
    lines << QStringLiteral("mupdf: ") + mupdfVersion();
    lines << QStringLiteral("os: ") + QSysInfo::prettyProductName() + QStringLiteral(" ") +
                 QSysInfo::currentCpuArchitecture();
    lines << QStringLiteral("exe: ") + QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    lines << QStringLiteral("data: ") + QDir::toNativeSeparators(dataDir());
    lines << QStringLiteral("plugins: ") +
                 QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::PluginsPath));
    try {
        MuContext mu;
        auto doc = Document::createBlank(mu, 1);
        const Bitmap bmp = doc->renderPage(0, {});
        const bool white = !bmp.empty() && bmp.pixels.front() == 255 && bmp.pixels.back() == 255;
        lines << QStringLiteral("render: %1x%2 %3")
                     .arg(bmp.width)
                     .arg(bmp.height)
                     .arg(white ? QStringLiteral("ok") : QStringLiteral("BAD"));
        ok = ok && white && bmp.width == 595 && bmp.height == 842;
    } catch (const std::exception& e) {
        lines << QStringLiteral("render: FAILED ") + QString::fromUtf8(e.what());
        ok = false;
    }
    lines << (ok ? QStringLiteral("result: ok") : QStringLiteral("result: FAILED"));
    return ok;
}

}  // namespace ot::app
