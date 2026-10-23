// unit.imgcmp.* — метрики imgcmp (эталон 9.3) против заранее посчитанных примеров (tests/fixtures/imgcmp,
// оракул numpy в tools/gen/make_imgcmp.py — определения pdfium_diff --metrics) и режим --cross для сравнения
// с другим движком: в области оба растра непустые, SSIM по серому ≥ 0.8, IoU масок «не фон» ≥ 0.7.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/render/renderer.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

using nlohmann::json;
using ot::Bitmap;
using ot::IRect;

namespace {

std::filesystem::path fixtures() { return ot::test::sourceDir() / L"tests" / L"fixtures" / L"imgcmp"; }

Bitmap whiteBitmap(int w, int h) {
    Bitmap b;
    b.width = w;
    b.height = h;
    b.components = 3;
    b.stride = w * 3;
    b.pixels.assign(static_cast<size_t>(b.stride) * static_cast<size_t>(h), 255);
    return b;
}

// Страница makePdf (чёрный квадрат 100..200 pt от левого верхнего угла, текст на второй) при 72 dpi.
Bitmap testPage(int aa) {
    ot::MuContext mu;
    auto doc = ot::Document::openOrThrowMemory(ot::test::makePdf(mu, 2));
    return ot::Renderer(*doc).renderPage(1, 72, true, aa);
}

Bitmap shifted(const Bitmap& src, int dx) {
    Bitmap out = whiteBitmap(src.width, src.height);
    for (int y = 0; y < src.height; ++y) {
        for (int x = 0; x < src.width; ++x) {
            if (x + dx >= 0 && x + dx < src.width) {
                std::memcpy(out.row(y) + (x + dx) * 3, src.row(y) + x * 3, 3);
            }
        }
    }
    return out;
}

}  // namespace

TEST(imgcmp, matches_precomputed_examples) {
    const json expected = json::parse(ot::paths::readFile(fixtures() / L"expected.json"));
    ot::MuContext mu;
    ASSERT_EQ(expected["pairs"].size(), 3u);
    for (const auto& [name, pair] : expected["pairs"].items()) {
        SCOPED_TRACE(name);
        const Bitmap a = ot::loadPng(mu, fixtures() / ot::paths::fromUtf8(pair["a"].get<std::string>()));
        const Bitmap b = ot::loadPng(mu, fixtures() / ot::paths::fromUtf8(pair["b"].get<std::string>()));
        const json& m = pair["metrics"];
        const ot::CompareResult r = ot::compareBitmaps(a, b);
        EXPECT_EQ(r.sameSize, m["same_size"].get<bool>());
        EXPECT_EQ(r.width, m["width"].get<int>());
        EXPECT_EQ(r.height, m["height"].get<int>());
        EXPECT_EQ(r.differingPixels, m["differing_pixels"].get<long long>());
        EXPECT_EQ(r.maxDelta, m["max_delta"].get<int>());
        EXPECT_NEAR(r.mse, m["mse"].get<double>(), 1e-9 * (1 + m["mse"].get<double>()));
        EXPECT_NEAR(r.windowedMse, m["wmse"].get<double>(), 1e-9 * (1 + m["wmse"].get<double>()));
        EXPECT_NEAR(r.ssim, m["ssim"].get<double>(), 1e-9);
        EXPECT_EQ(r.within({}), m["within_default"].get<bool>());
    }
}

TEST(imgcmp, cross) {
    // Тот же рисунок с другим сглаживанием (как pdfium против MuPDF) — проходит.
    const Bitmap a = testPage(8);
    const Bitmap b = testPage(2);
    const ot::CrossResult r = ot::crossCompare(a, b);
    EXPECT_TRUE(r.ok) << r.toJson();
    EXPECT_GE(r.ssim, 0.8);
    EXPECT_GE(r.iou, 0.7);
    EXPECT_GE(r.inkA, 50);
}

TEST(imgcmp, cross_rejects_empty_and_shifted) {
    const Bitmap a = testPage(8);
    const ot::CrossResult blank = ot::crossCompare(a, whiteBitmap(a.width, a.height));
    EXPECT_FALSE(blank.ok);
    EXPECT_EQ(blank.inkB, 0);
    const ot::CrossResult moved = ot::crossCompare(a, shifted(a, 60));
    EXPECT_FALSE(moved.ok) << moved.toJson();
    EXPECT_LT(moved.iou, 0.7);
}

TEST(imgcmp, cross_regions_and_size_tolerance) {
    const Bitmap a = testPage(8);
    const Bitmap b = testPage(4);
    // Область квадрата: обе стороны непустые.
    EXPECT_TRUE(ot::crossCompare(a, b, IRect{90, 90, 210, 210}).ok);
    // Пустое поле страницы: «оба непустые» не выполнено.
    const ot::CrossResult margin = ot::crossCompare(a, b, IRect{400, 700, 500, 800});
    EXPECT_FALSE(margin.ok);
    EXPECT_EQ(margin.inkA, 0);
    // Размер на 1 px больше — допустим, на 3 px — нет.
    EXPECT_TRUE(ot::crossCompare(a, ot::cropBitmap(b, {0, 0, b.width - 1, b.height})).sizeOk);
    EXPECT_FALSE(ot::crossCompare(a, ot::cropBitmap(b, {0, 0, b.width - 3, b.height})).ok);
}

TEST(imgcmp, crop_clips_to_bitmap) {
    const Bitmap a = testPage(8);
    const Bitmap part = ot::cropBitmap(a, {100, 100, 200, 120});
    EXPECT_EQ(part.width, 100);
    EXPECT_EQ(part.height, 20);
    EXPECT_EQ(part.row(5)[30], a.row(105)[(100 + 10) * 3]);
    EXPECT_TRUE(ot::cropBitmap(a, {a.width + 1, 0, a.width + 5, 10}).empty());
    EXPECT_EQ(ot::cropBitmap(a, {-10, -10, 5, 5}).width, 5);
}
