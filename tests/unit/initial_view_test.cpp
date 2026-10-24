// unit.initial_view.* — начальный вид при открытии (план 2.4, чтение; 2.5 — запись и Base URL; эталон 3.1
// «Acrobat», «Свойства документа → Начальный вид»; view-47): /PageMode, /PageLayout, /OpenAction (назначение
// — страница и вид, как у ссылок), основные /ViewerPreferences и заголовок /Info для DisplayDocTitle.
// Фикстуры: view/nav.pdf (/OpenAction [стр. 2 /Fit], /DisplayDocTitle), demo/report.pdf (/PageMode
// /UseOutlines), view/layout-mixed.pdf (/PageLayout /TwoColumnRight), mini/blank-a4.pdf (ничего).
#include <gtest/gtest.h>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/links/initial_view.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::InitialView;
using ot::readInitialView;
using ot::writeInitialView;
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

namespace {

InitialView sample() {
    InitialView v;
    v.pageMode = "UseThumbs";
    v.pageLayout = "TwoColumnLeft";
    ot::links::LinkAction a;
    a.kind = LinkKind::GoTo;
    a.page = 2;
    a.fit = DestFit::Fit;
    v.openAction = a;
    v.hideMenubar = true;
    v.hideToolbar = true;
    v.fitWindow = true;
    v.centerWindow = true;
    v.displayDocTitle = true;
    v.direction = "R2L";
    v.nonFullScreenPageMode = "UseOutlines";
    return v;
}

void expectSample(const InitialView& r) {
    EXPECT_EQ(r.pageMode, "UseThumbs");
    EXPECT_EQ(r.pageLayout, "TwoColumnLeft");
    ASSERT_TRUE(r.openAction.has_value());
    EXPECT_EQ(r.openAction->page, 2);
    EXPECT_EQ(r.openAction->fit, DestFit::Fit);
    EXPECT_TRUE(r.hideMenubar);
    EXPECT_TRUE(r.hideToolbar);
    EXPECT_FALSE(r.hideWindowUI);
    EXPECT_TRUE(r.fitWindow);
    EXPECT_TRUE(r.centerWindow);
    EXPECT_TRUE(r.displayDocTitle);
    EXPECT_EQ(r.direction, "R2L");
    EXPECT_EQ(r.nonFullScreenPageMode, "UseOutlines");
}

}  // namespace

// Запись (2.5, «Свойства → Начальный вид»): одна операция журнала «Initial view» — после записи, сохранения и
// переоткрытия значения те же; undo возвращает прежний вид файла.
TEST(initial_view, write_save_reopen_and_undo) {
    OT_FEATURE("view-47");
    auto doc = Document::openOrThrow(fixture("demo/report.pdf"));
    writeInitialView(*doc, sample());
    expectSample(readInitialView(*doc));
    EXPECT_EQ(doc->undoStack().undoName(), "Initial view");
    auto reopened = Document::openOrThrowMemory(doc->savePdfToMemory());
    expectSample(readInitialView(*reopened));

    auto again = Document::openOrThrow(fixture("demo/report.pdf"));
    writeInitialView(*again, sample());
    (void)again->undoStack().undo();
    const InitialView back = readInitialView(*again);
    EXPECT_EQ(back.pageMode, "UseOutlines");
    EXPECT_TRUE(back.pageLayout.empty());
    EXPECT_FALSE(back.openAction.has_value());
    EXPECT_FALSE(back.hideMenubar);
}

// Пустые значения и false удаляют ключи; /ViewerPreferences без ключей уходит целиком; /XYZ с масштабом.
TEST(initial_view, defaults_remove_keys_and_xyz_zoom) {
    auto doc = Document::openOrThrow(fixture("view/nav.pdf"));  // /OpenAction, /DisplayDocTitle
    InitialView v;
    ot::links::LinkAction a;
    a.kind = LinkKind::GoTo;
    a.page = 3;
    a.fit = DestFit::XYZ;
    a.zoom = 2.0f;
    v.openAction = a;
    writeInitialView(*doc, v);
    InitialView r = readInitialView(*doc);
    EXPECT_TRUE(r.pageMode.empty());
    EXPECT_FALSE(r.displayDocTitle);
    ASSERT_TRUE(r.openAction.has_value());
    EXPECT_EQ(r.openAction->page, 3);
    EXPECT_FLOAT_EQ(r.openAction->zoom, 2.0f);
    EXPECT_EQ(r.title, "Навигация: слои, вложения, назначения");  // /Info не трогается
    writeInitialView(*doc, InitialView{});
    r = readInitialView(*doc);
    EXPECT_FALSE(r.openAction.has_value());
    EXPECT_FALSE(ot::hasViewerPreferences(*doc));
}

// Base URL (/URI /Base каталога, 2.5): чтение, запись, удаление; операция журнала «Base URL».
TEST(initial_view, base_url_read_write_and_undo) {
    OT_FEATURE("view-45");
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "data" / "mini" / "blank-a4.pdf");
    EXPECT_EQ(ot::baseUrl(*doc), "");
    ot::setBaseUrl(*doc, "https://example.org/docs/");
    EXPECT_EQ(ot::baseUrl(*doc), "https://example.org/docs/");
    EXPECT_EQ(doc->undoStack().undoName(), "Base URL");
    auto reopened = Document::openOrThrowMemory(doc->savePdfToMemory());
    EXPECT_EQ(ot::baseUrl(*reopened), "https://example.org/docs/");
    ot::setBaseUrl(*reopened, "");
    EXPECT_EQ(ot::baseUrl(*reopened), "");
}
