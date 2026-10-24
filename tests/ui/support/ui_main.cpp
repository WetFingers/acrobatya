// main() для ui_tests: один QApplication на процесс, наборы — по имени (ctest ui.<имя>) или все подряд.
#include <QApplication>
#include <QtTest/QtTest>

#include <cstdio>
#include <cstring>

#include "services/print_job.h"
#include "services/protected_view.h"
#include "services/shell_launcher.h"
#include "support/ui_test.h"
#include "ui/theme/theme.h"
#include "ui/tools/tool_registry.h"

namespace ot::test {

std::vector<UiSuite>& uiSuites() {
    static std::vector<UiSuite> suites;
    return suites;
}

}  // namespace ot::test

int main(int argc, char** argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    ot::ui::Theme::apply(app, ot::ui::ThemeChoice::Light);
    ot::ui::registerBuiltinTools(ot::ui::ToolRegistry::instance());  // как main.cpp программы
    // Оболочка Windows в ui-тестах — только фейк (эталон 8.3): ни браузера, ни Word, ни Проводника.
    static ot::FakeShellLauncher shell;
    ot::ShellLauncher::setInstance(&shell);
    // Принтеры — только демонстрационные в памяти (2.6): живой принтер в тестах не открывается.
    static ot::ScopedPrintDevices printers(ot::demoPrinters(), ot::demoPrinterFactory());
    // Файлы тестов лежат в QTemporaryDir (%TEMP%) — для защищённого просмотра (2.5b) временные папки тестов
    // не «чужие»; набор protected_view проверяет это правило, подставляя свою папку.
    ot::protected_view::setTempFoldersForTesting(QStringList{});
    auto& suites = ot::test::uiSuites();
    if (argc > 1 && std::strcmp(argv[1], "--list") == 0) {
        for (const auto& s : suites) {
            std::printf("%s\n", s.name);
        }
        return 0;
    }
    // ui_tests <набор> [аргументы QTest…]: имя набора убирается из argv, остальное — QTest.
    const char* only = nullptr;
    std::vector<char*> args(argv, argv + argc);
    if (argc > 1 && argv[1][0] != '-') {
        only = argv[1];
        args.erase(args.begin() + 1);
    }
    int failed = 0;
    bool found = false;
    for (const auto& s : suites) {
        if (only != nullptr && std::strcmp(only, s.name) != 0) {
            continue;
        }
        found = true;
        std::unique_ptr<QObject> suite = s.make();
        failed += QTest::qExec(suite.get(), static_cast<int>(args.size()), args.data());
        ot::ui::Theme::apply(app, ot::ui::ThemeChoice::Light);
    }
    if (!found) {
        std::fprintf(stderr, "ui_tests: unknown suite %s (see --list)\n", only != nullptr ? only : "");
        return 2;
    }
    return failed == 0 ? 0 : 1;
}
