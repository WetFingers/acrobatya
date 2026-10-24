// unit.recolor.* — замена цветов документа (план 2.5b; эталон 3.16 «Замена цветов / ночной режим»; view-26,
// ocr-38): recolor_device перекрашивает текст и векторы по яркости между цветами «текст» и «фон» (чёрное —
// в цвет текста, белое — в цвет фона), бумага — цветом фона, картинки не трогаются; «Изменять только чёрное»
// и «без векторной графики» оставляют цветные заливки; «Инвертировать всё» — по растру, картинки тоже.
// Сравнение — по областям страницы makeMixedPdf(Full): поле, картинка Im1, строка текста, красный
// прямоугольник. Ключ стиля различает все поля (TileKey).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/display_cache.h"
#include "core/render/recolor_device.h"
#include "core/render/renderer.h"
#include "core/render/tiles.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::IRect;
using ot::Matrix;
using ot::PixelFormat;
using ot::Recolor;
using ot::RecolorMode;
using ot::RenderRequest;
using ot::RenderStyle;

namespace {

struct Image {
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgb;
    const std::uint8_t* at(int x, int y) const { return rgb.data() + (static_cast<size_t>(y) * w + x) * 3; }
    int luma(int x, int y) const {
        const std::uint8_t* p = at(x, y);
        return (299 * p[0] + 587 * p[1] + 114 * p[2]) / 1000;
    }
};

std::unique_ptr<Document> mixed() {
    return Document::openOrThrowMemory(
        ot::test::makeMixedPdf(ot::MuContext::global(), ot::test::MixedContent::Full));
}

Image render(const Document& doc, const RenderStyle& style) {
    RenderRequest req;
    req.page = 0;
    req.matrix = Matrix::dpi(72);
    req.style = style;
    const ot::Renderer r(doc);
    Image img;
    const IRect area = r.deviceBounds(0, req.matrix);
    img.w = area.width();
    img.h = area.height();
    img.rgb.resize(static_cast<size_t>(img.w) * img.h * 3);
    r.render(req, img.rgb.data(), img.w * 3, PixelFormat::Rgb24);
    return img;
}

RenderStyle recolored(RecolorMode mode, std::uint32_t text, std::uint32_t background) {
    RenderStyle s;
    s.recolor.mode = mode;
    s.recolor.text = text;
    s.recolor.background = background;
    return s;
}

// Наибольшая разница каналов в прямоугольнике [x0, x1) × [y0, y1).
int maxDiff(const Image& a, const Image& b, int x0, int y0, int x1, int y1, bool inverted = false) {
    int worst = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            for (int c = 0; c < 3; ++c) {
                const int va = a.at(x, y)[c];
                const int vb = inverted ? 255 - b.at(x, y)[c] : b.at(x, y)[c];
                worst = std::max(worst, std::abs(va - vb));
            }
        }
    }
    return worst;
}

// Области страницы A4 при 72 dpi (y вниз): поле слева сверху, картинка Im1 (50…350, 52…292), строка
// «Mosaic tiles: Helvetica 18» (базовая линия y = 342), красный прямоугольник (60…280, 502…542).
constexpr int kMarginX = 15, kMarginY = 15;
constexpr int kImX0 = 80, kImY0 = 80, kImX1 = 320, kImY1 = 260;
constexpr int kTextX0 = 60, kTextY0 = 326, kTextX1 = 270, kTextY1 = 344;
constexpr int kRedX = 170, kRedY = 520;

}  // namespace

TEST(recolor, luminance_maps_between_text_and_background) {
    Recolor rc;
    rc.mode = RecolorMode::Replace;
    rc.text = 0xFFFFFF;
    rc.background = 0x000000;
    float out[3];
    const float black[3] = {0, 0, 0};
    ot::recolorRgb(rc, black, out);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
    const float white[3] = {1, 1, 1};
    ot::recolorRgb(rc, white, out);
    EXPECT_FLOAT_EQ(out[2], 0.0f);
    const float grey[3] = {0.5f, 0.5f, 0.5f};
    ot::recolorRgb(rc, grey, out);
    EXPECT_NEAR(out[1], 0.5f, 1e-4);
    // «Только чёрное»: цветное остаётся, чёрное — цвет текста, белое — цвет фона.
    rc.onlyBlack = true;
    const float red[3] = {0.8f, 0.1f, 0.1f};
    ot::recolorRgb(rc, red, out);
    EXPECT_FLOAT_EQ(out[0], 0.8f);
    EXPECT_FLOAT_EQ(out[1], 0.1f);
    ot::recolorRgb(rc, black, out);
    EXPECT_FLOAT_EQ(out[0], 1.0f);
}

TEST(recolor, white_on_black_darkens_background_keeps_images) {
    OT_FEATURE("view-26");
    OT_FEATURE("ocr-38");
    auto doc = mixed();
    const Image normal = render(*doc, {});
    const Image night = render(*doc, recolored(RecolorMode::Replace, 0xFFFFFF, 0x000000));
    ASSERT_EQ(normal.w, night.w);
    // Поле: белое → чёрное.
    EXPECT_GT(normal.luma(kMarginX, kMarginY), 250);
    EXPECT_LT(night.luma(kMarginX, kMarginY), 5);
    // Картинка — как была.
    EXPECT_LE(maxDiff(normal, night, kImX0, kImY0, kImX1, kImY1), 1);
    // Текст: тёмные пиксели строки стали светлыми.
    int dark = 0, flipped = 0;
    for (int y = kTextY0; y < kTextY1; ++y) {
        for (int x = kTextX0; x < kTextX1; ++x) {
            if (normal.luma(x, y) < 100) {
                ++dark;
                flipped += night.luma(x, y) > 155 ? 1 : 0;
            }
        }
    }
    ASSERT_GT(dark, 100);
    EXPECT_GE(flipped * 10, dark * 9);
    // Красная заливка (яркость ≈ 0,3) — серая ≈ 0,7: без цвета.
    const std::uint8_t* red = night.at(kRedX, kRedY);
    EXPECT_LT(std::abs(red[0] - red[2]), 6);
    EXPECT_NEAR(red[0], 178, 12);
}

TEST(recolor, only_black_and_without_line_art_keep_colored_fills) {
    auto doc = mixed();
    const Image normal = render(*doc, {});
    RenderStyle onlyBlack = recolored(RecolorMode::Replace, 0xFFFF00, 0x000000);
    onlyBlack.recolor.onlyBlack = true;
    const Image a = render(*doc, onlyBlack);
    EXPECT_LE(maxDiff(normal, a, kRedX - 5, kRedY - 5, kRedX + 5, kRedY + 5), 1);
    EXPECT_LT(a.luma(kMarginX, kMarginY), 5);
    RenderStyle text = recolored(RecolorMode::Replace, 0x00FF00, 0x000000);
    text.recolor.lineArt = false;
    const Image b = render(*doc, text);
    EXPECT_LE(maxDiff(normal, b, kRedX - 5, kRedY - 5, kRedX + 5, kRedY + 5), 1);
    EXPECT_LE(maxDiff(normal, b, kImX0, kImY0, kImX1, kImY1), 1);
}

TEST(recolor, invert_all_inverts_images_too) {
    auto doc = mixed();
    const Image normal = render(*doc, {});
    RenderStyle s;
    s.recolor.mode = RecolorMode::InvertAll;
    const Image inv = render(*doc, s);
    EXPECT_LE(maxDiff(normal, inv, 0, 0, normal.w, normal.h, true), 1);
}

TEST(recolor, tiles_of_recolored_page_match_whole_render) {
    auto doc = mixed();
    const auto list = ot::DisplayList::build(*doc, 0);
    const RenderStyle style = recolored(RecolorMode::Replace, 0xE0E0E0, 0x202020);
    const float zoom = 1.0f;
    const Matrix m = ot::pageMatrix(list->bounds(), zoom, 1.0f);
    const IRect whole = ot::pageDeviceRect(list->bounds(), zoom, 1.0f);
    std::vector<std::uint8_t> full(static_cast<size_t>(whole.width()) * whole.height() * 4);
    ot::renderDisplayList(ot::MuContext::forThisThread().get(), *list, m, whole, full.data(),
                          whole.width() * 4, PixelFormat::Bgra32Premultiplied, nullptr, style);
    for (const ot::TileRect& t : ot::tileRectsFor(list->bounds(), zoom, 1.0f, 0, 256)) {
        std::vector<std::uint8_t> tile(static_cast<size_t>(t.rect.width()) * t.rect.height() * 4);
        ot::renderDisplayList(ot::MuContext::forThisThread().get(), *list, m, t.rect, tile.data(),
                              t.rect.width() * 4, PixelFormat::Bgra32Premultiplied, nullptr, style);
        // Поле тайла — цвет фона, как и у целой страницы (тайл не белый в углу).
        const std::uint8_t* corner = tile.data();
        const std::uint8_t* same =
            full.data() + (static_cast<size_t>(t.rect.y0) * whole.width() + t.rect.x0) * 4;
        EXPECT_NEAR(corner[0], same[0], 2) << t.tx << "," << t.ty;
        EXPECT_NEAR(corner[1], same[1], 2) << t.tx << "," << t.ty;
    }
}

TEST(recolor, style_key_distinguishes_fields) {
    const RenderStyle base;
    std::vector<RenderStyle> variants(8, base);
    variants[0].recolor.mode = RecolorMode::Replace;
    variants[1] = variants[0];
    variants[1].recolor.text = 0x123456;
    variants[2] = variants[0];
    variants[2].recolor.background = 0x654321;
    variants[3].recolor.mode = RecolorMode::InvertAll;
    variants[4].textAa = 0;
    variants[5].graphicsAa = 2;
    variants[6].smoothImages = false;
    variants[7].overprint = true;
    std::vector<std::uint64_t> keys{base.key()};
    for (const RenderStyle& v : variants) {
        keys.push_back(v.key());
    }
    RenderStyle thin;
    thin.thinLines = true;
    keys.push_back(thin.key());
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(std::unique(keys.begin(), keys.end()), keys.end());
    EXPECT_EQ(base.key(), RenderStyle{}.key());
    // Ключ тайла носит стиль: другой стиль — другой растр.
    const ot::TileRect tile{0, 0, IRect{0, 0, 512, 512}};
    EXPECT_FALSE(ot::makeTileKey(1, 0, 1.0f, 1.0f, 0, tile, 0, 0, base.key()) ==
                 ot::makeTileKey(1, 0, 1.0f, 1.0f, 0, tile, 0, 0, variants[0].key()));
}
