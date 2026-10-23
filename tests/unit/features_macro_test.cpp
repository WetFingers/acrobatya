// unit.features.* — OT_FEATURE (эталон 9.1) пишет строку OT_FEATURE:<id> в stdout (scope_report.py читает её
// из ctest.xml) и свойство feature теста (gtest-XML в build/<пресет>/gtest-xml). Тестовый ID test-00 в
// docs/scope.json не входит.
#include <gtest/gtest.h>

#include <string>

#include "core/util/features.h"

namespace {

bool hasProperty(const char* key, const char* value) {
    const testing::TestResult* result = testing::UnitTest::GetInstance()->current_test_info()->result();
    for (int i = 0; i < result->test_property_count(); ++i) {
        const testing::TestProperty& p = result->GetTestProperty(i);
        if (std::string(p.key()) == key && std::string(p.value()) == value) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(features, macro) {
    testing::internal::CaptureStdout();
    OT_FEATURE("test-00");
    const std::string out = testing::internal::GetCapturedStdout();
    EXPECT_NE(out.find("OT_FEATURE:test-00\n"), std::string::npos) << out;
}

TEST(features, macro_records_property) {
    OT_FEATURE("test-00");
    EXPECT_TRUE(hasProperty("feature", "test-00"));
}

TEST(features, macro_is_one_statement) {
    // do { … } while (0): макрос годится как единственная ветка if без фигурных скобок.
    bool flag = true;
    testing::internal::CaptureStdout();
    if (flag)
        OT_FEATURE("test-00");
    else
        flag = false;
    EXPECT_NE(testing::internal::GetCapturedStdout().find("OT_FEATURE:test-00"), std::string::npos);
    EXPECT_TRUE(flag);
}
