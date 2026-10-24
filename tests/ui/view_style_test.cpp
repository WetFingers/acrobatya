// ui.view_style — замена цветов и предпросмотр наложения в окне (план 2.5b; эталон 3.1, 3.16; view-26,
// ocr-38, pro-47): «Вид → Ночной режим» перекрашивает вкладки (поле страницы тёмное, команда отмечена),
// повторный выбор снимает; «Высокая контрастность» — схема из reading/highContrastScheme; view/overprint.pdf
// (PDF/X) по умолчанию («Только для PDF/X») рисуется с наложением — пересечение синее, «Никогда» — пурпурное.
// Цвет — с растра вьюпорта после дорисовки тайлов.
#include <QDir>
#include <QImage>
#include <QSettings>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/commands.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/tokens.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

using ot::RecolorMode;
using ot::ui::MainWindow;
using ot::ui::PageView;

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

// Цвет вьюпорта в точке страницы (пункты MuPDF, y вниз).
QColor at(PageView& v, int page, QPointF point) {
    const QImage img = v.viewport()->grab().toImage();
    const QPointF p = v.pageTransform(page).map(point) * img.devicePixelRatio();
    return img.pixelColor(p.toPoint());
}

}  // namespace

class ViewStyleTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-view_style"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void nightModeAndHighContrastRecolorTabs() {
        std::printf("OT_FEATURE:view-26\nOT_FEATURE:ocr-38\n");
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("fixtures/view/overprint.pdf"), 0));
        PageView& v = w.currentDocument()->view();
        QVERIFY(settle(v));
        const QPointF margin(20, 20);
        QVERIFY(at(v, 0, margin).lightness() > 240);

        ot::CommandRegistry& c = w.commands();
        QVERIFY(c.trigger(QStringLiteral("view.nightMode")));
        QVERIFY(c.isChecked(QStringLiteral("view.nightMode")));
        QCOMPARE(v.renderStyle().recolor.mode, RecolorMode::Replace);
        QCOMPARE(v.renderStyle().recolor.background,
                 (std::uint32_t{ot::ui::kNightReading.background.r} << 16) |
                     (std::uint32_t{ot::ui::kNightReading.background.g} << 8) |
                     ot::ui::kNightReading.background.b);
        QVERIFY(settle(v));
        QVERIFY2(at(v, 0, margin).lightness() < 60, qPrintable(at(v, 0, margin).name()));
        QCOMPARE(ot::Settings().get(ot::settings::kRecolor), QStringLiteral("night"));

        // Новая вкладка — тоже в ночном режиме.
        QVERIFY(w.openFile(source("fixtures/demo/report.pdf"), 0));
        QCOMPARE(w.currentDocument()->view().renderStyle().recolor.mode, RecolorMode::Replace);

        // Высокая контрастность (белый на чёрном по умолчанию) вместо ночного; повторный выбор — снять.
        QVERIFY(c.trigger(QStringLiteral("view.highContrast")));
        QVERIFY(!c.isChecked(QStringLiteral("view.nightMode")));
        QVERIFY(c.isChecked(QStringLiteral("view.highContrast")));
        QCOMPARE(v.renderStyle().recolor.text, std::uint32_t{0xFFFFFF});
        QCOMPARE(v.renderStyle().recolor.background, std::uint32_t{0x000000});
        QVERIFY(c.trigger(QStringLiteral("view.highContrast")));
        QCOMPARE(v.renderStyle().recolor.mode, RecolorMode::None);
        QVERIFY(settle(v));
        QVERIFY(at(v, 0, margin).lightness() > 240);
    }

    void overprintPreviewForPdfX() {
        std::printf("OT_FEATURE:pro-47\n");
        ot::Settings().set(ot::settings::kRecolor, QStringLiteral("none"));
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(source("fixtures/view/overprint.pdf"), 0));
        ot::ui::DocumentView* doc = w.currentDocument();
        PageView& v = doc->view();
        QVERIFY(doc->isPdfX());
        QVERIFY(v.renderStyle().overprint);  // «Только для PDF/X» — по умолчанию
        QVERIFY(settle(v));
        const QPointF overlap(290, 841.89 - 420);  // центр пересечения (y вниз)
        const QColor blue = at(v, 0, overlap);
        QVERIFY2(blue.red() < 110 && blue.blue() > 100, qPrintable(blue.name()));

        ot::Settings().set(ot::settings::kOverprintPreview, QStringLiteral("never"));
        w.applyViewStyle();
        QVERIFY(!v.renderStyle().overprint);
        QVERIFY(settle(v));
        const QColor magenta = at(v, 0, overlap);
        QVERIFY2(magenta.red() > 180 && magenta.green() < 80, qPrintable(magenta.name()));
        ot::Settings().set(ot::settings::kOverprintPreview, QStringLiteral("pdfx"));
        w.applyViewStyle();
        QVERIFY(v.renderStyle().overprint);

        // Не PDF/X в режиме «Только для PDF/X» — без симуляции.
        QVERIFY(w.openFile(source("fixtures/demo/report.pdf"), 0));
        QVERIFY(!w.currentDocument()->isPdfX());
        QVERIFY(!w.currentDocument()->view().renderStyle().overprint);
    }
};

OT_UI_TEST(ViewStyleTest, "view_style");

#include "view_style_test.moc"
