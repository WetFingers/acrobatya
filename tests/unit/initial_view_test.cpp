// unit.initial_view.* — начальный вид при открытии (план 2.4, чтение; запись — 2.5; эталон 3.1 «Acrobat»,
// «Свойства документа → Начальный вид»; view-47): /PageMode, /PageLayout, /OpenAction (назначение — страница
// и вид, как у ссылок), основные /ViewerPreferences и заголовок /Info для DisplayDocTitle. Фикстуры:
// view/nav.pdf (/OpenAction [стр. 2 /Fit], /DisplayDocTitle), demo/report.pdf (/PageMode /UseOutlines),
// view/layout-mixed.pdf (/PageLayout /TwoColumnRight), mini/blank-a4.pdf (ничего).
#include <gtest/gtest.h>

#include "core/doc/document.h"
#include "core/links/initial_view.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::readInitialView;
using ot::links::DestFit;
using ot::links::LinkKind;

namespace {

std::filesystem::path fixture(const char* rel) { return ot::test::sourceDir() / "tests" / "fixtures" / rel; }

}  // namespace

TEST(initial_view, open_action_and_display_title) {
    OT_FEATURE("view-47");
    auto doc = Document::openOrThrow(fixture("view/nav.pdf"));
    const ot::InitialView v = readInitialView(*doc);
    ASSERT_TRUE(v.openAction.has_value());
    EXPECT_EQ(v.openAction->kind, LinkKind::GoTo);
    EXPECT_EQ(v.openAction->page, 1);
    EXPECT_EQ(v.openAction->fit, DestFit::Fit);
    EXPECT_TRUE(v.displayDocTitle);
    EXPECT_EQ(v.title, "Навигация: слои, вложения, назначения");
    EXPECT_TRUE(v.pageMode.empty());
    EXPECT_FALSE(v.hideToolbar);
    EXPECT_FALSE(v.hideWindowUI);
}

TEST(initial_view, page_mode_and_layout) {
    OT_FEATURE("view-47");
    auto report = Document::openOrThrow(fixture("demo/report.pdf"));
    EXPECT_EQ(readInitialView(*report).pageMode, "UseOutlines");
    EXPECT_FALSE(readInitialView(*report).openAction.has_value());
    auto mixed = Document::openOrThrow(fixture("view/layout-mixed.pdf"));
    EXPECT_EQ(readInitialView(*mixed).pageLayout, "TwoColumnRight");
}

TEST(initial_view, nothing_in_blank) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "data" / "mini" / "blank-a4.pdf");
    const ot::InitialView v = readInitialView(*doc);
    EXPECT_TRUE(v.pageMode.empty());
    EXPECT_FALSE(v.openAction.has_value());
    EXPECT_FALSE(v.displayDocTitle);
    EXPECT_TRUE(v.direction.empty());
}
