// OT_FEATURE("view-10") — отметка «этот тест закрывает функцию каталога» (эталон 9.1).
// В gtest-тесте: RecordProperty("feature", id) (попадает в build/<пресет>/gtest-xml/*.xml) и строка
// «OT_FEATURE:<id>» в stdout; в QTest и прочих — только строка в stdout (scope_report.py берёт её из
// <system-out> ctest.xml). ID — из docs/scope.json; макрос не используется в рабочем коде.
#pragma once

#include <cstdio>

#if defined(GTEST_INCLUDE_GTEST_GTEST_H_) || defined(GOOGLETEST_INCLUDE_GTEST_GTEST_H_)
#define OT_FEATURE(id)                                  \
    do {                                                \
        ::testing::Test::RecordProperty("feature", id); \
        std::printf("OT_FEATURE:%s\n", id);             \
        std::fflush(stdout);                            \
    } while (0)
#else
#define OT_FEATURE(id)                      \
    do {                                    \
        std::printf("OT_FEATURE:%s\n", id); \
        std::fflush(stdout);                \
    } while (0)
#endif
