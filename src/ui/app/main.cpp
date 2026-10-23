// Точка входа AcroBatya.exe (эталон 3.18, 10.3). Служебные режимы без окна (--version, --self-check,
// --register-notify) идут на QCoreApplication и не требуют платформенного плагина Qt. Обычный запуск — один
// экземпляр на пользователя (services/single_instance.h): второй передаёт файлы первому и выходит; без файлов
// в командной строке окно открывает вкладки прошлого сеанса, если это включено (general/restoreSession).
#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

#include <cstdio>
#include <memory>

#include <windows.h>
#include <shlobj.h>

#include <spdlog/spdlog.h>

#include "services/app_info.h"
#include "services/i18n.h"
#include "services/session_store.h"
#include "services/single_instance.h"
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
        hasArg(argc, argv, "--register-notify") || hasArg(argc, argv, "--shots-list")) {
        attachParentConsole();
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("AcroBatya"));
        const CliOptions opts = parseCli(QCoreApplication::arguments());
        if (opts.registerNotify) {
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return 0;
        }
        if (opts.shotsList) {
            for (const QString& name : sceneNames()) {
                printUtf8(name);
            }
            std::fflush(stdout);
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
    if (!opts.dataDir.isEmpty()) {  // до первого обращения к каталогу данных (настройки, state.sqlite)
        qputenv("OTTISK_DATA_DIR", QFileInfo(opts.dataDir).absoluteFilePath().toUtf8());
    }
    if (!opts.logLevel.isEmpty()) {
        spdlog::set_level(spdlog::level::from_str(opts.logLevel.toStdString()));
    }
    // Язык до первого окна: --lang, иначе настройка, иначе системный (эталон 5.7).
    ot::i18n::install(opts.lang.isEmpty() ? ot::i18n::fromSettings() : opts.lang);
    if (!opts.shots.isEmpty()) {
        attachParentConsole();
        if (qEnvironmentVariableIsEmpty("OTTISK_INSTANCE")) {
            qputenv("OTTISK_INSTANCE", "test");  // снимки никогда не говорят с окном человека
        }
        ShotOptions shot;
        shot.outDir = opts.shotsOut;
        shot.size = opts.shotsSize;
        shot.theme = parseThemeChoice(opts.theme).value_or(ThemeChoice::Light);
        shot.scale = opts.scale;
        shot.lang =
            opts.lang.isEmpty() || opts.lang == QLatin1String("system") ? QStringLiteral("ru") : opts.lang;
        return runShots(opts.shots, shot);
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
    // Второй экземпляр: файлы — первому (он поднимет окно), этот выходит.
    AllowSetForegroundWindow(ASFW_ANY);
    if (ot::SingleInstance::sendToRunning({opts.files, opts.page})) {
        return 0;
    }
    MainWindow window;
    ot::SingleInstance instance;
    if (instance.listen()) {
        QObject::connect(&instance, &ot::SingleInstance::requestReceived, &window,
                         &MainWindow::handleRequest);
    }
    window.show();
    if (!opts.files.isEmpty()) {
        window.openFiles(opts.files, opts.page - 1);
    } else if (MainWindow::restoreSessionEnabled()) {
        window.restoreSession(ot::session::load());
    }
    if (opts.smokeSeconds > 0) {
        QTimer::singleShot(opts.smokeSeconds * 1000, &app, &QApplication::quit);
    }
    return app.exec();
}
