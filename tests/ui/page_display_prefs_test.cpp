// ui.page_display_prefs — «Установки → Просмотр страницы» целиком (план 2.5b; эталон 3.1 «Сглаживание и
// качество», 3.18; view-36, pro-47): поля пишутся в settings.ini и читаются обратно (с предпросмотром
// наложения «Всегда / Только для PDF/X / Никогда»); «ОК» меняет стиль рендера вкладок — сглаживание текста и
// векторов (AA 8 → 0), картинок, «утолщать тонкие линии» — и ключи запрошенных тайлов; «Показывать рамки
// обрезки, подрезки и выпуска» — слой рамок совпадает с fz_bound_page_box (BleedBox, TrimBox, ArtBox, в том
// числе у страницы с /CropBox и /Rotate 90) и рисуется цветами kPageBoxColors; раскладка и масштаб по
// умолчанию — у новых вкладок («Автоматически» — /PageLayout документа).
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QSettings>
#include <QtTest/QtTest>

#include <cmath>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "services/app_info.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/page_display_page.h"
#include "ui/theme/tokens.h"
#include "ui/view/boxes_overlay.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

extern "C" {
#include <mupdf/fitz.h>
}

using ot::Settings;
using ot::ui::MainWindow;
using ot::ui::PageView;
namespace keys = ot::settings;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

bool settle(PageView& v) {
    for (int i = 0; i < 600; ++i) {
        v.viewport()->repaint();
        if (v.tilePainter().visibleComplete()) {
            return true;
        }
        QTest::qWait(10);
    }
    return false;
}

// Рамка fz_bound_page_box страницы — независимо от программы, прямо MuPDF.
ot::RectF boundBox(const QString& file, int page, fz_box_type box) {
    auto doc = ot::Document::openOrThrow(file.toStdWString());
    fz_context* ctx = doc->context().get();
    ot::mu::PageHandle p(ctx);
    fz_rect r{};
    ot::mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc->handle(), page));
        r = fz_bound_page_box(ctx, p, box);
    });
    return {r.x0, r.y0, r.x1, r.y1};
}

bool same(const ot::RectF& a, const ot::RectF& b) {
    return std::abs(a.x0 - b.x0) < 0.01 && std::abs(a.y0 - b.y0) < 0.01 && std::abs(a.x1 - b.x1) < 0.01 &&
           std::abs(a.y1 - b.y1) < 0.01;
}

}  // namespace

class PageDisplayPrefsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-page_display_prefs"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() {
        Settings s;
        for (const char* k :
             {keys::kSmoothText.name, keys::kSmoothLineArt.name, keys::kSmoothImages.name,
              keys::kEnhanceThinLines.name, keys::kShowPageBoxes.name, keys::kOverprintPreview.name,
              keys::kDefaultLayout.name, keys::kDefaultZoom.name}) {
            s.reset(k);
        }
    }

    void fieldsRoundTrip() {
        std::printf("OT_FEATURE:view-36\n");
        ot::ui::PageDisplayPage page;
        page.load(Settings());
        QCOMPARE(page.overprintBox()->currentData().toString(), QStringLiteral("pdfx"));
        QCOMPARE(page.overprintBox()->currentText(), QStringLiteral("Только для PDF/X"));
        QVERIFY(page.smoothTextBox()->isChecked());
        QVERIFY(page.thinLinesBox()->isChecked());
        QVERIFY(!page.pageBoxesBox()->isChecked());
        page.overprintBox()->setCurrentIndex(page.overprintBox()->findData(QStringLiteral("always")));
        page.smoothTextBox()->setChecked(false);
        page.smoothImagesBox()->setChecked(false);
        page.pageBoxesBox()->setChecked(true);
        page.zoomBox()->setCurrentIndex(page.zoomBox()->findData(QStringLiteral("fitWidth")));
        Settings s;
        page.apply(s);
        QCOMPARE(s.get(keys::kOverprintPreview), QStringLiteral("always"));
        QVERIFY(!s.get(keys::kSmoothText));
        QVERIFY(!s.get(keys::kSmoothImages));
        QVERIFY(s.get(keys::kShowPageBoxes));
        QCOMPARE(s.get(keys::kDefaultZoom), QStringLiteral("fitWidth"));
        ot::ui::PageDisplayPage again;
        again.load(Settings());
        QCOMPARE(again.overprintBox()->currentData().toString(), QStringLiteral("always"));
        QVERIFY(again.pageBoxesBox()->isChecked());
    }

    void renderingSettingsReachTiles() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("fixtures/demo/report.pdf"), 0));
        PageView& v = w.currentDocument()->view();
        QCOMPARE(v.renderStyle().textAa, 8);
        QCOMPARE(v.renderStyle().graphicsAa, 8);
        QVERIFY(v.renderStyle().smoothImages);
        QVERIFY(v.renderStyle().thinLines);
        QVERIFY(settle(v));

        Settings s;
        s.set(keys::kSmoothText, false);
        s.set(keys::kSmoothLineArt, false);
        s.set(keys::kSmoothImages, false);
        s.set(keys::kEnhanceThinLines, false);
        w.applyPreferences();
        QCOMPARE(v.renderStyle().textAa, 0);
        QCOMPARE(v.renderStyle().graphicsAa, 0);
        QVERIFY(!v.renderStyle().smoothImages);
        QVERIFY(!v.renderStyle().thinLines);
        v.tilePainter().clearRequestLog();
        QVERIFY(settle(v));
        QVERIFY(!v.tilePainter().requestLog().empty());
        for (const auto& r : v.tilePainter().requestLog()) {
            QCOMPARE(r.key.style, v.renderStyle().key());  // новые растры — с новым стилем
        }
    }

    void boxesOverlayMatchesBoundPageBox() {
        const QString file = source("fixtures/view/boxes.pdf");
        Settings().set(keys::kShowPageBoxes, true);
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(file, 0));
        PageView& v = w.currentDocument()->view();
        ot::ui::BoxesOverlay& overlay = v.boxesOverlay();
        QVERIFY(overlay.isShown());
        QVERIFY(settle(v));
        QTRY_VERIFY_WITH_TIMEOUT(overlay.hasBoxes(0), 10000);
        for (int page : {0, 1}) {
            if (page == 1) {
                v.goToPage(1);
                QVERIFY(settle(v));
                QTRY_VERIFY_WITH_TIMEOUT(overlay.hasBoxes(1), 10000);
            }
            const ot::pages::PageBoxes b = *overlay.boxes(page);
            QVERIFY(b.hasBleed && b.hasTrim && b.hasArt);
            QVERIFY(same(b.bleed, boundBox(file, page, FZ_BLEED_BOX)));
            QVERIFY(same(b.trim, boundBox(file, page, FZ_TRIM_BOX)));
            QVERIFY(same(b.art, boundBox(file, page, FZ_ART_BOX)));
        }
        // Красная рамка обрезки нарисована по своему месту (середина верхней стороны, ±1 px).
        v.goToPage(0);
        QVERIFY(settle(v));
        const ot::RectF trim = overlay.boxes(0)->trim;
        const QImage img = v.viewport()->grab().toImage();
        const QPointF top =
            v.pageTransform(0).map(QPointF((trim.x0 + trim.x1) / 2, trim.y0)) * img.devicePixelRatio();
        const QColor red = ot::ui::kPageBoxColors[1].color();
        bool found = false;
        for (int dy = -1; dy <= 1 && !found; ++dy) {
            const QColor c =
                img.pixelColor(QPoint(static_cast<int>(top.x()), static_cast<int>(top.y()) + dy));
            found = std::abs(c.red() - red.red()) < 24 && std::abs(c.green() - red.green()) < 24 &&
                    std::abs(c.blue() - red.blue()) < 24;
        }
        QVERIFY(found);

        // Выключить — рамок нет.
        Settings().set(keys::kShowPageBoxes, false);
        w.applyPreferences();
        QVERIFY(!overlay.isShown());
    }

    void defaultLayoutAndZoomForNewTabs() {
        const QString mixed = source("fixtures/view/layout-mixed.pdf");  // /PageLayout /TwoColumnRight
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(mixed, 0));
            // «Автоматически» — /PageLayout документа: TwoColumnRight — разворот непрерывно, титул отдельно.
            QCOMPARE(w.currentDocument()->view().pageMode(), ot::ui::PageMode::FacingContinuous);
            QVERIFY(w.currentDocument()->view().coverPage());
        }
        Settings s;
        s.set(keys::kDefaultLayout, QStringLiteral("single"));
        s.set(keys::kDefaultZoom, QStringLiteral("fitWidth"));
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(mixed, 0));
            PageView& v = w.currentDocument()->view();
            QCOMPARE(v.pageMode(), ot::ui::PageMode::SinglePage);
            QTRY_VERIFY(v.fitMode() == ot::FitMode::Width);
        }
        s.set(keys::kDefaultZoom, QStringLiteral("200"));
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(source("fixtures/demo/report.pdf"), 0));
            QTRY_COMPARE(qRound(w.currentDocument()->view().zoomPercent()), 200);
        }
    }
};

OT_UI_TEST(PageDisplayPrefsTest, "page_display_prefs");

#include "page_display_prefs_test.moc"
