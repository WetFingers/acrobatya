// unit.page_labels.* — логические номера страниц /PageLabels и ввод поля страницы (план 2.4; эталон 3.1
// «Навигация», 6.1 «Строка состояния»; view-08): demo/report.pdf — i, ii, 1…10; view/nav.pdf — i…iv; без
// дерева — номера; ввод «iv» или «3» (логический, затем физический), «(N)» — физический, регистр римских цифр
// не важен.
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/pages/labels.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::pages::pageLabel;
using ot::pages::pageLabels;
using ot::pages::parsePageInput;

namespace {

std::filesystem::path fixture(const char* rel) { return ot::test::sourceDir() / "tests" / "fixtures" / rel; }

}  // namespace

TEST(page_labels, report_roman_then_decimal) {
    OT_FEATURE("view-08");
    auto doc = Document::openOrThrow(fixture("demo/report.pdf"));
    const std::vector<std::string> labels = pageLabels(*doc);
    ASSERT_EQ(labels.size(), 12u);
    EXPECT_EQ(labels[0], "i");
    EXPECT_EQ(labels[1], "ii");
    EXPECT_EQ(labels[2], "1");
    EXPECT_EQ(labels[11], "10");
    EXPECT_EQ(pageLabel(labels, 1), "ii");
    // «3» — логическая «3» (5-я страница), а не 3-я.
    EXPECT_EQ(parsePageInput(labels, "3", 12), 4);
    EXPECT_EQ(parsePageInput(labels, "ii", 12), 1);
    EXPECT_EQ(parsePageInput(labels, " II ", 12), 1);
    // «(3)» — физическая третья.
    EXPECT_EQ(parsePageInput(labels, "(3)", 12), 2);
    // Без логических номеров — физические.
    EXPECT_EQ(parsePageInput(labels, "3", 12, false), 2);
    // Логической «11» нет — физическая одиннадцатая.
    EXPECT_EQ(parsePageInput(labels, "11", 12), 10);
    EXPECT_EQ(parsePageInput(labels, "iv", 12), std::nullopt);
    EXPECT_EQ(parsePageInput(labels, "", 12), std::nullopt);
    EXPECT_EQ(parsePageInput(labels, "0", 12), std::nullopt);
}

TEST(page_labels, nav_accepts_iv_and_number_fallback) {
    OT_FEATURE("view-08");
    auto doc = Document::openOrThrow(fixture("view/nav.pdf"));
    const std::vector<std::string> labels = pageLabels(*doc);
    ASSERT_EQ(labels.size(), 4u);
    EXPECT_EQ(labels[3], "iv");
    EXPECT_EQ(parsePageInput(labels, "iv", 4), 3);
    EXPECT_EQ(parsePageInput(labels, "IV", 4), 3);
    // Логической «3» нет — физическая третья.
    EXPECT_EQ(parsePageInput(labels, "3", 4), 2);
    EXPECT_EQ(parsePageInput(labels, "5", 4), std::nullopt);
    EXPECT_EQ(parsePageInput(labels, "v", 4), std::nullopt);
}

TEST(page_labels, none_gives_numbers) {
    auto doc = Document::openOrThrow(fixture("view/links.pdf"));
    const std::vector<std::string> labels = pageLabels(*doc);
    EXPECT_TRUE(labels.empty());
    EXPECT_EQ(pageLabel(labels, 2), "3");
    EXPECT_EQ(parsePageInput(labels, "2", 3), 1);
    EXPECT_EQ(parsePageInput(labels, "abc", 3), std::nullopt);
}
