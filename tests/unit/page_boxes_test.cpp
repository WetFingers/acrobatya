// unit.page_boxes.* — рамки областей страницы (план 2.5b; эталон 3.1 «Сглаживание и качество… показать
// Art/Trim/Bleed»; view-36): /BleedBox, /TrimBox, /ArtBox читаются в координатах страницы MuPDF (как
// fz_bound_page — /Rotate и /CropBox учтены), заданные отмечены has*; незаданная рамка — как обрезка.
// Ожидания — tests/fixtures/view/boxes.json (tools/gen/make_view.py).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/pages/boxes.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

using nlohmann::json;
using ot::RectF;

namespace {

void expectRect(const RectF& r, double x0, double y0, double x1, double y1) {
    EXPECT_NEAR(r.x0, x0, 0.01);
    EXPECT_NEAR(r.y0, y0, 0.01);
    EXPECT_NEAR(r.x1, x1, 0.01);
    EXPECT_NEAR(r.y1, y1, 0.01);
}

}  // namespace

TEST(page_boxes, read_bleed_trim_art_in_page_coordinates) {
    OT_FEATURE("view-36");
    const auto dir = ot::test::sourceDir() / L"tests/fixtures/view";
    const json meta = json::parse(ot::paths::readFile(dir / L"boxes.json"));
    auto doc = ot::Document::openOrThrow(dir / L"boxes.pdf");
    ASSERT_EQ(doc->pageCount(), 2);

    // Стр. 1: /Rotate 0, без /CropBox — y вниз от верха MediaBox.
    const double h = meta["media"][3].get<double>();
    const ot::pages::PageBoxes p1 = ot::pages::pageBoxes(*doc, 0);
    EXPECT_TRUE(p1.hasBleed);
    EXPECT_TRUE(p1.hasTrim);
    EXPECT_TRUE(p1.hasArt);
    const json& b1 = meta["pages"][0];
    expectRect(p1.bleed, b1["bleed"][0], h - b1["bleed"][3].get<double>(), b1["bleed"][2],
               h - b1["bleed"][1].get<double>());
    expectRect(p1.trim, b1["trim"][0], h - b1["trim"][3].get<double>(), b1["trim"][2],
               h - b1["trim"][1].get<double>());
    expectRect(p1.art, b1["art"][0], h - b1["art"][3].get<double>(), b1["art"][2],
               h - b1["art"][1].get<double>());
    expectRect(p1.crop, 0, 0, meta["media"][2], h);

    // Стр. 2: /CropBox и /Rotate 90 — рамки повёрнуты вместе со страницей и лежат внутри неё.
    const ot::pages::PageBoxes p2 = ot::pages::pageBoxes(*doc, 1);
    const RectF page = doc->pageBox(1);
    expectRect(p2.crop, page.x0, page.y0, page.x1, page.y1);
    EXPECT_GT(p2.trim.width(), p2.trim.height());  // альбомная после поворота
    EXPECT_GT(p2.bleed.width(), p2.trim.width());
    EXPECT_GE(p2.trim.x0, page.x0);
    EXPECT_LE(p2.trim.x1, page.x1);
    // TrimBox 34…561 × 34…808 в CropBox 10…585 × 10…832, поворот 90: ширина — высота трима.
    EXPECT_NEAR(p2.trim.width(), 807.89 - 34, 0.01);
    EXPECT_NEAR(p2.trim.height(), 561.28 - 34, 0.01);

    // Страница без рамок: has* = false, рамки — как обрезка.
    auto blank = ot::Document::createBlank(1);
    const ot::pages::PageBoxes none = ot::pages::pageBoxes(*blank, 0);
    EXPECT_FALSE(none.hasBleed || none.hasTrim || none.hasArt);
    expectRect(none.trim, none.crop.x0, none.crop.y0, none.crop.x1, none.crop.y1);
}
