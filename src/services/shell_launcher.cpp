#include "services/shell_launcher.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>

#include <spdlog/spdlog.h>

#include <atomic>

namespace ot {

namespace {

ShellLauncher& realLauncher() {
    static ShellLauncher real;
    return real;
}

std::atomic<ShellLauncher*> current{nullptr};

bool refuse(const char* what, const QString& target) {
    spdlog::warn("shell: {} refused without a live desktop: {}", what, target.toStdString());
    return false;
}

}  // namespace

bool ShellLauncher::liveDesktop() {
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return false;
    }
    const QString platform = QGuiApplication::platformName();
    return platform != QLatin1String("offscreen") && platform != QLatin1String("minimal");
}

bool ShellLauncher::openUrl(const QUrl& url) {
    if (!liveDesktop()) {
        return refuse("openUrl", url.toString());
    }
    spdlog::info("shell: open url {}", url.toString().toStdString());
    return QDesktopServices::openUrl(url);
}

bool ShellLauncher::openFile(const QString& path) {
    if (!liveDesktop()) {
        return refuse("openFile", path);
    }
    spdlog::info("shell: open file {}", path.toStdString());
    return QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

bool ShellLauncher::openSettings(const QString& page) {
    if (!liveDesktop()) {
        return refuse("openSettings", page);
    }
    spdlog::info("shell: open settings {}", page.toStdString());
    return QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:") + page));
}

bool ShellLauncher::showInExplorer(const QString& path) {
    if (!liveDesktop()) {
        return refuse("showInExplorer", path);
    }
    spdlog::info("shell: show in explorer {}", path.toStdString());
    const QString native = QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath());
    // «/select,» отдельным аргументом: Проводник принимает путь с пробелами в кавычках следующим словом.
    return QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,"), native});
}

ShellLauncher& ShellLauncher::instance() {
    ShellLauncher* l = current.load();
    return l != nullptr ? *l : realLauncher();
}

void ShellLauncher::setInstance(ShellLauncher* launcher) { current.store(launcher); }

bool FakeShellLauncher::record(const QString& kind, const QString& target) {
    calls.push_back({kind, target});
    return result;
}

bool FakeShellLauncher::openUrl(const QUrl& url) { return record(QStringLiteral("openUrl"), url.toString()); }

bool FakeShellLauncher::openFile(const QString& path) { return record(QStringLiteral("openFile"), path); }

bool FakeShellLauncher::openSettings(const QString& page) {
    return record(QStringLiteral("openSettings"), page);
}

bool FakeShellLauncher::showInExplorer(const QString& path) {
    return record(QStringLiteral("showInExplorer"), path);
}

ScopedShellLauncher::ScopedShellLauncher(ShellLauncher& launcher) : previous_(current.load()) {
    ShellLauncher::setInstance(&launcher);
}

ScopedShellLauncher::~ScopedShellLauncher() { ShellLauncher::setInstance(previous_); }

}  // namespace ot
