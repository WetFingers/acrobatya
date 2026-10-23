// unit.render.fit_*, unit.render.memory_*, unit.render.preview_* — масштаб вида и память
// (эталон 4.1–4.2, 6.1): «реальный размер» по DPI монитора, по странице / ширине / высоте / видимому (bbox
// содержимого), проценты 1…6400 %, раскладка пустых и огромных страниц (Letter, 200 × 200 дюймов); превью 1/8
// — одно на 1/8 октавы, отдельный кеш; кеш 64 МБ не превышается на 200-страничном файле с картинками; «мало
// памяти» сжимает store MuPDF и кеш тайлов.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QEvent>
#include <QImage>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <mutex>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/display_cache.h"
#include "core/render/fit.h"
#include "core/render/tiles.h"
#include "core/util/features.h"
#include "services/memory_guard.h"
#include "services/render_pool.h"
#include "services/tile_cache.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

using ot::DisplayCache;
using ot::DisplayList;
using ot::Document;
using ot::FitMode;
using ot::MuContext;
using ot::RectF;
using ot::Viewport;

namespace {

const RectF kA4{0, 0, 595, 842};

}  // namespace

TEST(render, fit_actual_size_follows_monitor_dpi) {
    OT_FEATURE("view-04");
    // 100 % = dpi / 72 логических пикселей на пункт; системные 96 dpi и физические 141 dpi (15,6" 1920×1080).
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, {800, 600, 96}, FitMode::ActualSize), 96.0f / 72.0f);
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, {800, 600, 141.21f}, FitMode::ActualSize), 141.21f / 72.0f);
    EXPECT_FLOAT_EQ(ot::zoomToPercent(96.0f / 72.0f, 96), 100.0f);
    EXPECT_FLOAT_EQ(ot::percentToZoom(125.0f, 96), 125.0f / 100.0f * 96.0f / 72.0f);
    EXPECT_FLOAT_EQ(ot::percentToZoom(100.0f, 144), 2.0f);
    // Пределы 1 %…6400 %.
    EXPECT_FLOAT_EQ(ot::clampZoom(0.0001f, 96), ot::percentToZoom(1, 96));
    EXPECT_FLOAT_EQ(ot::clampZoom(1000.0f, 96), ot::percentToZoom(6400, 96));
    EXPECT_FLOAT_EQ(ot::clampZoom(1.5f, 96), 1.5f);
}

TEST(render, fit_page_width_height_with_rotation) {
    OT_FEATURE("view-04");
    const Viewport vp{1000, 700, 96};
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Width), 1000.0f / 595.0f);
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Height), 700.0f / 842.0f);
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Page), 700.0f / 842.0f);  // выше, чем шире
    // Поворот вида 90°: страница лежит — 842 × 595.
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Width, 90), 1000.0f / 842.0f);
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Height, 270), 700.0f / 595.0f);
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Page, 90), std::min(1000.0f / 842.0f, 700.0f / 595.0f));
    // Пустое окно — 100 %.
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, {0, 0, 96}, FitMode::Page), 96.0f / 72.0f);
    // Узкое окно и огромная страница — не меньше 1 %.
    EXPECT_FLOAT_EQ(ot::fitZoom(RectF{0, 0, 14400, 14400}, {10, 10, 96}, FitMode::Page),
                    ot::percentToZoom(1.0f, 96));
}

TEST(render, fit_visible_uses_content_bbox) {
    OT_FEATURE("view-04");
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(mu, 1));
    const auto list = DisplayList::build(*doc, 0);
    // Страница: чёрный квадрат 100..200 × 100..200 pt от левого верхнего угла и «Page 1» (24 pt) с базовой
    // линией в 342 pt от верха — содержимое 100..200 по ширине.
    const RectF content = list->contentBounds();
    EXPECT_NEAR(content.x0, 100.0f, 0.01f);
    EXPECT_NEAR(content.y0, 100.0f, 0.01f);
    EXPECT_NEAR(content.x1, 200.0f, 0.01f);
    EXPECT_GT(content.y1, 342.0f);
    EXPECT_LT(content.y1, 352.0f);
    const Viewport vp{800, 600, 96};
    EXPECT_FLOAT_EQ(ot::fitZoom(list->bounds(), vp, FitMode::Visible, 0, content), 800.0f / 100.0f);
    // Пустое содержимое — как по ширине.
    EXPECT_FLOAT_EQ(ot::fitZoom(kA4, vp, FitMode::Visible, 0, RectF{}), 800.0f / 595.0f);
    // Пустая страница — пустой bbox.
    auto blank = Document::createBlank(1);
    EXPECT_TRUE(DisplayList::build(*blank, 0)->contentBounds().empty());
}

TEST(render, fit_layout_limits_empty_and_huge_pages) {
    OT_FEATURE("view-04");
    const auto a4 = ot::layoutPageSize(595, 842);
    EXPECT_FLOAT_EQ(a4.width, 595);
    EXPECT_FLOAT_EQ(a4.height, 842);
    EXPECT_FLOAT_EQ(a4.scale, 1);
    EXPECT_FALSE(a4.defaulted || a4.clamped);
    for (const auto& bad : {std::pair<float, float>{0, 0}, {0, 842}, {-5, 10}, {NAN, 100}, {INFINITY, 100}}) {
        const auto s = ot::layoutPageSize(bad.first, bad.second);
        EXPECT_TRUE(s.defaulted);
        EXPECT_FLOAT_EQ(s.width, 612);
        EXPECT_FLOAT_EQ(s.height, 792);
    }
    // 20 000 × 100 pt (баннер) → 14 400 × 72 с сохранением пропорций.
    const auto huge = ot::layoutPageSize(20000, 100);
    EXPECT_TRUE(huge.clamped);
    EXPECT_FLOAT_EQ(huge.width, 14400);
    EXPECT_FLOAT_EQ(huge.height, 72);
    EXPECT_FLOAT_EQ(huge.scale, 0.72f);
    EXPECT_FLOAT_EQ(ot::layoutPageSize(14400, 14400).scale, 1);   // ровно предел — без изменений
    EXPECT_FLOAT_EQ(ot::layoutPageSize(100000, 0.5f).height, 1);  // сторона не меньше 1 pt
}

TEST(render, preview_one_per_eighth_octave) {
    EXPECT_FLOAT_EQ(ot::previewZoom(1.0f), 0.125f);
    EXPECT_FLOAT_EQ(ot::previewZoom(2.0f), 0.25f);
    // 1,0 и 1,04 — одна корзина: одно превью; 1,25 — другая.
    const auto a = ot::makePreviewKey(1, 0, 1.0f, 1.25f, 0);
    const auto b = ot::makePreviewKey(1, 0, 1.04f, 1.25f, 0);
    const auto c = ot::makePreviewKey(1, 0, 1.25f, 1.25f, 0);
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
    EXPECT_EQ(a.tx, -1);
    EXPECT_EQ(a.ty, -1);
    EXPECT_EQ(a.zoomMilli, 125);
    EXPECT_EQ(a.dprMilli, 1250);
    // Превью не путается с тайлом того же масштаба.
    const auto tile = ot::makeTileKey(1, 0, 0.125f, 1.25f, 0, ot::TileRect{0, 0, {0, 0, 10, 10}});
    EXPECT_NE(a, tile);
}

TEST(render, memory_tile_cache_limit_from_setting) {
    OT_FEATURE("view-36");
    EXPECT_EQ(ot::TileCache::limitFromMegabytes(10), 64LL * 1024 * 1024);
    EXPECT_EQ(ot::TileCache::limitFromMegabytes(512), 512LL * 1024 * 1024);
    EXPECT_EQ(ot::TileCache::limitFromMegabytes(100000), 4096LL * 1024 * 1024);
    ot::TileCache cache;
    EXPECT_EQ(cache.maxBytes(), 384LL * 1024 * 1024);
    EXPECT_EQ(cache.previewBudget(), cache.maxBytes() / 8);
    EXPECT_EQ(cache.tileBudget() + cache.previewBudget(), cache.maxBytes());
    // Уменьшение лимита вытесняет лишнее сразу.
    QImage tile(512, 512, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::white);
    for (int i = 0; i < 70; ++i) {
        ot::TileKey k;
        k.tx = i;
        ASSERT_TRUE(cache.insert(k, tile));
    }
    EXPECT_EQ(cache.count(), 70);
    cache.setMaxBytes(ot::TileCache::limitFromMegabytes(64));
    EXPECT_LE(cache.totalBytes(), 64LL * 1024 * 1024);
    EXPECT_EQ(cache.count(), 56);  // 7/8 от 64 МБ — тайлам
    cache.shrink(50);
    EXPECT_EQ(cache.count(), 28);
}

TEST(render, memory_cache_64mb_holds_on_200_image_pages) {
    OT_FEATURE("view-36");
    ot::test::qtApp();
    MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makeImagePagesPdf(mu, 200));
    ASSERT_EQ(doc->pageCount(), 200);
    ot::TileCache cache(64LL * 1024 * 1024);
    DisplayCache lists;  // LRU 40 страниц
    ot::RenderPool pool(8);
    std::atomic<qint64> peak{0};
    std::atomic<int> inserted{0};
    std::atomic<qint64> insertedBytes{0};
    auto record = [&] {
        const qint64 now = cache.totalBytes();
        qint64 seen = peak.load();
        while (now > seen && !peak.compare_exchange_weak(seen, now)) {
        }
    };
    const float zoom = 0.75f;  // 447 × 632 — тайл на страницу, ~1,1 МБ; 200 страниц ≈ 220 МБ растров
    for (int p = 0; p < 200; ++p) {
        const auto list = lists.get(*doc, p);
        ASSERT_LE(lists.size(), DisplayCache::kDefaultCapacity);
        const auto m = ot::pageMatrix(list->bounds(), zoom, 1.0f);
        for (const auto& t : ot::tileRectsFor(list->bounds(), zoom, 1.0f)) {
            ot::TileRequest req;
            req.key = ot::makeTileKey(1, p, zoom, 1.0f, 0, t);
            req.list = list;
            req.matrix = m;
            req.rect = t.rect;
            // Без context: колбэк в потоке пула — TileCache потокобезопасен.
            (void)pool.submit(req, nullptr, [&](const ot::TileResult& r) {
                ASSERT_TRUE(r.ok());
                EXPECT_TRUE(cache.insert(r.key, r.image));
                ++inserted;
                insertedBytes += r.image.sizeInBytes();
                record();
            });
        }
        // Превью страницы 1/8 — в своём кеше.
        const float pz = ot::previewZoom(zoom);
        ot::TileRequest prev;
        prev.key = ot::makePreviewKey(1, p, zoom, 1.0f, 0);
        prev.list = list;
        prev.matrix = ot::pageMatrix(list->bounds(), pz, 1.0f);
        prev.rect = ot::pageDeviceRect(list->bounds(), pz, 1.0f);
        (void)pool.submit(prev, nullptr, [&](const ot::TileResult& r) {
            ASSERT_TRUE(r.ok());
            EXPECT_TRUE(cache.insertPreview(r.key, r.image));
            record();
        });
    }
    ASSERT_TRUE(pool.waitForDone(60000));
    std::printf(
        "memory: inserted %d tiles, %lld MB; cache peak %lld MB, now %lld MB, %d tiles, %d previews\n",
        inserted.load(), static_cast<long long>(insertedBytes.load() >> 20),
        static_cast<long long>(peak.load() >> 20), static_cast<long long>(cache.totalBytes() >> 20),
        cache.count(), cache.previewCount());
    EXPECT_GT(insertedBytes.load(), 3 * cache.maxBytes());  // растров было втрое больше лимита
    EXPECT_LE(peak.load(), cache.maxBytes());
    EXPECT_LE(cache.totalBytes(), cache.maxBytes());
    EXPECT_EQ(cache.previewCount(), 200);  // превью маленькие — все 200 в 8 МБ
    EXPECT_EQ(lists.size(), DisplayCache::kDefaultCapacity);
    // Последние страницы — в кеше, первые вытеснены.
    EXPECT_TRUE(cache.find(ot::makeTileKey(1, 199, zoom, 1.0f, 0, ot::TileRect{0, 0, {}})).has_value());
    EXPECT_FALSE(cache.find(ot::makeTileKey(1, 0, zoom, 1.0f, 0, ot::TileRect{0, 0, {}})).has_value());
}

TEST(render, memory_low_memory_on_deactivate_and_idle) {
    OT_FEATURE("view-36");
    ot::test::qtApp();
    ot::TileCache cache(16LL * 1024 * 1024);
    QImage tile(512, 512, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::white);
    for (int i = 0; i < 14; ++i) {
        ot::TileKey k;
        k.tx = i;
        ASSERT_TRUE(cache.insert(k, tile));
    }
    ot::MemoryGuard guard(nullptr, 60000);
    guard.setTileCache(&cache);
    int lowCount = 0;
    QObject::connect(&guard, &ot::MemoryGuard::lowMemory, [&] { ++lowCount; });
    // Программа ушла в фон.
    QEvent deactivate(QEvent::ApplicationDeactivate);
    QCoreApplication::sendEvent(QCoreApplication::instance(), &deactivate);
    EXPECT_EQ(guard.relieveCount(), 1);
    EXPECT_EQ(lowCount, 1);
    EXPECT_EQ(cache.count(), 7);  // кеш тайлов — до половины бюджета
    // Простой: таймер срабатывает один раз; ввод перезапускает его.
    guard.setIdleInterval(30);
    ASSERT_TRUE(ot::test::waitUntil([&] { return guard.relieveCount() == 2; }, 2000));
    QThread::msleep(80);
    QCoreApplication::processEvents();
    EXPECT_EQ(guard.relieveCount(), 2);  // без ввода — не повторяется
    guard.noteActivity();
    ASSERT_TRUE(ot::test::waitUntil([&] { return guard.relieveCount() == 3; }, 2000));
    // Store MuPDF после сжатия укладывается в 50 % предела (в тестах он мал).
    EXPECT_TRUE(MuContext::forThisThread().shrinkStore(50));
}
