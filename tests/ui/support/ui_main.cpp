// main() для ui_tests: один QApplication на процесс, наборы — по имени (ctest ui.<имя>) или все подряд.
#include <QApplication>
#include <QtTest/QtTest>

#include <cstdio>
#include <cstring>

#include "support/ui_test.h"
#include "ui/theme/theme.h"

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
