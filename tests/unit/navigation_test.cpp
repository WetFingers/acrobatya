// unit.outlines.*, unit.destinations.*, unit.attachments.*, unit.layers.* — чтение для панелей навигации
// (план 2.4; core/links/{outlines,destinations,attachments,layers}.h; view-11, view-15, view-17, view-50,
// security-43). Фикстуры: tests/fixtures/view/nav.pdf + nav.json (tools/gen/make_view.py) — слои со
// вложенностью, выключенным и заблокированным, вложения дерева и аннотации, назначения /Names /Dests и /Dests
// каталога, закладки с /C и /F; tests/fixtures/demo/report.pdf — 7 закладок верхнего уровня с дочерними.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/links/attachments.h"
#include "core/links/destinations.h"
#include "core/links/layers.h"
#include "core/links/outlines.h"
#include "core/mu/error.h"
#include "core/render/renderer.h"
#include "core/util/features.h"
#include "services/document_session.h"
#include "services/nav_ops.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::links::DestFit;
using ot::links::LinkKind;

namespace {

fs::path fixture(const char* rel) { return ot::test::sourceDir() / "tests" / "fixtures" / rel; }

nlohmann::json navJson() {
    std::ifstream in(fixture("view/nav.json"), std::ios::binary);
    return nlohmann::json::parse(in);
}

std::unique_ptr<Document> openNav() { return Document::openOrThrow(fixture("view/nav.pdf")); }

// Пиксель RGB растра страницы при dpi в точке страницы (пункты, y вниз).
std::array<int, 3> pixelAt(const ot::Bitmap& b, float dpi, float x, float y) {
    const int px = static_cast<int>(x * dpi / 72.0f);
    const int py = static_cast<int>(y * dpi / 72.0f);
    const std::uint8_t* p = b.row(py) + static_cast<std::size_t>(px) * 3;
    return {p[0], p[1], p[2]};
}

}  // namespace

// ---------------------------------------------------------------- закладки

TEST(outlines, top_level_of_report_without_children_loaded) {
    OT_FEATURE("view-11");
    auto doc = Document::openOrThrow(fixture("demo/report.pdf"));
    ASSERT_TRUE(ot::outline::hasOutline(*doc));
    const auto top = ot::outline::children(*doc);
    ASSERT_EQ(top.size(), 7u);
    EXPECT_EQ(top[0].title, "Обложка");
    EXPECT_EQ(top[0].action.kind, LinkKind::GoTo);
    EXPECT_EQ(top[0].action.page, 0);
    EXPECT_EQ(top[1].action.page, 1);
    EXPECT_FALSE(top[0].hasChildren);
    EXPECT_TRUE(top[2].hasChildren);
    EXPECT_EQ(top[2].path, std::vector<int>({2}));
    const auto sub = ot::outline::children(*doc, top[2].path);
    ASSERT_EQ(sub.size(), 2u);
    EXPECT_EQ(sub[0].path, std::vector<int>({2, 0}));
    EXPECT_EQ(sub[0].action.page, 2);
    EXPECT_EQ(sub[1].action.page, 3);
    EXPECT_TRUE(ot::outline::children(*doc, {99}).empty());
    EXPECT_TRUE(ot::outline::children(*doc, {0}).empty());
}

TEST(outlines, style_color_and_open_state) {
    OT_FEATURE("view-11");
    auto doc = openNav();
    const auto top = ot::outline::children(*doc);
    const auto meta = navJson();
    ASSERT_EQ(top.size(), meta["outline_top"].size());
    for (std::size_t i = 0; i < top.size(); ++i) {
        EXPECT_EQ(top[i].title, meta["outline_top"][i].get<std::string>());
    }
    EXPECT_FALSE(top[0].color.has_value());  // без /C — цвет темы
    ASSERT_TRUE(top[1].color.has_value());
    EXPECT_EQ((*top[1].color)[0], 204);
    EXPECT_EQ((*top[1].color)[1], 0);
    EXPECT_TRUE(top[1].bold);
    EXPECT_FALSE(top[1].italic);
    EXPECT_TRUE(top[2].italic);
    EXPECT_TRUE(top[2].open);
    EXPECT_TRUE(top[2].hasChildren);
    EXPECT_FALSE(top[3].open);
    EXPECT_TRUE(top[3].hasChildren);
    EXPECT_EQ(top[3].action.page, 3);
    EXPECT_EQ(top[3].action.fit, DestFit::Fit);
    const auto sub = ot::outline::children(*doc, {2});
    ASSERT_EQ(sub.size(), 2u);
    EXPECT_EQ(sub[1].title, "Раздел 2.2");
}

TEST(outlines, none_in_blank) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "data" / "mini" / "blank-a4.pdf");
    EXPECT_FALSE(ot::outline::hasOutline(*doc));
    EXPECT_TRUE(ot::outline::children(*doc).empty());
}

// ---------------------------------------------------------------- назначения

TEST(destinations, names_tree_and_catalog_dests_sorted) {
    OT_FEATURE("view-50");
    auto doc = openNav();
    const auto list = ot::dests::list(*doc);
    const auto meta = navJson();
    ASSERT_EQ(list.size(), 4u);
    const char* names[] = {"chapter1", "chapter2", "intro", "summary"};
    for (std::size_t i = 0; i < list.size(); ++i) {
        EXPECT_EQ(list[i].name, names[i]);
        EXPECT_EQ(list[i].action.kind, LinkKind::GoTo);
    }
    EXPECT_EQ(list[0].action.page, 1);
    EXPECT_EQ(list[0].action.fit, DestFit::XYZ);
    ASSERT_TRUE(list[0].action.point.has_value());
    EXPECT_NEAR(list[0].action.point->x, 72, 0.1);
    EXPECT_NEAR(list[0].action.point->y, 841.8898 - 770, 0.1);  // y вниз
    EXPECT_EQ(list[1].action.page, 2);
    EXPECT_EQ(list[1].action.fit, DestFit::FitH);
    EXPECT_EQ(list[2].action.page, 0);
    EXPECT_EQ(list[3].action.page, 3);
    EXPECT_EQ(list[3].action.fit, DestFit::Fit);
    for (const auto& d : meta["destinations"]) {
        bool found = false;
        for (const auto& got : list) {
            found = found ||
                    (got.name == d["name"].get<std::string>() && got.action.page == d["page"].get<int>());
        }
        EXPECT_TRUE(found) << d["name"];
    }
}

TEST(destinations, links_fixture_and_none) {
    auto links = Document::openOrThrow(fixture("view/links.pdf"));
    const auto list = ot::dests::list(*links);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list[0].name, "itogi");
    EXPECT_EQ(list[0].action.page, 2);
    auto blank = Document::openOrThrow(ot::test::sourceDir() / "tests" / "data" / "mini" / "blank-a4.pdf");
    EXPECT_TRUE(ot::dests::list(*blank).empty());
}

// ---------------------------------------------------------------- вложения

TEST(attachments, list_tree_and_annotation) {
    OT_FEATURE("view-17");
    auto doc = openNav();
    const auto list = ot::attachments::list(*doc);
    const auto meta = navJson()["attachments"];
    ASSERT_EQ(list.size(), meta.size());
    for (std::size_t i = 0; i < list.size(); ++i) {
        SCOPED_TRACE(i);
        const auto& e = meta[i];
        EXPECT_EQ(list[i].fileName, e["name"].get<std::string>());
        EXPECT_EQ(list[i].description, e["description"].get<std::string>());
        EXPECT_EQ(list[i].size, e["size"].get<std::int64_t>());
        EXPECT_TRUE(list[i].embedded);
        if (e["source"] == "document") {
            EXPECT_EQ(list[i].source, ot::attachments::Source::Document);
            EXPECT_EQ(list[i].key, e["name"].get<std::string>());
        } else {
            EXPECT_EQ(list[i].source, ot::attachments::Source::Annotation);
            EXPECT_EQ(list[i].page, e["page"].get<int>());
            EXPECT_GT(list[i].objnum, 0);
        }
    }
}

TEST(attachments, contents_of_text_pdf_and_annotation) {
    auto doc = openNav();
    const auto list = ot::attachments::list(*doc);
    ASSERT_EQ(list.size(), 5u);
    EXPECT_EQ(ot::attachments::contents(*doc, list[1]), "Заметки: проверить итоги.\r\n");
    const std::string inner = ot::attachments::contents(*doc, list[0]);
    EXPECT_EQ(static_cast<std::int64_t>(inner.size()), list[0].size);
    auto innerDoc = Document::openOrThrowMemory(inner);
    EXPECT_EQ(innerDoc->pageCount(), 1);
    EXPECT_EQ(ot::attachments::contents(*doc, list[4]), "Месяц;Сумма\r\nЯнварь;100\r\n");
    ot::attachments::Attachment missing = list[1];
    missing.key = "nope.txt";
    EXPECT_THROW(ot::attachments::contents(*doc, missing), ot::MuError);
}

TEST(attachments, blocked_extensions_of_section_7_3) {
    OT_FEATURE("security-43");
    using ot::attachments::isBlocked;
    for (const char* bad :
         {"setup.exe", "A.EXE",       "run.bat",      "x.cmd",          "x.com", "x.js",  "x.jse",
          "x.vbs",     "x.vbe",       "x.ps1",        "x.msi",          "x.msp", "x.scr", "x.pif",
          "x.reg",     "x.hta",       "x.jar",        "x.lnk",          "x.wsf", "x.wsh", "x.cpl",
          "x.inf",     "trick.exe. ", "trick.exe...", "report.docx.exe"}) {
        EXPECT_TRUE(isBlocked(bad)) << bad;
    }
    for (const char* ok :
         {"report.docx", "notes.txt", "inner.pdf", "data.csv", "noext", "exe", "x.exe.txt", ""}) {
        EXPECT_FALSE(isBlocked(ok)) << ok;
    }
    EXPECT_EQ(ot::attachments::extensionOf("Report.DOCX"), "docx");
    EXPECT_EQ(ot::attachments::extensionOf("a.exe. "), "exe");
    EXPECT_EQ(ot::attachments::extensionOf("noext"), "");
    EXPECT_TRUE(ot::attachments::isPdf("Inner.PDF"));
    EXPECT_FALSE(ot::attachments::isPdf("inner.pdf.exe"));
}

TEST(attachments, safe_file_name_for_disk) {
    using ot::attachments::safeFileName;
    EXPECT_EQ(safeFileName("../../evil.exe"), "evil.exe");
    EXPECT_EQ(safeFileName("C:\\Windows\\x.txt"), "x.txt");
    EXPECT_EQ(safeFileName("dir/sub/Отчёт.docx"), "Отчёт.docx");
    EXPECT_EQ(safeFileName("a<b>:c|d?e*.txt"), "a_b__c_d_e_.txt");
    EXPECT_EQ(safeFileName("con.txt"), "_con.txt");
    EXPECT_EQ(safeFileName("NUL"), "_NUL");
    EXPECT_EQ(safeFileName("COM1.log"), "_COM1.log");
    EXPECT_EQ(safeFileName("name. . "), "name");
    EXPECT_EQ(safeFileName("   "), "attachment");
    EXPECT_EQ(safeFileName(""), "attachment");
    EXPECT_EQ(safeFileName(std::string("a\x01"
                                       "b.txt")),
              "a_b.txt");
}

TEST(attachments, none_in_blank) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "data" / "mini" / "blank-a4.pdf");
    EXPECT_TRUE(ot::attachments::list(*doc).empty());
}

// ---------------------------------------------------------------- слои

TEST(layers, rows_of_default_config) {
    OT_FEATURE("view-15");
    auto doc = openNav();
    ASSERT_TRUE(ot::layers::hasLayers(*doc));
    const auto rows = ot::layers::list(*doc);
    const auto meta = navJson()["layers"];
    ASSERT_EQ(rows.size(), meta.size());
    for (std::size_t i = 0; i < rows.size(); ++i) {
        SCOPED_TRACE(i);
        const auto& e = meta[i];
        EXPECT_EQ(rows[i].index, static_cast<int>(i));
        EXPECT_EQ(rows[i].text, e["text"].get<std::string>());
        EXPECT_EQ(rows[i].depth, e["depth"].get<int>());
        if (e.value("label", false)) {
            EXPECT_EQ(rows[i].kind, ot::layers::UiKind::Label);
        } else {
            EXPECT_EQ(rows[i].kind, ot::layers::UiKind::Checkbox);
            EXPECT_EQ(rows[i].on, e["on"].get<bool>());
            EXPECT_EQ(rows[i].locked, e["locked"].get<bool>());
        }
    }
}

TEST(layers, toggle_changes_render_and_reset_returns) {
    OT_FEATURE("view-15");
    auto doc = openNav();
    const float dpi = 36;
    const float cx = 595.2756f / 2;
    const float cy = 841.8898f / 2;
    const std::uint64_t initial = ot::layers::stateHash(*doc);
    EXPECT_NE(initial, 0u);
    auto before = ot::Renderer(*doc).renderPage(0, dpi);
    EXPECT_LT(pixelAt(before, dpi, cx, cy)[0], 40);  // квадрат слоя «Сетка» — чёрный

    ASSERT_TRUE(ot::layers::toggle(*doc, 0));
    EXPECT_FALSE(ot::layers::list(*doc)[0].on);
    const std::uint64_t hidden = ot::layers::stateHash(*doc);
    EXPECT_NE(hidden, initial);
    auto after = ot::Renderer(*doc).renderPage(0, dpi);
    EXPECT_GT(pixelAt(after, dpi, cx, cy)[0], 220);  // слой скрыт — бумага

    EXPECT_FALSE(ot::layers::toggle(*doc, 5));  // «Водяной знак» заблокирован
    EXPECT_TRUE(ot::layers::list(*doc)[5].on);
    EXPECT_FALSE(ot::layers::toggle(*doc, 4));  // подпись группы
    EXPECT_TRUE(ot::layers::setOn(*doc, 3, true));
    EXPECT_TRUE(ot::layers::list(*doc)[3].on);

    ot::layers::reset(*doc);
    const auto rows = ot::layers::list(*doc);
    EXPECT_TRUE(rows[0].on);
    EXPECT_FALSE(rows[3].on);
    EXPECT_EQ(ot::layers::stateHash(*doc), initial);
    auto again = ot::Renderer(*doc).renderPage(0, dpi);
    EXPECT_LT(pixelAt(again, dpi, cx, cy)[0], 40);
}

TEST(layers, none_in_blank) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "data" / "mini" / "blank-a4.pdf");
    EXPECT_FALSE(ot::layers::hasLayers(*doc));
    EXPECT_TRUE(ot::layers::list(*doc).empty());
    EXPECT_EQ(ot::layers::stateHash(*doc), 0u);
    EXPECT_FALSE(ot::layers::toggle(*doc, 0));
    ot::layers::reset(*doc);  // без слоёв — ничего
}

// ---------------------------------------------------------------- через DocumentSession

TEST(nav_ops, toggle_layer_is_view_state_not_edit) {
    OT_FEATURE("view-15");
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(fixture("view/nav.pdf").wstring())));
    const quint64 initial = session.layersHash();
    EXPECT_NE(initial, 0u);
    const quint64 revision = session.revision();
    int fired = 0;
    QObject::connect(&session, &ot::DocumentSession::layersChanged, [&fired] { ++fired; });
    const auto rows = ot::nav::toggleLayer(session, 0).result();
    ASSERT_FALSE(rows.empty());
    EXPECT_FALSE(rows[0].on);
    EXPECT_TRUE(ot::test::waitUntil([&] { return fired == 1; }));
    EXPECT_NE(session.layersHash(), initial);
    EXPECT_EQ(session.revision(), revision);  // не правка: revision и история те же
    EXPECT_FALSE(session.undoState().canUndo);
    ot::nav::toggleLayer(session, 5).result();  // заблокированный — без сигнала
    const auto reset = ot::nav::resetLayers(session).result();
    EXPECT_TRUE(reset[0].on);
    EXPECT_TRUE(ot::test::waitUntil([&] { return fired == 2; }));
    EXPECT_EQ(session.layersHash(), initial);
}

TEST(nav_ops, outline_attachments_and_save) {
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(fixture("view/nav.pdf").wstring())));
    EXPECT_EQ(ot::nav::outline(session).result().size(), 4u);
    EXPECT_EQ(ot::nav::outline(session, {2}).result().size(), 2u);
    EXPECT_EQ(ot::nav::destinations(session).result().size(), 4u);
    const auto files = ot::nav::attachments(session).result();
    ASSERT_EQ(files.size(), 5u);
    const fs::path out = fs::temp_directory_path() / L"ottisk-nav-ops-notes.txt";
    ot::nav::saveAttachment(session, files[1], QString::fromStdWString(out.wstring())).waitForFinished();
    std::ifstream in(out, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(text, "Заметки: проверить итоги.\r\n");
    in.close();
    fs::remove(out);
}
