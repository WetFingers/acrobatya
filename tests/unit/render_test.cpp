// unit.render.* — растр страницы: размеры при dpi, чёрный квадрат на своём месте, белый фон, PNG туда и
// обратно.
#include <gtest/gtest.h>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Bitmap;
using ot::Document;
using ot::MuContext;

namespace {

int at(const Bitmap& b, int x, int y) { return b.row(y)[x * b.components]; }

}  // namespace

TEST(render, page_size_follows_dpi) {
    OT_FEATURE("view-01");
    MuContext mu;
    auto doc = Document::openMemory(mu, ot::test::makePdf(mu));
    const Bitmap b72 = doc->renderPage(0, {72.0f, true});
    EXPECT_EQ(b72.width, 595);
    EXPECT_EQ(b72.height, 842);
    EXPECT_EQ(b72.components, 3);
    const Bitmap b144 = doc->renderPage(0, {144.0f, true});
    EXPECT_EQ(b144.width, 1190);
    EXPECT_EQ(b144.height, 1684);
}

TEST(render, black_square_where_drawn_and_white_background) {
    MuContext mu;
    auto doc = Document::openMemory(mu, ot::test::makePdf(mu));
    const Bitmap b = doc->renderPage(0, {72.0f, true});
    EXPECT_EQ(at(b, 150, 150), 0);    // внутри квадрата 100..200 × 100..200 (от верха)
    EXPECT_EQ(at(b, 50, 50), 255);    // поле
    EXPECT_EQ(at(b, 250, 150), 255);  // правее квадрата
    EXPECT_EQ(at(b, 150, 250), 255);  // ниже квадрата
}

TEST(render, png_roundtrip_is_lossless) {
    MuContext mu;
    auto doc = Document::openMemory(mu, ot::test::makePdf(mu));
    const Bitmap b = doc->renderPage(0, {50.0f, true});
    ot::test::TempDir dir;
    const auto file = dir.path() / L"страница.png";
    ot::savePng(mu, b, file);
    const Bitmap back = ot::loadPng(mu, file);
    const auto r = ot::compareBitmaps(b, back);
    EXPECT_TRUE(r.sameSize);
    EXPECT_EQ(r.maxDelta, 0);
    EXPECT_EQ(r.differingPixels, 0);
}
