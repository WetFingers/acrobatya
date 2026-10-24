// ui.page_view_render — растр PageView (план 2.2; эталон 4.1–4.2, 6.1; view-01 — экран): снимок вида после
// рендера совпадает с целым рендером страницы в порогах imgcmp (offscreen, dpr 1); тайлы запрашиваются только
// для видимой области и запаса в 1 экран, превью — раньше тайлов; прокрутка отменяет ненужные задачи; правка
// страницы меняет ключи только её тайлов; повреждённая страница — заглушка, остальные рисуются; слои поверх.
#include <QFile>
#include <QPainter>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstring>
#include <set>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "services/document_session.h"
#include "services/task_context.h"
#include "support/ui_test.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

using ot::ui::PageView;
using ot::ui::TilePainter;

namespace {

QString mixedPdf() { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/view/layout-mixed.pdf"); }

// n пустых страниц A4 во временном каталоге.
QString blankPdf(const QTemporaryDir& dir, int pages) {
    auto doc = ot::Document::createBlank(pages);
    const std::string bytes = doc->savePdfToMemory();
    const QString path = dir.filePath(QStringLiteral("blank-%1.pdf").arg(pages));
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(bytes.data(), static_cast<qint64>(bytes.size()));
    }
    return path;
}

ot::Bitmap toBitmap(const QImage& image) {
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    ot::Bitmap b;
    b.width = rgb.width();
    b.height = rgb.height();
    b.components = 3;
    b.stride = b.width * 3;
    b.pixels.resize(static_cast<std::size_t>(b.stride) * static_cast<std::size_t>(b.height));
    for (int y = 0; y < b.height; ++y) {
        std::memcpy(b.row(y), rgb.constScanLine(y), static_cast<std::size_t>(b.stride));
    }
    return b;
}

QRect snapped(const QRectF& r) {
    return QRect(qRound(r.left()), qRound(r.top()), qRound(r.width()), qRound(r.height()));
}

// Вид с документом, показан и дорисован (все видимые тайлы в кеше).
bool waitComplete(PageView& v, int timeoutMs = 20000) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs) {
        v.viewport()->repaint();
        if (v.tilePainter().visibleComplete()) {
            return true;
        }
        QTest::qWait(10);
    }
    return false;
}

std::set<int> requestedPages(const TilePainter& tiles) {
    std::set<int> pages;
    for (const auto& r : tiles.requestLog()) {
        pages.insert(r.page);
    }
    return pages;
}

}  // namespace

class PageViewRenderTest : public QObject {
    Q_OBJECT
private slots:

    // Снимок вида = целый рендер страницы тем же масштабом (пороги imgcmp по умолчанию).
    void snapshotMatchesPageRender() {
        ot::DocumentSession session;
        QVERIFY(session.open(mixedPdf()));
        PageView v;
        v.setRespectDocumentLayout(false);
        v.resize(1000, 800);
        v.setDocument(&session);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        QCOMPARE(v.devicePixelRatioF(), 1.0);
        for (const int page : {0, 3}) {
            v.setZoomPercent(page == 0 ? 100 : 150);
            v.goToPage(page);
            QVERIFY(waitComplete(v));
            const QImage shot = v.viewport()->grab().toImage();
            const QRect onScreen = snapped(v.pageViewportRect(page)).intersected(v.viewport()->rect());
            QVERIFY(!onScreen.isEmpty());
            const QImage reference = session.renderPage(page, v.zoom() * 72.0);
            QVERIFY(!reference.isNull());
            const QRect pageRect = snapped(v.pageViewportRect(page));
            QCOMPARE(reference.size(), pageRect.size());
            const QImage a = shot.copy(onScreen);
            const QImage b = reference.copy(onScreen.translated(-pageRect.topLeft()));
            const ot::CompareResult cmp = ot::compareBitmaps(toBitmap(a), toBitmap(b));
            QVERIFY2(cmp.within(ot::FuzzyThreshold{}), cmp.toJson().c_str());
            const QString out = QStringLiteral(OTTISK_BINARY_DIR "/ui-shots");
            QDir().mkpath(out);
            shot.save(out + QStringLiteral("/page_view_render_%1.png").arg(page));
        }
        // Поворот вида: тот же растр, повёрнутый на 90° (рендер MuPDF своей матрицей — близко, не бит в бит).
        v.setZoomPercent(100);
        v.goToPage(0);
        v.setViewRotation(90);
        v.goToPage(0);
        QVERIFY(waitComplete(v));
        const QImage shot = v.viewport()->grab().toImage();
        const QRect pageRect = snapped(v.pageViewportRect(0));
        const QRect onScreen = pageRect.intersected(v.viewport()->rect());
        const QImage rotated = session.renderPage(0, v.zoom() * 72.0).transformed(QTransform().rotate(90));
        QCOMPARE(rotated.size(), pageRect.size());
        const ot::CrossResult cross = ot::crossCompare(
            toBitmap(shot.copy(onScreen)), toBitmap(rotated.copy(onScreen.translated(-pageRect.topLeft()))));
        QVERIFY2(cross.ok, cross.toJson().c_str());
        session.close();
        std::printf("OT_FEATURE:view-01\n");
    }

    // Запросы — только видимое и запас в экран по вертикали; превью раньше тайлов; далёких страниц — нет.
    void requestsOnlyVisiblePlusOneScreen() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ot::DocumentSession session;
        QVERIFY(session.open(blankPdf(dir, 40)));
        PageView v;
        v.resize(800, 600);
        v.setDocument(&session);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(100);
        v.goToPage(20);
        v.tilePainter().clearRequestLog();
        QVERIFY(waitComplete(v));
        const QRectF viewport(v.viewport()->rect());
        const QRectF extended = viewport.adjusted(0, -viewport.height(), 0, viewport.height());
        const auto& log = v.tilePainter().requestLog();
        QVERIFY(!log.empty());
        const std::vector<int> margin = v.visiblePages(viewport.height());
        int tiles = 0;
        int previewPriority = 0;
        int tilePriority = 0;
        for (const auto& r : log) {
            QVERIFY2(std::find(margin.begin(), margin.end(), r.page) != margin.end(),
                     qPrintable(QString::number(r.page)));
            if (r.preview) {
                previewPriority = std::max(previewPriority, r.priority);
                continue;
            }
            ++tiles;
            tilePriority = std::max(tilePriority, r.priority);
            QVERIFY2(r.viewportRect.intersects(extended),
                     qPrintable(
                         QStringLiteral("страница %1, тайл %2,%3").arg(r.page).arg(r.key.tx).arg(r.key.ty)));
        }
        QVERIFY(tiles > 0);
        QVERIFY(previewPriority > tilePriority);
        const std::set<int> pages = requestedPages(v.tilePainter());
        QVERIFY(!pages.contains(0));
        QVERIFY(!pages.contains(39));
        QVERIFY(pages.contains(20));

        // Прокрутка далеко: новые запросы — там, прежние задачи отменены или завершены.
        v.tilePainter().clearRequestLog();
        v.goToPage(35);
        QVERIFY(waitComplete(v));
        const std::set<int> after = requestedPages(v.tilePainter());
        QVERIFY(after.contains(35));
        QVERIFY(!after.contains(20));
        QTRY_COMPARE_WITH_TIMEOUT(v.tilePainter().inflight(), 0, 10000);
        session.close();
    }

    // Зум: тайлы прежнего масштаба держат картинку (кадр не пустой) до прихода новых; новые — по новым
    // ключам.
    void zoomRequestsNewTilesAndKeepsOldUntilReady() {
        ot::DocumentSession session;
        QVERIFY(session.open(mixedPdf()));
        PageView v;
        v.setRespectDocumentLayout(false);
        v.resize(800, 600);
        v.setDocument(&session);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(100);
        QVERIFY(waitComplete(v));
        v.tilePainter().clearRequestLog();
        v.setZoomPercent(200);
        // Сразу после зума (до рендера) кадр уже не белый: превью или прежние тайлы растянуты.
        const QImage early = v.viewport()->grab().toImage();
        const QRect page = snapped(v.pageViewportRect(v.currentPage())).intersected(v.viewport()->rect());
        bool ink = false;
        for (int y = page.top(); y < page.bottom() && !ink; y += 3) {
            for (int x = page.left(); x < page.right() && !ink; x += 3) {
                ink = qGray(early.pixel(x, y)) < 200;
            }
        }
        QVERIFY(ink);
        QVERIFY(waitComplete(v));
        int zoom200 = 0;
        for (const auto& r : v.tilePainter().requestLog()) {
            zoom200 += (!r.preview && r.key.zoomMilli == qRound(v.zoom() * 1000)) ? 1 : 0;
        }
        QVERIFY(zoom200 > 0);
        session.close();
    }

    // Правка страницы 1 меняет ключи только её тайлов (поле revision); остальные — из кеша.
    void pageEditInvalidatesOnlyThatPage() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ot::DocumentSession session;
        QVERIFY(session.open(blankPdf(dir, 3)));
        PageView v;
        v.resize(900, 2400);
        v.setDocument(&session);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(50);
        QVERIFY(waitComplete(v));
        QCOMPARE(v.visiblePages().size(), 3u);
        v.tilePainter().clearRequestLog();
        session
            .edit(QStringLiteral("Add note"),
                  [](ot::TaskContext& tc) {
                      fz_context* ctx = tc.context();
                      pdf_document* pdf = tc.document().pdf();
                      pdf_page* page = nullptr;
                      ot::mu::call(ctx, [&] {
                          page = pdf_load_page(ctx, pdf, 1);
                          pdf_annot* annot = pdf_create_annot(ctx, page, PDF_ANNOT_SQUARE);
                          pdf_set_annot_rect(ctx, annot, fz_make_rect(100, 100, 300, 300));
                          pdf_update_annot(ctx, annot);
                          pdf_drop_annot(ctx, annot);
                      });
                      fz_drop_page(ctx, reinterpret_cast<fz_page*>(page));
                      tc.touchPage(1);
                  })
            .waitForFinished();
        QTest::qWait(20);
        QVERIFY(waitComplete(v));
        const std::set<int> pages = requestedPages(v.tilePainter());
        QCOMPARE(pages, (std::set<int>{1}));
        session.close();
    }

    // Повреждённая страница (display list — ошибка): заглушка и сигнал, соседние рисуются.
    void brokenPageShowsPlaceholder() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ot::DocumentSession session;
        QVERIFY(session.open(blankPdf(dir, 3)));
        // Страница удалена без структурной отметки: раскладка (summary) по-прежнему о трёх — третьей нет.
        session
            .edit(QStringLiteral("Delete page"),
                  [](ot::TaskContext& tc) {
                      fz_context* ctx = tc.context();
                      pdf_document* pdf = tc.document().pdf();
                      ot::mu::call(ctx, [&] { pdf_delete_page(ctx, pdf, 2); });
                      tc.touchPage(0);
                  })
            .waitForFinished();
        QCOMPARE(session.summary().pageCount, 3);
        PageView v;
        v.resize(900, 2400);
        v.setDocument(&session);
        QSignalSpy failed(&v.tilePainter(), &TilePainter::pageFailed);
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(50);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 10000);
        QCOMPARE(failed.first().first().toInt(), 2);
        QVERIFY(v.tilePainter().isBroken(2));
        QVERIFY(!v.tilePainter().isBroken(0));
        QVERIFY(waitComplete(v));
        const QImage shot = v.viewport()->grab().toImage();
        const QRect broken = snapped(v.pageViewportRect(2)).intersected(v.viewport()->rect());
        int dark = 0;
        for (int y = broken.top(); y < broken.bottom(); ++y) {
            for (int x = broken.left(); x < broken.right(); ++x) {
                dark += qGray(shot.pixel(x, y)) < 180 ? 1 : 0;
            }
        }
        QVERIFY2(dark > 50, "заглушка «Страница не отрисована»");
        session.close();
    }

    // Слои: выделение (заливка selection) и рамка аннотации (accent) — поверх растра, в координатах страницы.
    void overlaysPaintOverPages() {
        PageView v;
        v.resize(800, 600);
        v.setPageSizes(QVector<QSizeF>(2, QSizeF(595, 842)));
        v.show();
        QVERIFY(QTest::qWaitForWindowExposed(&v));
        v.setZoomPercent(100);
        const ot::QuadF quad{{100, 100}, {300, 100}, {100, 130}, {300, 130}};
        v.selectionOverlay().setQuads(0, {quad});
        v.annotationOverlay().setRects(0, {ot::RectF{100, 200, 300, 260}});
        const QImage shot = v.viewport()->grab().toImage();
        const QPointF inside = v.pageTransform(0).map(QPointF(200, 115));
        const QPointF outside = v.pageTransform(0).map(QPointF(200, 160));
        const QColor sel = shot.pixelColor(inside.toPoint());
        QVERIFY2(sel.blue() > sel.red() + 20, qPrintable(sel.name()));
        QCOMPARE(shot.pixelColor(outside.toPoint()), QColor(Qt::white));
        const QPoint frame = v.pageTransform(0).map(QPointF(100, 230)).toPoint();
        bool stroked = false;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                stroked = stroked || shot.pixelColor(frame + QPoint(dx, dy)) != QColor(Qt::white);
            }
        }
        QVERIFY(stroked);
        // Поиск, выделение, рамки аннотаций, рамки областей (2.5b), фокус ссылки, слой инструмента.
        QCOMPARE(v.overlays().size(), 6);
    }
};

OT_UI_TEST(PageViewRenderTest, "page_view_render");
#include "page_view_render_test.moc"
