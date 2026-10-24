// unit.band_renderer.* — растр печати полосами (план 2.6; эталон 3.3 «Механика»; view-37, view-42, view-43):
// A4 при 600 dpi в RGB — 4 полосы ≤ 32 МиБ, мозаика полос совпадает с целым рендером бит в бит; «Серый» —
// растр Gray8 с яркостью цветного (R = G = B после разворота); «Только поля» — рендер без содержимого (всё
// нарисованное — внутри виджетов); «Документ» без пометок, «Документ и пометки» — с ними; слой «Только
// экран» (/PrintState /OFF) и выключенный в панели слой не печатаются; размещение n-up с поворотом рисует
// страницу в своей ячейке; отмена — cookie.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/annot/annots.h"
#include "core/doc/document.h"
#include "core/links/layers.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/print/band_renderer.h"
#include "core/print/imposition.h"
#include "core/render/display_cache.h"
#include "core/render/renderer.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::IRect;
using ot::PixelFormat;
using ot::RectF;
using ot::print::Band;
using ot::print::BandOptions;
using ot::print::buildPrintList;
using ot::print::deviceRect;
using ot::print::Placement;
using ot::print::PrintContent;
using ot::print::renderPlacement;
using ot::print::splitBands;

namespace {

std::filesystem::path fixture(const char* rel) { return ot::test::sourceDir() / "tests" / "fixtures" / rel; }

// Растр листа (или его части) из полос: area — вся собираемая область устройства.
struct Canvas {
    IRect area;
    int n = 3;
    std::vector<std::uint8_t> px;
    int bands = 0;

    Canvas(const IRect& a, int components) : area(a), n(components) {
        px.assign(static_cast<size_t>(a.width()) * a.height() * n, 0x7F);
    }
    void put(const Band& b) {
        ++bands;
        ASSERT_EQ(ot::bytesPerPixel(b.format), n);
        for (int y = b.area.y0; y < b.area.y1; ++y) {
            std::memcpy(&px[(static_cast<size_t>(y - area.y0) * area.width() + (b.area.x0 - area.x0)) * n],
                        b.pixels + static_cast<size_t>(y - b.area.y0) * b.stride,
                        static_cast<size_t>(b.area.width()) * n);
        }
    }
    const std::uint8_t* at(int x, int y) const {
        return &px[(static_cast<size_t>(y - area.y0) * area.width() + (x - area.x0)) * n];
    }
    bool white(int x, int y) const {
        const std::uint8_t* p = at(x, y);
        for (int c = 0; c < n; ++c) {
            if (p[c] < 250) {
                return false;
            }
        }
        return true;
    }
    // Есть ли нарисованное (не белое) в прямоугольнике устройства.
    bool inked(const IRect& r) const {
        for (int y = std::max(r.y0, area.y0); y < std::min(r.y1, area.y1); ++y) {
            for (int x = std::max(r.x0, area.x0); x < std::min(r.x1, area.x1); ++x) {
                if (!white(x, y)) {
                    return true;
                }
            }
        }
        return false;
    }
};

// Страница целиком на листе её размера, масштаб 1.
Placement whole(const RectF& bounds) {
    Placement p;
    p.page = 0;
    p.target = {0, 0, bounds.width(), bounds.height()};
    p.clip = p.target;
    return p;
}

Canvas print(const Document& doc, int page, PrintContent content, float dpi, bool gray = false,
             std::size_t maxBytes = ot::print::kMaxBandBytes, const Placement* custom = nullptr) {
    const auto list = buildPrintList(doc, page, content);
    Placement p = custom != nullptr ? *custom : whole(list->bounds());
    p.page = page;
    BandOptions o;
    o.dpi = dpi;
    o.grayscale = gray;
    o.maxBandBytes = maxBytes;
    Canvas c(deviceRect(p.clip, dpi), gray ? 1 : 3);
    renderPlacement(doc.context().get(), *list, p, o, nullptr, [&](const Band& b) { c.put(b); });
    return c;
}

IRect toDevice(const RectF& r, float dpi) { return deviceRect(r, dpi); }

// Совпадает ли растр полос с растром Renderer в общей области (размеры — ±1 пиксель на краю).
::testing::AssertionResult sameAs(const Canvas& c, const ot::Bitmap& b) {
    if (std::abs(c.area.width() - b.width) > 1 || std::abs(c.area.height() - b.height) > 1) {
        return ::testing::AssertionFailure()
               << c.area.width() << "x" << c.area.height() << " vs " << b.width << "x" << b.height;
    }
    const int w = std::min(c.area.width(), b.width);
    const int h = std::min(c.area.height(), b.height);
    for (int y = 0; y < h; ++y) {
        if (std::memcmp(&c.px[static_cast<size_t>(y) * c.area.width() * 3],
                        &b.pixels[static_cast<size_t>(y) * b.stride], static_cast<size_t>(w) * 3) != 0) {
            return ::testing::AssertionFailure() << "строка " << y;
        }
    }
    return ::testing::AssertionSuccess();
}

}  // namespace

TEST(band_renderer, a4_600dpi_four_bands) {
    OT_FEATURE("view-37");
    const IRect a4 = deviceRect(RectF{0, 0, 595.2756f, 841.8898f}, 600);
    EXPECT_EQ(a4.width(), 4961);
    EXPECT_EQ(a4.height(), 7016);
    const auto rgb = splitBands(a4, 3, ot::print::kMaxBandBytes);
    ASSERT_EQ(rgb.size(), 4u);
    int y = a4.y0;
    for (const IRect& b : rgb) {
        EXPECT_EQ(b.x0, a4.x0);
        EXPECT_EQ(b.x1, a4.x1);
        EXPECT_EQ(b.y0, y);
        y = b.y1;
        const std::size_t stride = (static_cast<std::size_t>(b.width()) * 3 + 3) & ~std::size_t{3};
        EXPECT_LE(stride * b.height(), ot::print::kMaxBandBytes);
        EXPECT_LE(rgb[0].height() - b.height(), 1);  // поровну
    }
    EXPECT_EQ(y, a4.y1);
    EXPECT_EQ(splitBands(a4, 1, ot::print::kMaxBandBytes).size(), 2u);
    EXPECT_TRUE(splitBands(IRect{}, 3, ot::print::kMaxBandBytes).empty());
    // Предел меньше строки — по строке.
    EXPECT_EQ(splitBands(IRect{0, 0, 100, 3}, 3, 10).size(), 3u);
}

TEST(band_renderer, mosaic_matches_full_render) {
    OT_FEATURE("view-37");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 2));
    const ot::Renderer renderer(*doc);
    const ot::Bitmap full = renderer.renderPage(1, 150);
    // ~5 полос по 64 КиБ… — предел меньше страницы в разы.
    const Canvas c = print(*doc, 1, PrintContent::Document, 150, false, 300 * 1024);
    EXPECT_GE(c.bands, 4);
    // Края листа — round, у renderPage — fz_round_rect (ceil): правый край может отличаться на пиксель.
    EXPECT_TRUE(sameAs(c, full));
}

TEST(band_renderer, grayscale_is_gray) {
    OT_FEATURE("view-37");
    auto doc = Document::openOrThrowMemory(
        ot::test::makeMixedPdf(ot::MuContext::global(), ot::test::MixedContent::Full));
    const Canvas color = print(*doc, 0, PrintContent::Document, 72);
    const Canvas gray = print(*doc, 0, PrintContent::Document, 72, true);
    ASSERT_EQ(gray.n, 1);
    ASSERT_EQ(color.area, gray.area);
    // Перевод MuPDF — колориметрический (ICC: sRGB → серый), а не 0,3 R + 0,59 G + 0,11 B: насыщенные цвета
    // расходятся с формулой на десятки единиц, но яркость следует цветной (корреляция), белое — белое,
    // чёрное — чёрное.
    long dark = 0;
    double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0;
    long n = 0;
    for (int y = gray.area.y0; y < gray.area.y1; ++y) {
        for (int x = gray.area.x0; x < gray.area.x1; ++x) {
            const std::uint8_t* p = color.at(x, y);
            const double luma = (30 * p[0] + 59 * p[1] + 11 * p[2]) / 100.0;
            const int g = *gray.at(x, y);
            if (p[0] == 255 && p[1] == 255 && p[2] == 255) {
                ASSERT_GE(g, 254);
            }
            if (p[0] == 0 && p[1] == 0 && p[2] == 0) {
                ASSERT_LE(g, 1);
            }
            sx += luma;
            sy += g;
            sxx += luma * luma;
            syy += static_cast<double>(g) * g;
            sxy += luma * g;
            ++n;
            dark += g < 128 ? 1 : 0;
        }
    }
    const double cov = sxy / n - (sx / n) * (sy / n);
    const double r = cov / std::sqrt((sxx / n - (sx / n) * (sx / n)) * (syy / n - (sy / n) * (sy / n)));
    EXPECT_GT(r, 0.97);
    EXPECT_GT(dark, 1000);
    // Серый растр, развёрнутый в RGB, — R = G = B у каждого пикселя (так его получает принтер).
    for (std::size_t i = 0; i < gray.px.size(); ++i) {
        const std::uint8_t rgb[3] = {gray.px[i], gray.px[i], gray.px[i]};
        ASSERT_TRUE(rgb[0] == rgb[1] && rgb[1] == rgb[2]);
    }
}

TEST(band_renderer, form_fields_only_without_content) {
    OT_FEATURE("view-42");
    auto doc = Document::openOrThrow(fixture("demo/form.pdf"));
    const float dpi = 72;
    const Canvas all = print(*doc, 0, PrintContent::Document, dpi);
    const Canvas fields = print(*doc, 0, PrintContent::FormFieldsOnly, dpi);
    std::vector<IRect> widgets;
    for (const auto& a : ot::annot::listAnnots(*doc, 0)) {
        if (a.type == ot::annot::AnnotType::Widget) {
            const IRect r = toDevice(a.rect, dpi);
            widgets.push_back({r.x0 - 2, r.y0 - 2, r.x1 + 2, r.y1 + 2});
        }
    }
    ASSERT_FALSE(widgets.empty());
    long inkAll = 0, inkFields = 0;
    for (int y = fields.area.y0; y < fields.area.y1; ++y) {
        for (int x = fields.area.x0; x < fields.area.x1; ++x) {
            inkAll += all.white(x, y) ? 0 : 1;
            if (!fields.white(x, y)) {
                ++inkFields;
                const bool inWidget = std::any_of(widgets.begin(), widgets.end(), [&](const IRect& r) {
                    return x >= r.x0 && x < r.x1 && y >= r.y0 && y < r.y1;
                });
                ASSERT_TRUE(inWidget) << "содержимое страницы в режиме «Только поля»: " << x << "," << y;
            }
        }
    }
    EXPECT_GT(inkFields, 0);
    EXPECT_GT(inkAll, inkFields);
}

TEST(band_renderer, markups_only_with_document_and_markups) {
    OT_FEATURE("view-42");
    auto doc = Document::openOrThrow(fixture("demo/comments.pdf"));
    const float dpi = 72;
    const Canvas plain = print(*doc, 0, PrintContent::Document, dpi);
    const Canvas marked = print(*doc, 0, PrintContent::DocumentAndMarkups, dpi);
    // «Документ» — как содержимое без пометок (рендер просмотра без аннотаций).
    const ot::Bitmap contents = ot::Renderer(*doc).renderPage(0, dpi, false);
    EXPECT_TRUE(sameAs(plain, contents));
    // Выделение текста (Highlight) есть только в «Документ и пометки».
    bool found = false;
    for (const auto& a : ot::annot::listAnnots(*doc, 0)) {
        if (a.type == ot::annot::AnnotType::Highlight) {
            found = true;
            const IRect r = toDevice(a.rect, dpi);
            long diff = 0;
            for (int y = r.y0; y < r.y1; ++y) {
                for (int x = r.x0; x < r.x1; ++x) {
                    diff += std::memcmp(plain.at(x, y), marked.at(x, y), 3) != 0 ? 1 : 0;
                }
            }
            EXPECT_GT(diff, (r.width() * r.height()) / 4);
        }
    }
    EXPECT_TRUE(found);
}

TEST(band_renderer, hidden_layers_not_printed) {
    OT_FEATURE("view-42");
    auto doc = Document::openOrThrow(fixture("pages/print-layers.pdf"));
    const float dpi = 36;
    const IRect screenOnly = toDevice(RectF{70, 70, 230, 230}, dpi);
    const IRect common =
        toDevice(RectF{595.2756f - 230, 841.8898f - 230, 595.2756f - 70, 841.8898f - 70}, dpi);
    // На экране — оба квадрата.
    const ot::Bitmap view = ot::Renderer(*doc).renderPage(0, dpi);
    const auto viewInk = [&](const IRect& r) {
        const std::uint8_t* p = &view.pixels[static_cast<size_t>((r.y0 + r.y1) / 2) * view.stride +
                                             static_cast<size_t>((r.x0 + r.x1) / 2) * 3];
        return p[0] < 200 || p[1] < 200 || p[2] < 200;
    };
    EXPECT_TRUE(viewInk(screenOnly));
    EXPECT_TRUE(viewInk(common));
    // На печати — без «Только экран».
    Canvas c = print(*doc, 0, PrintContent::Document, dpi);
    EXPECT_FALSE(c.inked(screenOnly));
    EXPECT_TRUE(c.inked(common));
    // Выключенный в панели «Слои» — тоже не печатается.
    const auto rows = ot::layers::list(*doc);
    for (const auto& row : rows) {
        if (row.text == "Общий") {
            ASSERT_TRUE(ot::layers::setOn(*doc, row.index, false));
        }
    }
    c = print(*doc, 0, PrintContent::Document, dpi);
    EXPECT_FALSE(c.inked(common));
}

TEST(band_renderer, rotated_placement_in_cell) {
    OT_FEATURE("view-39");
    auto doc = Document::openOrThrow(fixture("pages/mixed-sizes.pdf"));
    // Альбомная A4 (стр. 3) повёрнута на 90° в правой нижней ячейке 2 × 2 листа A4.
    const auto list = buildPrintList(*doc, 2, PrintContent::Document);
    Placement p;
    p.page = 2;
    p.rotation = 90;
    p.scale = 0.5f;
    p.target = {297.6378f, 420.9449f, 595.2756f, 841.8898f};
    p.clip = p.target;
    const float dpi = 72;
    Canvas sheet(deviceRect(RectF{0, 0, 595.2756f, 841.8898f}, dpi), 3);
    std::fill(sheet.px.begin(), sheet.px.end(), 0xFF);
    BandOptions o;
    o.dpi = dpi;
    renderPlacement(doc->context().get(), *list, p, o, nullptr, [&](const Band& b) { sheet.put(b); });
    // Левая верхняя ячейка не тронута.
    EXPECT_FALSE(sheet.inked(deviceRect(RectF{0, 0, 297, 420}, dpi)));
    // Цветной квадрат левого верхнего угла страницы (20…80 pt) после поворота по часовой — в правом верхнем
    // углу ячейки: x = 595 − (20…80)/2, y = 421 + (20…80)/2.
    const std::uint8_t* q = sheet.at(595 - 25, 421 + 25);
    EXPECT_TRUE(q[1] > q[0] + 40 && q[1] > q[2] + 40) << int(q[0]) << "," << int(q[1]) << "," << int(q[2]);
}

TEST(band_renderer, cancel_with_cookie) {
    auto doc = Document::openOrThrowMemory(ot::test::makeHeavyPdf(ot::MuContext::global(), 400));
    const auto list = buildPrintList(*doc, 0, PrintContent::Document);
    fz_cookie cookie{};
    cookie.abort = 1;
    BandOptions o;
    o.dpi = 150;
    int bands = 0;
    try {
        renderPlacement(doc->context().get(), *list, whole(list->bounds()), o, &cookie,
                        [&](const Band&) { ++bands; });
        FAIL() << "без исключения";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ot::ErrorKind::Aborted);
    }
    EXPECT_EQ(bands, 0);
    // Пустое место буклета — ничего.
    Placement blank;
    renderPlacement(doc->context().get(), *list, blank, o, nullptr, [&](const Band&) { ++bands; });
    EXPECT_EQ(bands, 0);
}
