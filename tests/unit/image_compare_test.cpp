// unit.image_compare.* — метрики imgcmp (эталон 9.3): одинаковые, сдвиг на 1, разные размеры, порог.
#include <gtest/gtest.h>

#include "core/render/image_compare.h"

using ot::Bitmap;
using ot::compareBitmaps;

namespace {

Bitmap solid(int w, int h, std::uint8_t v) {
    Bitmap b;
    b.width = w;
    b.height = h;
    b.components = 3;
    b.stride = w * 3;
    b.pixels.assign(static_cast<size_t>(b.stride) * static_cast<size_t>(h), v);
    return b;
}

}  // namespace

TEST(image_compare, identical_images_have_zero_metrics) {
    const Bitmap a = solid(32, 32, 200);
    const auto r = compareBitmaps(a, a);
    EXPECT_TRUE(r.sameSize);
    EXPECT_EQ(r.differingPixels, 0);
    EXPECT_EQ(r.maxDelta, 0);
    EXPECT_DOUBLE_EQ(r.mse, 0.0);
    EXPECT_NEAR(r.ssim, 1.0, 1e-9);
    EXPECT_TRUE(r.within({}));
}

TEST(image_compare, small_noise_within_default_threshold) {
    const Bitmap a = solid(64, 64, 200);
    Bitmap b = a;
    b.row(10)[30] = 201;  // один канал одного пикселя на 1
    const auto r = compareBitmaps(a, b);
    EXPECT_EQ(r.differingPixels, 1);
    EXPECT_EQ(r.maxDelta, 1);
    EXPECT_TRUE(r.within({}));
}

TEST(image_compare, big_block_change_fails) {
    const Bitmap a = solid(64, 64, 255);
    Bitmap b = a;
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16 * 3; ++x) {
            b.row(y)[x] = 0;
        }
    }
    Bitmap diff;
    const auto r = compareBitmaps(a, b, &diff);
    EXPECT_EQ(r.differingPixels, 256);
    EXPECT_EQ(r.maxDelta, 255);
    EXPECT_FALSE(r.within({}));
    EXPECT_EQ(diff.width, 64);
    EXPECT_EQ(diff.row(0)[0], 255);
    EXPECT_EQ(diff.row(40)[40 * 3], 0);
}

TEST(image_compare, different_sizes_fail) {
    const auto r = compareBitmaps(solid(10, 10, 0), solid(11, 10, 0));
    EXPECT_FALSE(r.sameSize);
    EXPECT_FALSE(r.within({}));
    EXPECT_NE(r.toJson().find("\"same_size\":false"), std::string::npos);
}
