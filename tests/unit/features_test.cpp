// unit.features.macro — OT_FEATURE пишет строку OT_FEATURE:<id> в stdout (scope_report.py читает её
// из ctest.xml) и свойство feature в gtest-XML. Тестовый ID test-00 в docs/scope.json не входит.
#include <gtest/gtest.h>

#include <string>

#include "core/util/features.h"

TEST(features, macro) {
    testing::internal::CaptureStdout();
    OT_FEATURE("test-00");
    const std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("OT_FEATURE:test-00"), std::string::npos) << out;
}
