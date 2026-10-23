// Точка входа AcroBatya.exe (эталон 3.18, 10.3). Служебные режимы без окна (--version, --self-check,
// --register-notify) идут на QCoreApplication и не требуют платформенного плагина Qt.
#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include <cstdio>
#include <memory>

#include <windows.h>
#include <shlobj.h>

#include "services/app_info.h"
#include "ui/app/cli.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/theme/theme.h"
#include "ui/widgets/showcase.h"

namespace {

// Программа собрана как WIN32 (без консоли). Если вывод не перенаправлен (запуск из cmd/PowerShell
// напрямую), подключаемся к консоли родителя, чтобы --self-check и --version было видно.
void attachParentConsole() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_UNKNOWN) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
    }
}

void printUtf8(const QString& s) {
    const QByteArray utf8 = s.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
}

// Qt offscreen (снимки, --smoke в установленной копии) берёт шрифты только из QT_QPA_FONTDIR;
// без неё кириллица рисуется квадратами. Подставляем системную папку шрифтов, если переменная пуста.
void defaultOffscreenFonts() {
    if (qEnvironmentVariable("QT_QPA_PLATFORM") != QLatin1String("offscreen") ||
        !qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
        return;
    }
    const QString windir = qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows"));
    qputenv("QT_QPA_FONTDIR", QString(windir + QStringLiteral("/Fonts")).toUtf8());
}

}  // namespace

int main(int argc, char** argv) {
    using namespace ot::ui;
    defaultOffscreenFonts();
    if (hasArg(argc, argv, "--version") || hasArg(argc, argv, "--self-check") ||
        hasArg(argc, argv, "--register-notify")) {
        attachParentConsole();
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("AcroBatya"));
        const CliOptions opts = parseCli(QCoreApplication::arguments());
        if (opts.registerNotify) {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return 0;
        }
        if (opts.version) {
            printUtf8(QStringLiteral("AcroBatya ") + ot::app::version());
            std::fflush(stdout);
            return 0;
        }
        QStringList lines;
        const bool ok = ot::app::selfCheck(lines);
        for (const QString& line : lines) {
            printUtf8(line);
        }
        std::fflush(stdout);
        return ok ? 0 : 1;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("AcroBatya"));
    QApplication::setApplicationVersion(ot::app::version());
    const CliOptions opts = parseCli(QApplication::arguments());
    if (!opts.error.isEmpty()) {
        attachParentConsole();
        printUtf8(opts.error);
        return 2;
    }
    if (!opts.shots.isEmpty()) {
        attachParentConsole();
        return runShots(opts.shots, opts.shotsOut, opts.shotsSize, opts.theme);
    }

    // Тема до создания окна: Fusion, палитра и шрифт из токенов (эталон 6.4); по умолчанию — «Системная».
    Theme::apply(app, parseThemeChoice(opts.theme).value_or(ThemeChoice::System), opts.scale);
    if (opts.showcase) {
        std::unique_ptr<QWidget> showcase(createShowcaseWindow());
        showcase->show();
        if (opts.smokeSeconds > 0) {
            QTimer::singleShot(opts.smokeSeconds * 1000, &app, &QApplication::quit);
        }
        return app.exec();
    }
    MainWindow window;
    window.show();
    if (!opts.files.isEmpty()) {
        window.openFile(opts.files.first());
    }
    if (opts.smokeSeconds > 0) {
        QTimer::singleShot(opts.smokeSeconds * 1000, &app, &QApplication::quit);
    }
    return app.exec();
}
