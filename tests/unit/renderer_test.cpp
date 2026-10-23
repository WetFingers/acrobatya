// unit.renderer.* — Renderer (эталон 4.2): BGRA в буфер вызывающего (QImage ARGB32_Premultiplied) и RGB,
// тайлы через scissor складываются в целую страницу, матрица с поворотом, сглаживание, страница без
// пометок, отмена через fz_cookie; путь через display list совпадает с fz_new_pixmap_from_page бит в бит.
#include <gtest/gtest.h>

#include <set>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/image_compare.h"
#include "core/render/renderer.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

using ot::Bitmap;
using ot::Document;
using ot::IRect;
using ot::Matrix;
using ot::MuContext;
using ot::PixelFormat;
using ot::Renderer;
using ot::RenderRequest;

namespace {

std::unique_ptr<Document> testDoc(int pages = 2) {
    MuContext mu;
    return Document::openOrThrowMemory(ot::test::makePdf(mu, pages));
}

// Эталон «как раньше»: fz_new_pixmap_from_page (fz_run_page) — RGB без альфы.
Bitmap referencePixmap(const Document& doc, int page, float dpi, bool annotations) {
    fz_context* ctx = doc.context().get();
    ot::mu::PageHandle p(ctx);
    ot::mu::PixmapHandle pix(ctx);
    ot::mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        const fz_matrix ctm = fz_scale(dpi / 72.0f, dpi / 72.0f);
        pix.reset(annotations ? fz_new_pixmap_from_page(ctx, p, ctm, fz_device_rgb(ctx), 0)
                              : fz_new_pixmap_from_page_contents(ctx, p, ctm, fz_device_rgb(ctx), 0));
    });
    Bitmap b;
    b.width = fz_pixmap_width(ctx, pix);
    b.height = fz_pixmap_height(ctx, pix);
    b.components = 3;
    b.stride = b.width * 3;
    b.pixels.resize(static_cast<size_t>(b.stride) * static_cast<size_t>(b.height));
    for (int y = 0; y < b.height; ++y) {
        std::memcpy(b.row(y),
                    fz_pixmap_samples(ctx, pix) + static_cast<ptrdiff_t>(y) * fz_pixmap_stride(ctx, pix),
                    static_cast<size_t>(b.stride));
    }
    return b;
}

}  // namespace

TEST(renderer, display_list_path_equals_pixmap_from_page) {
    OT_FEATURE("view-01");
    const wchar_t* files[] = {L"tests/fixtures/demo/report.pdf", L"tests/fixtures/demo/comments.pdf",
                              L"tests/data/mini/cyr-cid.pdf", L"tests/data/mini/type1-diff.pdf"};
    for (const wchar_t* rel : files) {
        SCOPED_TRACE(ot::paths::narrow(rel));
        auto doc = Document::openOrThrow(ot::test::sourceDir() / rel);
        for (bool annotations : {true, false}) {
            const Bitmap expected = referencePixmap(*doc, 0, 72.0f, annotations);
            const Bitmap actual = Renderer(*doc).renderPage(0, 72.0f, annotations);
            const auto r = ot::compareBitmaps(expected, actual);
            EXPECT_TRUE(r.sameSize);
            EXPECT_EQ(r.differingPixels, 0) << r.toJson();
        }
    }
}

TEST(renderer, bgra_tiles_compose_whole_page) {
    auto doc = testDoc();
    const Renderer renderer(*doc);
    const Matrix m = Matrix::dpi(100);
    const Bitmap whole = renderer.renderPage(1, 100);
    const IRect bounds = renderer.deviceBounds(1, m);
    ASSERT_EQ(bounds.width(), whole.width);
    ASSERT_EQ(bounds.height(), whole.height);
    const int tile = 256;
    for (int ty = bounds.y0; ty < bounds.y1; ty += tile) {
        for (int tx = bounds.x0; tx < bounds.x1; tx += tile) {
            RenderRequest req;
            req.page = 1;
            req.matrix = m;
            req.scissor = {tx, ty, std::min(tx + tile, bounds.x1), std::min(ty + tile, bounds.y1)};
            const int stride = tile * 4 + 16;  // шаг строки шире тайла — как у выровненного QImage
            std::vector<std::uint8_t> buf(static_cast<size_t>(stride) * tile, 0x11);
            renderer.render(req, buf.data(), stride, PixelFormat::Bgra32Premultiplied);
            for (int y = 0; y < req.scissor.height(); ++y) {
                for (int x = 0; x < req.scissor.width(); ++x) {
                    const std::uint8_t* px = buf.data() + static_cast<ptrdiff_t>(y) * stride + x * 4;
                    const std::uint8_t* rgb = whole.row(ty - bounds.y0 + y) + (tx - bounds.x0 + x) * 3;
                    ASSERT_EQ(px[0], rgb[2]) << "B at " << tx + x << "," << ty + y;
                    ASSERT_EQ(px[1], rgb[1]);
                    ASSERT_EQ(px[2], rgb[0]);
                    ASSERT_EQ(px[3], 255);
                }
                // хвост строки за шириной области не тронут
                ASSERT_EQ(buf[static_cast<size_t>(y) * stride + static_cast<size_t>(req.scissor.width()) * 4],
                          0x11);
            }
        }
    }
}

TEST(renderer, rotated_matrix_moves_square) {
    auto doc = testDoc(1);
    const Renderer renderer(*doc);
    // 72 dpi, поворот 90°: страница 595 × 842 → 842 × 595; квадрат (100..200, 100..200 от верха) уходит
    // вправо.
    const Matrix m = ot::concat(Matrix::dpi(72), Matrix::rotate(90));
    const IRect bounds = renderer.deviceBounds(0, m);
    EXPECT_EQ(bounds.width(), 842);
    EXPECT_EQ(bounds.height(), 595);
    RenderRequest req;
    req.matrix = m;
    req.scissor = bounds;
    std::vector<std::uint8_t> buf(static_cast<size_t>(bounds.width()) * bounds.height() * 3);
    renderer.render(req, buf.data(), bounds.width() * 3, PixelFormat::Rgb24);
    auto at = [&](int x, int y) { return buf[(static_cast<size_t>(y) * bounds.width() + x) * 3]; };
    EXPECT_EQ(at(842 - 150, 150), 0);    // квадрат
    EXPECT_EQ(at(150, 150), 255);        // там, где он был без поворота
    EXPECT_EQ(at(842 - 150, 400), 255);  // ниже квадрата
}

TEST(renderer, antialiasing_level_is_per_request) {
    auto doc = testDoc(2);
    const Renderer renderer(*doc);
    auto grays = [](const Bitmap& b) {
        std::set<int> values;
        for (size_t i = 0; i < b.pixels.size(); i += 3) {
            values.insert(b.pixels[i]);
        }
        return values;
    };
    const auto hard = grays(renderer.renderPage(1, 72, true, 0));
    EXPECT_EQ(hard, (std::set<int>{0, 255}));  // без сглаживания — только чёрное и белое
    const auto smooth = grays(renderer.renderPage(1, 72, true, 8));
    EXPECT_GT(smooth.size(), 2u);
    EXPECT_EQ(fz_aa_level(doc->context().get()), fz_aa_level(MuContext::global().get()));  // восстановлен
}

TEST(renderer, page_without_annotations_differs) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / L"tests/fixtures/demo/comments.pdf");
    const Renderer renderer(*doc);
    const Bitmap with = renderer.renderPage(0, 72, true);
    const Bitmap without = renderer.renderPage(0, 72, false);
    EXPECT_GT(ot::compareBitmaps(with, without).differingPixels, 100);
}

TEST(renderer, cookie_abort_throws_aborted) {
    auto doc = testDoc(1);
    const Renderer renderer(*doc);
    RenderRequest req;
    req.matrix = Matrix::dpi(72);
    req.scissor = renderer.deviceBounds(0, req.matrix);
    std::vector<std::uint8_t> buf(static_cast<size_t>(req.scissor.width()) * req.scissor.height() * 4);
    fz_cookie cookie{};
    cookie.abort = 1;
    try {
        renderer.render(req, buf.data(), req.scissor.width() * 4, PixelFormat::Bgra32Premultiplied, &cookie);
        FAIL() << "no exception";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ot::ErrorKind::Aborted);
    }
}

TEST(renderer, bad_buffer_arguments_throw) {
    auto doc = testDoc(1);
    const Renderer renderer(*doc);
    RenderRequest req;
    req.scissor = {0, 0, 10, 10};
    std::vector<std::uint8_t> buf(400);
    EXPECT_THROW(renderer.render(req, buf.data(), 39), std::invalid_argument);  // нужно ≥ 40 байт на строку
    EXPECT_THROW(renderer.render(req, nullptr, 40), std::invalid_argument);
    EXPECT_NO_THROW(renderer.render(req, buf.data(), 30, PixelFormat::Rgb24));  // RGB: 10 px = 30 байт
}
