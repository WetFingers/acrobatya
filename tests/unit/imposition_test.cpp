// unit.imposition.* — спуск полос печати (план 2.6; эталон 3.3 «Размер и обработка»; view-38…41): n-up 4 на
// A4 книжной для 10 страниц — 3 листа и позиции по tests/fixtures/print/nup4_a4.json, порядок и автоповорот
// n-up, «Авто» для 2 на листе; буклет 10 страниц → 12 → листы [12,1][2,11][10,3][4,9][8,5][6,7], «только
// лицо», «только оборот» (в обратном порядке), корешок справа; постер A1 на A4 с перекрытием 10 мм и
// метками реза; «вписать» A5 на Letter — масштаб > 1, «уменьшить негабаритные» — A5 без масштаба,
// A1 — вписан; ориентация «Авто» по странице; матрица размещения с поворотом.
#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>

#include "core/print/imposition.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Matrix;
using ot::PointF;
using ot::RectF;
using ot::print::Binding;
using ot::print::BookletSubset;
using ot::print::impose;
using ot::print::ImposeOptions;
using ot::print::kPointsPerMm;
using ot::print::Layout;
using ot::print::NupOrder;
using ot::print::Orientation;
using ot::print::Paper;
using ot::print::Placement;
using ot::print::Sheet;
using ot::print::SheetSide;
using ot::print::SizeF;
using ot::print::SizeMode;

namespace {

constexpr SizeF kA4{595.2756f, 841.8898f};
constexpr SizeF kA4Landscape{841.8898f, 595.2756f};
constexpr SizeF kA5{419.5276f, 595.2756f};
constexpr SizeF kA1{1683.7795f, 2383.937f};

std::vector<int> sequence(int n) {
    std::vector<int> v;
    for (int i = 0; i < n; ++i) {
        v.push_back(i);
    }
    return v;
}

void expectRect(const RectF& r, float x0, float y0, float x1, float y1, float eps = 0.01f) {
    EXPECT_NEAR(r.x0, x0, eps);
    EXPECT_NEAR(r.y0, y0, eps);
    EXPECT_NEAR(r.x1, x1, eps);
    EXPECT_NEAR(r.y1, y1, eps);
}

bool inside(const RectF& outer, const RectF& r, float eps = 0.01f) {
    return r.x0 >= outer.x0 - eps && r.y0 >= outer.y0 - eps && r.x1 <= outer.x1 + eps &&
           r.y1 <= outer.y1 + eps;
}

PointF apply(const Matrix& m, float x, float y) { return {x * m.a + y * m.c + m.e, x * m.b + y * m.d + m.f}; }

// Страницы листа по порядку размещений (номера с 1, пусто — 0).
std::vector<int> pagesOf(const Sheet& s) {
    std::vector<int> v;
    for (const Placement& p : s.placements) {
        v.push_back(p.page + 1);
    }
    return v;
}

}  // namespace

TEST(imposition, nup4_a4_json) {
    OT_FEATURE("view-39");
    std::ifstream in(ot::test::sourceDir() / "tests" / "fixtures" / "print" / "nup4_a4.json",
                     std::ios::binary);
    ASSERT_TRUE(in.good());
    const nlohmann::json e = nlohmann::json::parse(in);
    const std::vector<SizeF> sizes(e["pages"].get<int>(), kA4);
    ImposeOptions o;
    o.layout = Layout::Multiple;
    o.cols = e["cols"].get<int>();
    o.rows = e["rows"].get<int>();
    o.orientation = Orientation::Portrait;
    const auto sheets =
        impose(sizes, sequence(static_cast<int>(sizes.size())), Paper::a4(e["paper"]["margin"]), o);
    ASSERT_EQ(sheets.size(), e["sheets"].size());
    ASSERT_EQ(sheets.size(), 3u);
    for (std::size_t k = 0; k < sheets.size(); ++k) {
        const auto& es = e["sheets"][k];
        EXPECT_EQ(sheets[k].landscape, es["landscape"].get<bool>());
        EXPECT_NEAR(sheets[k].size.width, es["size"][0].get<float>(), 0.01f);
        ASSERT_EQ(sheets[k].placements.size(), es["placements"].size()) << "лист " << k;
        for (std::size_t i = 0; i < sheets[k].placements.size(); ++i) {
            const Placement& p = sheets[k].placements[i];
            const auto& ep = es["placements"][i];
            EXPECT_EQ(p.page, ep["page"].get<int>());
            EXPECT_EQ(p.rotation, ep["rotation"].get<int>());
            EXPECT_NEAR(p.scale, e["scale"].get<float>(), 1e-4f);
            const auto& t = ep["target"];
            expectRect(p.target, t[0], t[1], t[2], t[3]);
            expectRect(p.clip, t[0], t[1], t[2], t[3]);
        }
        EXPECT_TRUE(sheets[k].frames.empty());
    }
}

TEST(imposition, nup_order_borders_auto) {
    OT_FEATURE("view-39");
    const std::vector<SizeF> sizes(4, kA4);
    ImposeOptions o;
    o.layout = Layout::Multiple;
    o.cols = 2;
    o.rows = 2;
    o.orientation = Orientation::Portrait;
    o.borders = true;
    const Paper paper = Paper::a4();
    // Ячейки: (0, 0) левая верхняя, (1, 0) правая верхняя, (0, 1) левая нижняя.
    const auto cellOf = [&](const Placement& p) {
        const float cx = (p.target.x0 + p.target.x1) / 2, cy = (p.target.y0 + p.target.y1) / 2;
        return std::pair<int, int>{cx > kA4.width / 2 ? 1 : 0, cy > kA4.height / 2 ? 1 : 0};
    };
    const std::pair<NupOrder, std::vector<std::pair<int, int>>> cases[] = {
        {NupOrder::Horizontal, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}},
        {NupOrder::HorizontalReversed, {{1, 0}, {0, 0}, {1, 1}, {0, 1}}},
        {NupOrder::Vertical, {{0, 0}, {0, 1}, {1, 0}, {1, 1}}},
        {NupOrder::VerticalReversed, {{1, 0}, {1, 1}, {0, 0}, {0, 1}}},
    };
    for (const auto& [order, cells] : cases) {
        o.order = order;
        const auto sheets = impose(sizes, sequence(4), paper, o);
        ASSERT_EQ(sheets.size(), 1u);
        ASSERT_EQ(sheets[0].placements.size(), 4u);
        ASSERT_EQ(sheets[0].frames.size(), 4u);
        for (int i = 0; i < 4; ++i) {
            EXPECT_EQ(sheets[0].placements[i].page, i);
            EXPECT_EQ(cellOf(sheets[0].placements[i]), cells[i]) << static_cast<int>(order) << " " << i;
            expectRect(sheets[0].frames[i], sheets[0].placements[i].target.x0,
                       sheets[0].placements[i].target.y0, sheets[0].placements[i].target.x1,
                       sheets[0].placements[i].target.y1);
        }
    }

    // 2 на листе, «Авто»: книжные страницы — альбомный лист 2 × 1 без поворота, масштаб 1/√2.
    ImposeOptions two;
    two.layout = Layout::Multiple;
    ot::print::nupGrid(2, two.cols, two.rows);
    EXPECT_EQ(two.cols, 2);
    EXPECT_EQ(two.rows, 1);
    const auto sheets = impose(std::vector<SizeF>(3, kA4), sequence(3), paper, two);
    ASSERT_EQ(sheets.size(), 2u);
    EXPECT_TRUE(sheets[0].landscape);
    EXPECT_NEAR(sheets[0].size.width, kA4.height, 0.01f);
    EXPECT_EQ(sheets[0].placements[0].rotation, 0);
    EXPECT_NEAR(sheets[0].placements[0].scale, 1.0f / std::sqrt(2.0f), 1e-3f);
    EXPECT_LT(sheets[0].placements[0].target.x1, sheets[0].placements[1].target.x0 + 0.01f);
    EXPECT_EQ(sheets[1].placements.size(), 1u);

    int cols = 0, rows = 0;
    ot::print::nupGrid(6, cols, rows);
    EXPECT_EQ(std::pair(cols, rows), std::pair(3, 2));
    ot::print::nupGrid(16, cols, rows);
    EXPECT_EQ(std::pair(cols, rows), std::pair(4, 4));
}

TEST(imposition, nup_auto_rotate) {
    OT_FEATURE("view-39");
    // Альбомная страница в книжной ячейке: с автоповоротом — 90° и крупнее, без — 0°.
    const std::vector<SizeF> sizes = {kA4, kA4Landscape};
    ImposeOptions o;
    o.layout = Layout::Multiple;
    o.cols = 2;
    o.rows = 2;
    o.orientation = Orientation::Portrait;
    auto sheets = impose(sizes, {0, 1}, Paper::a4(), o);
    ASSERT_EQ(sheets[0].placements.size(), 2u);
    EXPECT_EQ(sheets[0].placements[1].rotation, 90);
    const float rotated = sheets[0].placements[1].scale;
    EXPECT_NEAR(sheets[0].placements[1].target.width(), kA4Landscape.height * rotated, 0.01f);
    o.autoRotate = false;
    sheets = impose(sizes, {0, 1}, Paper::a4(), o);
    EXPECT_EQ(sheets[0].placements[1].rotation, 0);
    EXPECT_LT(sheets[0].placements[1].scale, rotated);
}

TEST(imposition, booklet_ten_pages) {
    OT_FEATURE("view-40");
    const std::vector<SizeF> sizes(10, kA4);
    ImposeOptions o;
    o.layout = Layout::Booklet;
    const Paper paper = Paper::a4(10);
    auto sheets = impose(sizes, sequence(10), paper, o);
    // 10 → 12: три листа, у каждого лицо и оборот.
    ASSERT_EQ(sheets.size(), 6u);
    const std::vector<std::vector<int>> expected = {{12, 1}, {2, 11}, {10, 3}, {4, 9}, {8, 5}, {6, 7}};
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        std::vector<int> got = pagesOf(sheets[i]);
        // Пустые места (страницы 11 и 12) — page = -1, на своём месте.
        std::vector<int> want = expected[i];
        for (int& n : want) {
            if (n > 10) {
                n = 0;
            }
        }
        EXPECT_EQ(got, want) << "лист " << i;
        EXPECT_EQ(sheets[i].side, i % 2 == 0 ? SheetSide::Front : SheetSide::Back);
        EXPECT_TRUE(sheets[i].landscape);
    }
    // Одним масштабом, прижаты к корешку (середина листа).
    const float spine = sheets[0].size.width / 2;
    const Placement& left = sheets[2].placements[0];
    const Placement& right = sheets[2].placements[1];
    EXPECT_NEAR(left.target.x1, spine, 0.01f);
    EXPECT_NEAR(right.target.x0, spine, 0.01f);
    EXPECT_NEAR(left.scale, right.scale, 1e-5f);
    EXPECT_TRUE(inside(paper.printableFor(true), left.target));
    EXPECT_TRUE(inside(paper.printableFor(true), right.target));

    o.bookletSubset = BookletSubset::FrontOnly;
    sheets = impose(sizes, sequence(10), paper, o);
    ASSERT_EQ(sheets.size(), 3u);
    EXPECT_EQ(pagesOf(sheets[1]), (std::vector<int>{10, 3}));
    o.bookletSubset = BookletSubset::BackOnly;
    sheets = impose(sizes, sequence(10), paper, o);
    ASSERT_EQ(sheets.size(), 3u);
    EXPECT_EQ(pagesOf(sheets[0]), (std::vector<int>{6, 7}));
    EXPECT_EQ(pagesOf(sheets[1]), (std::vector<int>{4, 9}));
    EXPECT_EQ(pagesOf(sheets[2]), (std::vector<int>{2, 0}));
    EXPECT_EQ(sheets[0].side, SheetSide::Back);

    o.bookletSubset = BookletSubset::BothSides;
    o.binding = Binding::Right;
    sheets = impose(std::vector<SizeF>(8, kA4), sequence(8), paper, o);
    ASSERT_EQ(sheets.size(), 4u);
    EXPECT_EQ(pagesOf(sheets[0]), (std::vector<int>{1, 8}));
    EXPECT_EQ(pagesOf(sheets[1]), (std::vector<int>{7, 2}));
}

TEST(imposition, booklet_mixed_sizes_by_largest) {
    OT_FEATURE("view-40");
    const std::vector<SizeF> sizes = {kA5, kA4, kA5, kA5};
    ImposeOptions o;
    o.layout = Layout::Booklet;
    const auto sheets = impose(sizes, sequence(4), Paper::a4(), o);
    ASSERT_EQ(sheets.size(), 2u);
    // Масштаб общий — по наибольшей (A4 ровно в половину A4 альбомного: 1/√2).
    for (const Sheet& s : sheets) {
        for (const Placement& p : s.placements) {
            EXPECT_NEAR(p.scale, 1.0f / std::sqrt(2.0f), 1e-3f);
        }
    }
}

TEST(imposition, poster_a1_on_a4) {
    OT_FEATURE("view-41");
    const std::vector<SizeF> sizes = {kA1};
    ImposeOptions o;
    o.layout = Layout::Poster;
    o.overlap = 10 * kPointsPerMm;
    o.cutMarks = true;
    o.labels = true;
    o.title = "plan.pdf";
    const Paper paper = Paper::a4();
    // 100 %: A1 594 × 841 мм на A4 с перекрытием 10 мм — 3 × 3 книжных (альбомных было бы 3 × 5).
    auto sheets = impose(sizes, {0}, paper, o);
    ASSERT_EQ(sheets.size(), 9u);
    for (const Sheet& s : sheets) {
        EXPECT_FALSE(s.landscape);
        ASSERT_EQ(s.placements.size(), 1u);
        const Placement& p = s.placements[0];
        EXPECT_NEAR(p.scale, 1.0f, 1e-5f);
        EXPECT_NEAR(p.target.width(), kA1.width, 0.05f);
        EXPECT_TRUE(inside(paper.printableFor(false), p.clip));
        EXPECT_FALSE(p.clip.empty());
        for (const auto& m : s.marks) {
            EXPECT_TRUE(ot::rectContains(paper.printableFor(false), m.a));
            EXPECT_TRUE(ot::rectContains(paper.printableFor(false), m.b));
        }
        if (s.tileRow > 0 || s.tileCol > 0) {
            EXPECT_FALSE(s.marks.empty()) << s.tileRow << "," << s.tileCol;
        }
    }
    EXPECT_EQ(sheets[0].label, "plan.pdf (1, 1)");
    EXPECT_EQ(sheets[5].label, "plan.pdf (2, 3)");
    // Соседние плитки перекрываются ровно на 10 мм в координатах страницы.
    const Placement& a = sheets[0].placements[0];
    const Placement& b = sheets[1].placements[0];
    const float aRight = a.clip.x1 - a.target.x0;  // правый край плитки (0, 0) в пунктах страницы
    const float bLeft = b.clip.x0 - b.target.x0;   // левый край плитки (0, 1)
    EXPECT_NEAR(aRight - bLeft, 10 * kPointsPerMm, 0.05f);
    // Плитки вместе покрывают всю страницу.
    RectF covered = RectF::none();
    for (const Sheet& s : sheets) {
        const Placement& p = s.placements[0];
        covered = ot::rectUnion(covered, RectF{p.clip.x0 - p.target.x0, p.clip.y0 - p.target.y0,
                                               p.clip.x1 - p.target.x0, p.clip.y1 - p.target.y0});
    }
    expectRect(covered, 0, 0, kA1.width, kA1.height, 0.05f);

    // 96 %: 8 листов — альбомные 2 × 4 (книжных было бы 3 × 3).
    o.tileScale = 0.96f;
    sheets = impose(sizes, {0}, paper, o);
    ASSERT_EQ(sheets.size(), 8u);
    EXPECT_TRUE(sheets[0].landscape);
    EXPECT_EQ(sheets.back().tileRow, 3);
    EXPECT_EQ(sheets.back().tileCol, 1);

    // «Только большие страницы»: A4 на A4 — один лист в реальном размере.
    o.tileScale = 1.0f;
    o.tileLargeOnly = true;
    sheets = impose({kA4, kA1}, {0, 1}, Paper::a4(0), o);
    ASSERT_EQ(sheets.size(), 10u);
    EXPECT_EQ(sheets[0].tileRow, -1);
    EXPECT_NEAR(sheets[0].placements[0].scale, 1.0f, 1e-5f);
}

TEST(imposition, single_fit_shrink_actual_custom) {
    OT_FEATURE("view-38");
    const std::vector<SizeF> sizes = {kA5, kA1, kA4Landscape};
    const Paper letter = Paper::letter();
    ImposeOptions o;
    o.orientation = Orientation::Portrait;
    // «Вписать» A5 на Letter — увеличение.
    auto s = impose(sizes, {0}, letter, o);
    ASSERT_EQ(s.size(), 1u);
    EXPECT_GT(s[0].placements[0].scale, 1.0f);
    EXPECT_NEAR(s[0].placements[0].scale, 792.0f / kA5.height, 1e-4f);
    // «Уменьшить негабаритные» — A5 как есть, по центру бумаги; A1 — вписан.
    o.size = SizeMode::ShrinkOversized;
    s = impose(sizes, {0, 1}, letter, o);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_NEAR(s[0].placements[0].scale, 1.0f, 1e-6f);
    expectRect(s[0].placements[0].target, (612 - kA5.width) / 2, (792 - kA5.height) / 2,
               (612 + kA5.width) / 2, (792 + kA5.height) / 2);
    EXPECT_LT(s[1].placements[0].scale, 1.0f);
    EXPECT_TRUE(inside(letter.printableFor(false), s[1].placements[0].target));
    // «Реальный размер» A1 — лист обрезает: clip = область печати.
    o.size = SizeMode::Actual;
    s = impose(sizes, {1}, Paper::letter(18), o);
    EXPECT_NEAR(s[0].placements[0].scale, 1.0f, 1e-6f);
    expectRect(s[0].placements[0].clip, 18, 18, 612 - 18, 792 - 18);
    // Свой масштаб 50 %.
    o.size = SizeMode::Custom;
    o.customScale = 0.5f;
    s = impose(sizes, {0}, letter, o);
    EXPECT_NEAR(s[0].placements[0].target.width(), kA5.width / 2, 0.01f);
}

TEST(imposition, single_auto_orientation) {
    OT_FEATURE("view-38");
    const std::vector<SizeF> sizes = {kA4, kA4Landscape};
    ImposeOptions o;
    auto s = impose(sizes, {0, 1}, Paper::a4(), o);
    ASSERT_EQ(s.size(), 2u);
    EXPECT_FALSE(s[0].landscape);
    EXPECT_TRUE(s[1].landscape);
    EXPECT_NEAR(s[1].size.width, kA4.height, 0.01f);
    EXPECT_NEAR(s[1].placements[0].scale, 1.0f, 1e-4f);
    o.orientation = Orientation::Portrait;
    s = impose(sizes, {1}, Paper::a4(), o);
    EXPECT_FALSE(s[0].landscape);
    EXPECT_LT(s[0].placements[0].scale, 1.0f);
    EXPECT_TRUE(impose(sizes, {}, Paper::a4(), o).empty());
}

TEST(imposition, placement_matrix_rotation) {
    Placement p;
    p.rotation = 90;
    p.scale = 0.5f;
    p.target = {100, 200, 100 + 842 * 0.5f, 200 + 595 * 0.5f};
    const RectF page{0, 0, 595, 842};
    const Matrix m = ot::print::placementMatrix(p, page);
    // По часовой: левый верхний угол страницы → правый верхний угол цели, левый нижний → левый верхний.
    const PointF tl = apply(m, 0, 0);
    EXPECT_NEAR(tl.x, p.target.x1, 0.01f);
    EXPECT_NEAR(tl.y, p.target.y0, 0.01f);
    const PointF bl = apply(m, 0, 842);
    EXPECT_NEAR(bl.x, p.target.x0, 0.01f);
    EXPECT_NEAR(bl.y, p.target.y0, 0.01f);
    // Начало рамки страницы не в нуле — сдвиг учитывается.
    p.rotation = 0;
    p.target = {10, 20, 10 + 595 * 0.5f, 20 + 842 * 0.5f};
    const Matrix shifted = ot::print::placementMatrix(p, RectF{50, 60, 645, 902});
    const PointF o0 = apply(shifted, 50, 60);
    EXPECT_NEAR(o0.x, 10, 0.01f);
    EXPECT_NEAR(o0.y, 20, 0.01f);
}
