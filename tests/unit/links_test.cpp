// unit.links.* — ссылки страницы (план 2.2b, core/links/links.h; view-13): links.pdf (tools/gen/make_view.py)
// — восемь ссылок всех видов с рамками и назначениями из links.json: явное и именованное назначение (/XYZ,
// точка — координаты MuPDF), /Named /NextPage, /URI, /GoToR (файл и страница), /Launch (файл — только для
// сообщения), /JavaScript — Unsupported без адреса; /Fit; попадание по точке; не PDF и страница без ссылок.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/links/links.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::PointF;
using ot::links::DestFit;
using ot::links::LinkInfo;
using ot::links::LinkKind;

namespace {

fs::path viewFixture(const char* name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "view" / name;
}

nlohmann::json expected() {
    std::ifstream in(viewFixture("links.json"), std::ios::binary);
    return nlohmann::json::parse(in);
}

void expectRect(const ot::RectF& r, const nlohmann::json& e) {
    EXPECT_NEAR(r.x0, e[0].get<float>(), 0.05);
    EXPECT_NEAR(r.y0, e[1].get<float>(), 0.05);
    EXPECT_NEAR(r.x1, e[2].get<float>(), 0.05);
    EXPECT_NEAR(r.y1, e[3].get<float>(), 0.05);
}

}  // namespace

TEST(links, all_kinds_on_first_page) {
    OT_FEATURE("view-13");
    auto doc = Document::openOrThrow(viewFixture("links.pdf"));
    const auto meta = expected();
    const std::vector<LinkInfo> links = ot::links::pageLinks(*doc, 0);
    ASSERT_EQ(links.size(), 7u);
    for (std::size_t i = 0; i < links.size(); ++i) {
        SCOPED_TRACE(i);
        const auto& e = meta["links"][i];
        EXPECT_EQ(links[i].page, 0);
        EXPECT_GT(links[i].objnum, 0);
        const std::string kind = e["kind"].get<std::string>();
        if (kind == "JavaScript") {  // вид, которого нет в LinkKind, — Unsupported с именем /S
            EXPECT_EQ(links[i].action.kind, LinkKind::Unsupported);
            EXPECT_EQ(links[i].action.name, kind);
        } else {
            EXPECT_EQ(ot::links::linkKindName(links[i].action.kind), kind);
        }
        expectRect(links[i].rect, e["rect_mupdf"]);
    }
    // Явное назначение: страница 3, /XYZ 72 700 → точка в координатах MuPDF (y вниз), масштаб 0 — не менять.
    const auto& explicitDest = links[0].action;
    EXPECT_EQ(explicitDest.page, 2);
    EXPECT_EQ(explicitDest.fit, DestFit::XYZ);
    ASSERT_TRUE(explicitDest.point.has_value());
    EXPECT_NEAR(explicitDest.point->x, 72, 0.05);
    EXPECT_NEAR(explicitDest.point->y, meta["links"][0]["target"]["point"][1].get<float>(), 0.05);
    EXPECT_EQ(explicitDest.zoom, 0);
    EXPECT_TRUE(explicitDest.internal());
    // Именованное — из /Names /Dests.
    const auto& named = links[1].action;
    EXPECT_EQ(named.kind, LinkKind::GoTo);
    EXPECT_EQ(named.destName, "itogi");
    EXPECT_EQ(named.page, 2);
    ASSERT_TRUE(named.point.has_value());
    EXPECT_NEAR(named.point->y, meta["links"][1]["target"]["point"][1].get<float>(), 0.05);
    // /Named /NextPage со страницы 1 — страница 2.
    EXPECT_EQ(links[2].action.kind, LinkKind::Named);
    EXPECT_EQ(links[2].action.name, "NextPage");
    EXPECT_EQ(links[2].action.page, 1);
    // /URI.
    EXPECT_EQ(links[3].action.kind, LinkKind::Uri);
    EXPECT_EQ(links[3].action.uri, "https://example.org/akrobatya");
    EXPECT_FALSE(links[3].action.internal());
    // /GoToR: файл и страница другого документа (целое /D — с нуля).
    EXPECT_EQ(links[4].action.kind, LinkKind::GoToR);
    EXPECT_EQ(links[4].action.file, "other.pdf");
    EXPECT_EQ(links[4].action.page, 1);
    // /Launch: файл есть — запускать его никто не будет (эталон 7.6).
    EXPECT_EQ(links[5].action.kind, LinkKind::Launch);
    EXPECT_EQ(links[5].action.file, "calc.exe");
    EXPECT_FALSE(links[5].action.internal());
    // /JavaScript — MuPDF адреса не даёт: Unsupported с именем действия.
    EXPECT_EQ(links[6].action.kind, LinkKind::Unsupported);
    EXPECT_EQ(links[6].action.name, "JavaScript");
    EXPECT_TRUE(links[6].action.uri.empty());
}

TEST(links, fit_dest_and_hit_test) {
    auto doc = Document::openOrThrow(viewFixture("links.pdf"));
    const auto meta = expected();
    const std::vector<LinkInfo> back = ot::links::pageLinks(*doc, 1);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].action.kind, LinkKind::GoTo);
    EXPECT_EQ(back[0].action.page, 0);
    EXPECT_EQ(back[0].action.fit, DestFit::Fit);
    expectRect(back[0].rect, meta["links"][7]["rect_mupdf"]);
    EXPECT_TRUE(ot::links::pageLinks(*doc, 2).empty());

    const std::vector<LinkInfo> first = ot::links::pageLinks(*doc, 0);
    const ot::RectF& uri = first[3].rect;
    const auto hit = ot::links::linkAt(first, PointF{(uri.x0 + uri.x1) / 2, (uri.y0 + uri.y1) / 2});
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->action.kind, LinkKind::Uri);
    EXPECT_FALSE(ot::links::linkAt(first, PointF{20, 20}).has_value());
    EXPECT_TRUE(ot::links::linkAt(first, PointF{uri.x1 + 2, uri.y0 + 1}, 3).has_value());
    EXPECT_THROW(ot::links::pageLinks(*doc, 3), ot::MuError);
}

TEST(links, page_without_links) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "fixtures" / "text" / "search.pdf");
    EXPECT_TRUE(ot::links::pageLinks(*doc, 0).empty());
}
