// Наборы QTest в одном ui_tests.exe: файл tests/ui/<имя>_test.cpp регистрирует класс OT_UI_TEST(Класс,
// "<имя>"), CMake по тому же имени файла заводит ctest ui.<имя> (`ui_tests <имя> -o …`). Без имени ui_tests
// гоняет все наборы, `ui_tests --list` печатает имена. QApplication создаёт ui_main.cpp (offscreen из
// окружения ctest).
#pragma once

#include <QObject>

#include <functional>
#include <memory>
#include <vector>

namespace ot::test {

struct UiSuite {
    const char* name;
    std::function<std::unique_ptr<QObject>()> make;
};

std::vector<UiSuite>& uiSuites();

struct UiSuiteRegistrar {
    UiSuiteRegistrar(const char* name, std::function<std::unique_ptr<QObject>()> make) {
        uiSuites().push_back({name, std::move(make)});
    }
};

}  // namespace ot::test

#define OT_UI_TEST(Class, name)                                \
    static const ot::test::UiSuiteRegistrar otUiSuite_##Class( \
        name, [] { return std::unique_ptr<QObject>(new Class); })
