// unit.render.tiles_*, unit.render.cancel_*, unit.render.lru_* — тайлы, рендер-пул и кеши (эталон 4.1–4.2):
// одна матрица scale × rotate × translate, тайлы 512 × 512 по целым device px; 20+ тайлов одной страницы,
// нарисованных параллельно в RenderPool, дают те же пиксели, что один рендер целиком (pixels == 0, в том
// числе с картинками, поворотом и dpr 1,25); отмена через fz_cookie снимает очередь и прерывает идущие;
// DisplayCache — LRU с инвалидацией страницы; TileCache (QCache) не превышает лимит в байтах.
#include <gtest/gtest.h>

#include <QElapsedTimer>
#include <QImage>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/render/display_cache.h"
#include "core/render/image_compare.h"
#include "core/render/renderer.h"
#include "core/render/tiles.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/render_pool.h"
#include "services/task_context.h"
#include "services/tile_cache.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

using ot::DisplayCache;
using ot::DisplayList;
using ot::Document;
using ot::ErrorKind;
using ot::IRect;
using ot::Matrix;
using ot::MuContext;
using ot::RectF;
using ot::RenderPool;
using ot::TileKey;
using ot::TileRect;
using ot::TileRequest;
using ot::TileResult;

namespace fs = std::filesystem;

namespace {

const RectF kA4{0, 0, 595, 842};

std::unique_ptr<Document> mixedDoc() {
    MuContext mu;
    return Document::openOrThrowMemory(ot::test::makeMixedPdf(mu));
}

std::unique_ptr<Document> heavyDoc() {
    MuContext mu;
    return Document::openOrThrowMemory(ot::test::makeHeavyPdf(mu));
}

// Вся страница одним рендером в главном потоке (свой клон контекста) — BGRA.
QImage wholePage(const DisplayList& list, const Matrix& m, const IRect& device) {
    QImage img(device.width(), device.height(), QImage::Format_ARGB32_Premultiplied);
    ot::renderDisplayList(MuContext::forThisThread().get(), list, m, device, img.bits(),
                          static_cast<int>(img.bytesPerLine()));
    return img;
}

// Число отличающихся пикселей тайла и того же места целого растра.
long long diffPixels(const QImage& whole, const TileResult& tile) {
    long long diff = 0;
    const IRect& r = tile.rect;
    for (int y = 0; y < r.height(); ++y) {
        const auto* a = reinterpret_cast<const quint32*>(tile.image.constScanLine(y));
        const auto* b = reinterpret_cast<const quint32*>(whole.constScanLine(r.y0 + y)) + r.x0;
        for (int x = 0; x < r.width(); ++x) {
            diff += a[x] != b[x] ? 1 : 0;
        }
    }
    return diff;
}

// Собрать результаты пула (DirectConnection: сигнал из потоков пула).
struct Collector {
    explicit Collector(RenderPool& pool) {
        QObject::connect(
            &pool, &RenderPool::tileReady, &pool,
            [this](const TileResult& r) {
                const std::lock_guard lock(m);
                results.push_back(r);
            },
            Qt::DirectConnection);
    }
    size_t count() {
        const std::lock_guard lock(m);
        return results.size();
    }
    std::mutex m;
    std::vector<TileResult> results;
};

}  // namespace

TEST(render, tiles_cover_page_on_integer_device_pixels) {
    // 100 %, dpr 1: A4 = 595 × 842 → 2 × 2 тайла.
    const auto t = ot::tileRectsFor(kA4, 1.0f, 1.0f);
    ASSERT_EQ(t.size(), 4u);
    EXPECT_EQ(t[0], (TileRect{0, 0, {0, 0, 512, 512}}));
    EXPECT_EQ(t[1], (TileRect{1, 0, {512, 0, 595, 512}}));
    EXPECT_EQ(t[2], (TileRect{0, 1, {0, 512, 512, 842}}));
    EXPECT_EQ(t[3], (TileRect{1, 1, {512, 512, 595, 842}}));
    // 150 % × dpr 1,25, поворот 90°: рамка 1579 × 1116 (как fz_round_rect), тайлы без зазоров и наложений.
    auto doc = mixedDoc();
    const ot::Renderer renderer(*doc);
    for (const int rot : {0, 90, 180, 270}) {
        const Matrix m = ot::pageMatrix(kA4, 1.5f, 1.25f, rot);
        const IRect device = ot::pageDeviceRect(kA4, 1.5f, 1.25f, rot);
        EXPECT_EQ(device, renderer.deviceBounds(0, m)) << rot;
        EXPECT_EQ(device.x0, 0);
        EXPECT_EQ(device.y0, 0);
        EXPECT_EQ(rot % 180 == 0 ? device.width() : device.height(), 1116) << rot;
        EXPECT_EQ(rot % 180 == 0 ? device.height() : device.width(), 1579) << rot;
        const auto tiles = ot::tileRectsFor(kA4, 1.5f, 1.25f, rot);
        long long area = 0;
        for (const auto& tile : tiles) {
            EXPECT_EQ(tile.rect.x0, tile.tx * ot::kTileSize);
            EXPECT_EQ(tile.rect.y0, tile.ty * ot::kTileSize);
            EXPECT_LE(tile.rect.width(), ot::kTileSize);
            EXPECT_LE(tile.rect.x1, device.x1);
            EXPECT_LE(tile.rect.y1, device.y1);
            EXPECT_FALSE(tile.rect.empty());
            area += static_cast<long long>(tile.rect.width()) * tile.rect.height();
        }
        EXPECT_EQ(area, static_cast<long long>(device.width()) * device.height());
        EXPECT_EQ(tiles.size(),
                  static_cast<size_t>(((device.width() + 511) / 512) * ((device.height() + 511) / 512)));
    }
    EXPECT_TRUE(ot::tileRectsFor(RectF{0, 0, 0, 0}, 1, 1).empty());
}

TEST(render, tiles_one_matrix_scale_rotate_translate) {
    auto apply = [](const Matrix& m, float x, float y) {
        return std::pair<float, float>{x * m.a + y * m.c + m.e, x * m.b + y * m.d + m.f};
    };
    // Без поворота: масштаб zoom · dpr, начало страницы — (0, 0).
    const Matrix m0 = ot::pageMatrix(kA4, 2.0f, 1.5f, 0);
    EXPECT_FLOAT_EQ(m0.a, 3.0f);
    EXPECT_FLOAT_EQ(m0.d, 3.0f);
    EXPECT_EQ(apply(m0, 100, 100), (std::pair<float, float>{300, 300}));
    // 90°: (x, y) → (H·s − y·s, x·s); левый верхний угол страницы уходит вправо.
    const Matrix m90 = ot::pageMatrix(kA4, 1.0f, 1.0f, 90);
    EXPECT_EQ(apply(m90, 0, 0), (std::pair<float, float>{842, 0}));
    EXPECT_EQ(apply(m90, 100, 200), (std::pair<float, float>{642, 100}));
    // Рамка со смещённым началом (CropBox) тоже начинается в (0, 0) устройства.
    const RectF shifted{36.5f, 20, 631.5f, 862};
    const Matrix ms = ot::pageMatrix(shifted, 1.0f, 2.0f, 0);
    EXPECT_EQ(apply(ms, 36.5f, 20), (std::pair<float, float>{0, 0}));
    EXPECT_EQ(ot::pageDeviceRect(shifted, 1.0f, 2.0f), (IRect{0, 0, 1190, 1684}));
    // Масштаб и dpr — в тысячных (как в ключе).
    EXPECT_EQ(ot::pageMatrix(kA4, 1.0004f, 1.0f).a, 1.0f);
}

TEST(render, tiles_key_bucket_and_hash) {
    EXPECT_EQ(ot::zoomBucket(1.0f), 0);
    EXPECT_EQ(ot::zoomBucket(2.0f), 8);
    EXPECT_EQ(ot::zoomBucket(0.5f), -8);
    EXPECT_EQ(ot::zoomBucket(1.25f), 3);  // 8 · log2(1,25) = 2,58
    EXPECT_FLOAT_EQ(ot::bucketZoom(8), 2.0f);
    EXPECT_EQ(ot::toMilli(1.25f), 1250);
    EXPECT_EQ(ot::toMilli(0.0f), 1);
    EXPECT_EQ(ot::normalizeRotation(-90), 270);
    EXPECT_EQ(ot::normalizeRotation(450), 90);
    const TileRect tile{3, 4, {1536, 2048, 2048, 2560}};
    const TileKey a = ot::makeTileKey(7, 2, 1.5f, 1.25f, -90, tile, 5);
    EXPECT_EQ(a.doc, 7u);
    EXPECT_EQ(a.page, 2);
    EXPECT_EQ(a.zoomMilli, 1500);
    EXPECT_EQ(a.dprMilli, 1250);
    EXPECT_EQ(a.zoomBucket, 5);
    EXPECT_EQ(a.rotation, 270);
    EXPECT_EQ(a.tx, 3);
    EXPECT_EQ(a.ty, 4);
    EXPECT_EQ(a.revision, 5u);
    const TileKey b = ot::makeTileKey(7, 2, 1.5f, 1.25f, 270, tile, 5);
    EXPECT_EQ(a, b);
    EXPECT_EQ(ot::hashValue(a), ot::hashValue(b));
    EXPECT_EQ(ot::qHash(a), ot::qHash(b));
    TileKey c = a;
    c.revision = 6;
    EXPECT_NE(a, c);
    std::set<std::size_t> hashes;
    for (int i = 0; i < 64; ++i) {
        TileKey k = a;
        k.tx = i % 8;
        k.ty = i / 8;
        hashes.insert(ot::hashValue(k));
    }
    EXPECT_EQ(hashes.size(), 64u);
}

TEST(render, tiles_visible_subset) {
    const IRect device{0, 0, 1300, 1700};
    const auto v = ot::tilesIntersecting(device, IRect{500, 0, 600, 100});
    ASSERT_EQ(v.size(), 2u);
    EXPECT_EQ(v[0], (TileRect{0, 0, {0, 0, 512, 512}}));
    EXPECT_EQ(v[1], (TileRect{1, 0, {512, 0, 1024, 512}}));
    EXPECT_TRUE(ot::tilesIntersecting(device, IRect{2000, 2000, 2100, 2100}).empty());
    EXPECT_EQ(ot::tilesIntersecting(device, IRect{-100, -100, 5000, 5000}).size(), 12u);
}

namespace {

struct MosaicCase {
    float zoom, dpr;
    int rotation;
};

// Тайлы страницы page документа doc параллельно в пуле (8 потоков); tileSize 256 — у каждого случая ≥ 20
// тайлов.
std::vector<TileResult> poolTiles(const DisplayList& list, const std::shared_ptr<const DisplayList>& shared,
                                  const MosaicCase& c) {
    const Matrix m = ot::pageMatrix(list.bounds(), c.zoom, c.dpr, c.rotation);
    const auto tiles = ot::tileRectsFor(list.bounds(), c.zoom, c.dpr, c.rotation, 256);
    EXPECT_GE(tiles.size(), 20u);
    RenderPool pool(8);
    Collector got(pool);
    for (const auto& t : tiles) {
        TileRequest req;
        req.key = ot::makeTileKey(1, 0, c.zoom, c.dpr, c.rotation, t);
        req.list = shared;
        req.matrix = m;
        req.rect = t.rect;
        req.dpr = c.dpr;
        (void)pool.submit(req);
    }
    EXPECT_TRUE(pool.waitForDone(30000));
    EXPECT_EQ(got.count(), tiles.size());
    for (const auto& r : got.results) {
        EXPECT_TRUE(r.ok()) << r.error.message.toStdString();
        EXPECT_EQ(r.image.format(), QImage::Format_ARGB32_Premultiplied);
        EXPECT_EQ(r.image.width(), r.rect.width());
        EXPECT_EQ(r.image.height(), r.rect.height());
        EXPECT_DOUBLE_EQ(r.image.devicePixelRatio(), c.dpr);
    }
    return std::move(got.results);
}

// BGRA (QImage) → RGB Bitmap для compareBitmaps.
ot::Bitmap toBitmap(const QImage& img) {
    ot::Bitmap b;
    b.width = img.width();
    b.height = img.height();
    b.stride = b.width * 3;
    b.pixels.resize(static_cast<size_t>(b.stride) * b.height);
    for (int y = 0; y < b.height; ++y) {
        const auto* src = reinterpret_cast<const quint32*>(img.constScanLine(y));
        std::uint8_t* dst = b.row(y);
        for (int x = 0; x < b.width; ++x) {
            dst[x * 3 + 0] = static_cast<std::uint8_t>(qRed(src[x]));
            dst[x * 3 + 1] = static_cast<std::uint8_t>(qGreen(src[x]));
            dst[x * 3 + 2] = static_cast<std::uint8_t>(qBlue(src[x]));
        }
    }
    return b;
}

QImage assemble(const IRect& device, const std::vector<TileResult>& tiles) {
    QImage out(device.width(), device.height(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::magenta);  // непокрытое место заметно
    for (const auto& r : tiles) {
        for (int y = 0; y < r.rect.height(); ++y) {
            std::memcpy(out.scanLine(r.rect.y0 + y) + static_cast<ptrdiff_t>(r.rect.x0) * 4,
                        r.image.constScanLine(y), static_cast<size_t>(r.rect.width()) * 4);
        }
    }
    return out;
}

const MosaicCase kMosaicCases[] = {{2.5f, 1.5f, 0}, {2.0f, 1.25f, 90}, {1.33f, 1.25f, 0}, {1.5f, 1.0f, 270}};
// Для метрик (SSIM по всей странице в Debug — секунды) — без самого крупного растра.
const MosaicCase kMetricCases[] = {{2.0f, 1.25f, 90}, {1.33f, 1.25f, 0}, {1.5f, 1.0f, 270}};

}  // namespace

TEST(render, tiles_mosaic_text_and_rects_equals_whole_page_exactly) {
    OT_FEATURE("view-01");
    OT_FEATURE("view-03");
    ot::test::qtApp();
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makeMixedPdf(mu, ot::test::MixedContent::TextAndRects));
    const auto list = DisplayList::build(*doc, 0);
    for (const MosaicCase& c : kMosaicCases) {
        const IRect device = ot::pageDeviceRect(list->bounds(), c.zoom, c.dpr, c.rotation);
        const QImage whole =
            wholePage(*list, ot::pageMatrix(list->bounds(), c.zoom, c.dpr, c.rotation), device);
        long long total = 0;
        for (const auto& r : poolTiles(*list, list, c)) {
            total += diffPixels(whole, r);
        }
        EXPECT_EQ(total, 0) << "zoom " << c.zoom << " dpr " << c.dpr << " rot " << c.rotation;
    }
}

namespace {

// Мозаика из пула против целого рендера: метрики imgcmp и доля отличающихся пикселей.
std::pair<ot::CompareResult, double> mosaicVsWhole(ot::test::MixedContent content, const MosaicCase& c) {
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makeMixedPdf(mu, content));
    const auto list = DisplayList::build(*doc, 0);
    const IRect device = ot::pageDeviceRect(list->bounds(), c.zoom, c.dpr, c.rotation);
    const QImage whole = wholePage(*list, ot::pageMatrix(list->bounds(), c.zoom, c.dpr, c.rotation), device);
    const QImage mosaic = assemble(device, poolTiles(*list, list, c));
    const ot::CompareResult cmp = ot::compareBitmaps(toBitmap(whole), toBitmap(mosaic));
    const double share =
        static_cast<double>(cmp.differingPixels) / (static_cast<double>(device.width()) * device.height());
    std::printf("mosaic %d zoom %.2f dpr %.2f rot %d: %s, share %.5f\n", static_cast<int>(content), c.zoom,
                c.dpr, c.rotation, cmp.toJson().c_str(), share);
    return {cmp, share};
}

}  // namespace

// Кривые и масштабированные картинки MuPDF рисует с отсечением по тайлу: GEL обрезает рёбра по scissor с
// целочисленной интерполяцией (сглаживание на рёбрах — доли пикселя), аффинная отрисовка картинки считает шаг
// от начала отсечения (граница тексела сдвигается на 1 px). Швов нет; границы — строгие.
TEST(render, tiles_mosaic_curves_match_whole_page_within_edge_rounding) {
    ot::test::qtApp();
    for (const MosaicCase& c : kMetricCases) {
        const auto [cmp, share] = mosaicVsWhole(ot::test::MixedContent::Curves, c);
        EXPECT_TRUE(cmp.sameSize);
        EXPECT_LE(cmp.maxDelta, 24);  // одна подстрока сглаживания из 15
        EXPECT_LT(share, 0.002);
        EXPECT_LE(cmp.mse, 0.05);
        EXPECT_GE(cmp.ssim, 0.9999);
    }
}

TEST(render, tiles_mosaic_scaled_images_match_whole_page_within_one_texel_shift) {
    ot::test::qtApp();
    for (const MosaicCase& c : kMetricCases) {
        // Картинка с полосами шириной 7 и 5 текселов — худший случай для сдвига границы тексела.
        const auto [cmp, share] = mosaicVsWhole(ot::test::MixedContent::Full, c);
        EXPECT_TRUE(cmp.sameSize);
        EXPECT_LT(share, 0.005);
        EXPECT_LE(cmp.mse, 10.0);
        EXPECT_GE(cmp.ssim, 0.999);
    }
}

TEST(render, tiles_parallel_equals_sequential_exactly) {
    // Детерминизм пула: те же тайлы, нарисованные в 8 потоках одновременно и по одному в главном потоке,
    // совпадают бит в бит (клоны контекста не мешают друг другу).
    ot::test::qtApp();
    auto doc = mixedDoc();
    const auto list = DisplayList::build(*doc, 0);
    const MosaicCase c{2.0f, 1.25f, 90};
    const Matrix m = ot::pageMatrix(list->bounds(), c.zoom, c.dpr, c.rotation);
    long long total = 0;
    for (const auto& r : poolTiles(*list, list, c)) {
        const QImage alone = wholePage(*list, m, r.rect);  // тот же тайл тем же scissor
        for (int y = 0; y < r.rect.height(); ++y) {
            total += std::memcmp(alone.constScanLine(y), r.image.constScanLine(y),
                                 static_cast<size_t>(r.rect.width()) * 4)
                         ? 1
                         : 0;
        }
    }
    EXPECT_EQ(total, 0);
}

TEST(render, tiles_callback_arrives_in_context_thread) {
    ot::test::qtApp();
    auto doc = mixedDoc();
    const auto list = DisplayList::build(*doc, 0);
    RenderPool pool(2);
    QObject receiver;
    std::vector<std::thread::id> threads;
    const auto tiles = ot::tileRectsFor(list->bounds(), 1.0f, 1.0f);
    for (const auto& t : tiles) {
        TileRequest req;
        req.key = ot::makeTileKey(1, 0, 1.0f, 1.0f, 0, t);
        req.list = list;
        req.matrix = ot::pageMatrix(list->bounds(), 1.0f, 1.0f);
        req.rect = t.rect;
        (void)pool.submit(req, &receiver, [&](const TileResult& r) {
            EXPECT_TRUE(r.ok());
            threads.push_back(std::this_thread::get_id());
        });
    }
    ASSERT_TRUE(ot::test::waitUntil([&] { return threads.size() == tiles.size(); }));
    for (const auto& id : threads) {
        EXPECT_EQ(id, std::this_thread::get_id());
    }
}

TEST(render, cancel_pool_stops_running_and_queued_tiles) {
    OT_FEATURE("view-01");
    ot::test::qtApp();
    auto doc = heavyDoc();
    const auto list = DisplayList::build(*doc, 0);
    const Matrix m = ot::pageMatrix(list->bounds(), 600.0f / 72.0f, 1.0f);
    const auto tiles = ot::tileRectsFor(list->bounds(), 600.0f / 72.0f, 1.0f);
    ASSERT_GT(tiles.size(), 100u);
    RenderPool pool(4);
    Collector got(pool);
    for (const auto& t : tiles) {
        TileRequest req;
        req.key = ot::makeTileKey(1, 0, 600.0f / 72.0f, 1.0f, 0, t);
        req.list = list;
        req.matrix = m;
        req.rect = t.rect;
        (void)pool.submit(req);
    }
    QThread::msleep(100);
    const size_t before = got.count();
    ASSERT_LT(before, tiles.size()) << "рендер кончился до отмены";
    QElapsedTimer timer;
    timer.start();
    pool.cancelDocument(1);
    ASSERT_TRUE(pool.waitForDone(5000));
    const qint64 ms = timer.elapsed();
    std::printf("pool cancel latency: %lld ms, done before cancel %zu of %zu\n", static_cast<long long>(ms),
                before, tiles.size());
    EXPECT_LT(ms, 200);
    ASSERT_EQ(got.count(), tiles.size());  // результат приходит у каждой задачи
    size_t aborted = 0;
    for (const auto& r : got.results) {
        if (r.aborted) {
            ++aborted;
            EXPECT_TRUE(r.image.isNull());
            EXPECT_EQ(r.error.kind, ErrorKind::Aborted);
        } else {
            EXPECT_TRUE(r.ok());
        }
    }
    EXPECT_GE(aborted, tiles.size() - before - 4);  // всё, что не успело, — отменено
    EXPECT_EQ(pool.pending(), 0);
}

TEST(render, cancel_retarget_keeps_visible_and_aborts_the_rest) {
    ot::test::qtApp();
    auto doc = heavyDoc();
    const auto list = DisplayList::build(*doc, 0);
    const float zoom = 600.0f / 72.0f;
    const Matrix m = ot::pageMatrix(list->bounds(), zoom, 1.0f);
    const auto tiles = ot::tileRectsFor(list->bounds(), zoom, 1.0f);
    RenderPool pool(1);
    Collector got(pool);
    for (const auto& t : tiles) {
        TileRequest req;
        req.key = ot::makeTileKey(1, 0, zoom, 1.0f, 0, t);
        req.list = list;
        req.matrix = m;
        req.rect = t.rect;
        (void)pool.submit(req);
    }
    // «Видим» только первый тайл последнего ряда: он дорисовывается, остальное отменяется.
    const int lastRow = tiles.back().ty;
    auto visible = [&](const TileKey& k) { return k.ty == lastRow && k.tx == 0; };
    pool.retarget(1, visible, 10);
    ASSERT_TRUE(pool.waitForDone(30000));
    ASSERT_EQ(got.count(), tiles.size());
    size_t visibleOk = 0;
    size_t invisibleOk = 0;
    for (const auto& r : got.results) {
        if (visible(r.key)) {
            EXPECT_TRUE(r.ok());
            visibleOk += r.ok() ? 1 : 0;
        } else {
            EXPECT_TRUE(r.aborted || r.ok());
            invisibleOk += r.ok() ? 1 : 0;
        }
    }
    EXPECT_EQ(visibleOk, 1u);
    EXPECT_LE(invisibleOk, 1u);  // мог успеть только тот, что уже шёл
}

TEST(render, cancel_inside_clip_is_aborted_not_error) {
    // Отмена посреди списка, когда на стеке устройства clip: устройство не закрывается (close бросил бы
    // «items left on stack», FZ_ERROR_ARGUMENT) — ошибка рендера только Aborted.
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makeHeavyPdf(mu, 4000, true));
    const auto list = DisplayList::build(*doc, 0);
    const float zoom = 600.0f / 72.0f;
    const IRect device = ot::pageDeviceRect(list->bounds(), zoom, 1.0f);
    std::vector<std::uint8_t> buf(static_cast<size_t>(device.width()) * device.height() * 4);
    fz_cookie cookie{};
    std::thread killer([&cookie] {
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        cookie.abort = 1;
    });
    try {
        ot::renderDisplayList(MuContext::forThisThread().get(), *list,
                              ot::pageMatrix(list->bounds(), zoom, 1.0f), device, buf.data(),
                              device.width() * 4, ot::PixelFormat::Bgra32Premultiplied, &cookie);
        ADD_FAILURE() << "render finished before abort";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ErrorKind::Aborted) << e.what();
    }
    killer.join();
}

TEST(render, cancel_display_list_build_is_not_cached) {
    auto doc = heavyDoc();
    DisplayCache cache;
    fz_cookie cookie{};
    cookie.abort = 1;
    try {
        (void)cache.get(*doc, 0, true, &cookie);
        FAIL() << "no abort";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ErrorKind::Aborted);
    }
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_NE(cache.get(*doc, 0), nullptr);
    EXPECT_EQ(cache.size(), 1u);
}

TEST(render, lru_display_cache_evicts_oldest) {
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(mu, 6));
    DisplayCache cache(3);
    const auto l0 = cache.get(*doc, 0);
    (void)cache.get(*doc, 1);
    (void)cache.get(*doc, 2);
    EXPECT_EQ(cache.size(), 3u);
    EXPECT_EQ(cache.misses(), 3u);
    EXPECT_EQ(cache.get(*doc, 0), l0);  // из кеша, стал свежим
    EXPECT_EQ(cache.hits(), 1u);
    (void)cache.get(*doc, 3);  // вытесняет самый давний — страницу 1
    EXPECT_EQ(cache.size(), 3u);
    EXPECT_EQ(cache.find(1), nullptr);
    EXPECT_NE(cache.find(0), nullptr);
    EXPECT_NE(cache.find(2), nullptr);
    EXPECT_NE(cache.find(3), nullptr);
    // Вариант без пометок — отдельный ключ.
    const auto plain = cache.get(*doc, 3, false);
    EXPECT_NE(plain, cache.find(3, true));
    EXPECT_FALSE(plain->annotations());
    EXPECT_EQ(cache.find(2), nullptr);  // самая давняя вытеснена вариантом без пометок
    EXPECT_NE(cache.find(0), nullptr);
    // Список, который держат снаружи, живёт после вытеснения.
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(l0->page(), 0);
    EXPECT_FLOAT_EQ(l0->bounds().width(), 595.0f);
    cache.setCapacity(DisplayCache::kDefaultCapacity);
    for (int p = 0; p < 6; ++p) {
        (void)cache.get(*doc, p);
    }
    EXPECT_EQ(cache.size(), 6u);
    cache.setCapacity(2);
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_NE(cache.find(5), nullptr);
    EXPECT_NE(cache.find(4), nullptr);
}

TEST(render, lru_display_cache_invalidate_page) {
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(mu, 3));
    DisplayCache cache;
    const auto a = cache.get(*doc, 1);
    (void)cache.get(*doc, 1, false);
    (void)cache.get(*doc, 2);
    cache.invalidate(1);
    EXPECT_EQ(cache.find(1, true), nullptr);
    EXPECT_EQ(cache.find(1, false), nullptr);
    EXPECT_NE(cache.find(2), nullptr);
    EXPECT_NE(cache.get(*doc, 1), a);  // построен заново
}

TEST(render, lru_tile_cache_respects_byte_limit) {
    OT_FEATURE("view-03");
    ot::TileCache cache(8LL * 1024 * 1024);  // 8 МБ: тайлам 7/8 = 7 тайлов 512 × 512 BGRA, превью 1/8
    QImage tile(512, 512, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::white);
    for (int i = 0; i < 10; ++i) {
        TileKey k;
        k.tx = i;
        QImage copy = tile.copy();
        copy.setPixel(0, 0, qRgb(i, 0, 0));
        EXPECT_TRUE(cache.insert(k, copy));
        EXPECT_LE(cache.totalBytes(), cache.maxBytes());
    }
    EXPECT_EQ(cache.count(), 7);
    EXPECT_EQ(cache.totalBytes(), 7LL * 1024 * 1024);
    TileKey k;
    k.tx = 0;
    EXPECT_FALSE(cache.find(k).has_value());  // вытеснен
    k.tx = 9;
    ASSERT_TRUE(cache.find(k).has_value());
    EXPECT_EQ(qRed(cache.find(k)->pixel(0, 0)), 9);
    // Больше лимита — не кладётся; пустой — тоже.
    EXPECT_FALSE(cache.insert(TileKey{}, QImage(2048, 1024, QImage::Format_ARGB32_Premultiplied)));
    EXPECT_FALSE(cache.insert(TileKey{}, QImage()));
    // Сбросы по документу и странице, уменьшение лимита.
    TileKey other;
    other.doc = 2;
    other.page = 3;
    EXPECT_TRUE(cache.insert(other, tile));
    EXPECT_EQ(cache.removePage(2, 3), 1);
    EXPECT_EQ(cache.removeDocument(0), 6);
    EXPECT_EQ(cache.count(), 0);
    EXPECT_EQ(cache.totalBytes(), 0);
    EXPECT_EQ(ot::TileCache().maxBytes(), 384LL * 1024 * 1024);
}

TEST(render, tiles_session_display_list_to_pool_and_cache) {
    ot::test::qtApp();
    MuContext mu;
    ot::test::TempDir dir;
    const fs::path file = dir.path() / L"смешанный.pdf";
    ot::paths::writeFile(file, ot::test::makeMixedPdf(mu));
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(file.wstring())));
    const auto list = session.displayList(0).result();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(session.displayList(0).result(), list);  // из кеша сессии
    RenderPool pool(4);
    ot::TileCache cache;
    QObject receiver;
    int done = 0;
    const auto tiles = ot::tileRectsFor(list->bounds(), 1.0f, 1.0f);
    for (const auto& t : tiles) {
        TileRequest req;
        req.key = ot::makeTileKey(session.id(), 0, 1.0f, 1.0f, 0, t, session.revision());
        req.list = list;
        req.matrix = ot::pageMatrix(list->bounds(), 1.0f, 1.0f);
        req.rect = t.rect;
        (void)pool.submit(req, &receiver, [&](const TileResult& r) {
            EXPECT_TRUE(cache.insert(r.key, r.image));
            ++done;
        });
    }
    ASSERT_TRUE(ot::test::waitUntil([&] { return done == static_cast<int>(tiles.size()); }));
    EXPECT_EQ(cache.count(), static_cast<int>(tiles.size()));
    // Правка страницы сбрасывает её display list в кеше сессии.
    session.edit(QStringLiteral("touch"), [](ot::TaskContext& tc) { tc.touchPage(0); }).waitForFinished();
    const auto after = session.displayList(0).result();
    EXPECT_NE(after, list);
    EXPECT_EQ(session.revision(), 1u);
    session.close();
    // Список переживает закрытие документа: пул дорисовывает то, что уже взял.
    QImage img = wholePage(*after, ot::pageMatrix(after->bounds(), 0.25f, 1.0f),
                           ot::pageDeviceRect(after->bounds(), 0.25f, 1.0f));
    EXPECT_FALSE(img.isNull());
}
