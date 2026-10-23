// ui.modes — режим чтения, полный экран и состояние панелей (план 2.4; эталон 3.1 «Режимы», 6.1, 6.8;
// view-21, view-22): Ctrl+H прячет глобальную панель, левую и правую панели, строку состояния, плавающую
// панель и полосы сообщений, показывает тонкую полосу внизу (страница, масштаб, выход); Esc возвращает панели
// ровно как были; Ctrl+L открывает окно полного экрана без рамки (offscreen не падает), клавиши и щелчок
// листают, переходы /Trans (растворение, угасание, «коробка») идут и заканчиваются, «Игнорировать все
// переходы», автопродвижение по /Dur и по настройке, «по кругу», Esc — выход на показанную страницу;
// /PageMode /FullScreen — полоса с кнопкой, /NonFullScreenPageMode — панель миниатюр; F8 и Shift+F4
// переживают перезапуск окна.
#include <QDir>
#include <QLineEdit>
#include <QSettings>
#include <QtTest/QtTest>

#include <cstdio>

#include "services/app_info.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/full_screen.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/read_mode.h"
#include "ui/shell/status_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::FullScreenSettings;
using ot::ui::FullScreenWindow;
using ot::ui::MainWindow;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTest::qWaitForWindowActive(&w);
}

void discard(MainWindow& w) {
    w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
}

}  // namespace

class ModesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-modes"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void init() {
        QSettings ini(ot::app::settingsPath(), QSettings::IniFormat);
        ini.remove(QLatin1String(MainWindow::kPanelsHiddenKey));
        ini.remove(QLatin1String(MainWindow::kToolPanelCollapsedKey));
        ini.remove(QStringLiteral("navpane"));
        ini.remove(QStringLiteral("fullScreen"));
    }

    void readModeHidesPanelsAndEscRestores() {
        std::printf("OT_FEATURE:view-21\n");
        MainWindow w;
        discard(w);
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf"), 0));
        DocumentView* doc = w.currentDocument();
        w.navPane().showPane(QStringLiteral("layers"));
        w.toolPanel().setCollapsed(true);
        doc->messageStack().showMessage(ot::ui::MessageKind::PdfA, QStringLiteral("pdfa"));
        QTRY_VERIFY(doc->messageStack().isVisible());
        QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(w.isReadMode());
        QVERIFY(!w.globalBar().isVisible());
        QVERIFY(!w.toolPanel().isVisible());
        QVERIFY(!w.navPane().isVisible());
        QVERIFY(!w.navPane().panel()->isVisible());
        QVERIFY(!w.status().isVisible());
        QVERIFY(!doc->floatingBar()->isVisible());
        QVERIFY(!doc->messageStack().isVisible());
        ot::ui::ReadModeBar* bar = w.readModeBar();
        QTRY_VERIFY(bar->isVisible());
        // Тонкая полоса — по центру у нижнего края, документ — на всю ширину окна.
        QTRY_COMPARE(doc->mapTo(&w, QPoint(0, 0)), QPoint(0, 0));
        QTRY_COMPARE(doc->width(), 1440);
        const QRect r = bar->geometry();
        const QPoint mid = bar->mapTo(&w, QPoint(r.width() / 2, r.height()));
        QVERIFY(r.height() <= 48);
        QVERIFY2(qAbs(mid.x() - 720) <= 2, qPrintable(QStringLiteral("bar centre x %1").arg(mid.x())));
        QVERIFY2(900 - mid.y() < 40, qPrintable(QStringLiteral("bar bottom y %1").arg(mid.y())));
        // Полоса листает и вводит страницу.
        QCOMPARE(bar->pageField()->text(), QStringLiteral("i"));
        bar->button(QStringLiteral("next"))->click();
        QTRY_COMPARE(doc->view().currentPage(), 1);
        QVERIFY(bar->goToInput(QStringLiteral("iv")));
        QTRY_COMPARE(doc->view().currentPage(), 3);
        const float zoom = doc->view().zoomPercent();
        bar->button(QStringLiteral("zoomIn"))->click();
        QVERIFY(doc->view().zoomPercent() > zoom);
        // Esc — назад, панели как были.
        doc->view().setFocus();
        QTest::keyClick(&doc->view(), Qt::Key_Escape);
        QTRY_VERIFY(!w.isReadMode());
        QVERIFY(w.globalBar().isVisible());
        QVERIFY(w.toolPanel().isVisible());
        QVERIFY(w.toolPanel().isCollapsed());
        QVERIFY(w.navPane().isVisible());
        QVERIFY(w.navPane().panel()->isVisible());
        QCOMPARE(w.navPane().currentPane(), QStringLiteral("layers"));
        QVERIFY(w.status().isVisible());
        QVERIFY(doc->floatingBar()->isVisible());
        QVERIFY(doc->messageStack().isVisible());
        QVERIFY(!bar->isVisible());
        // Ctrl+H — туда и обратно; F8 до режима остаётся после.
        QTest::keyClick(&w, Qt::Key_F8);
        QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(w.isReadMode());
        QTest::keyClick(&w, Qt::Key_H, Qt::ControlModifier);
        QVERIFY(!w.isReadMode());
        QVERIFY(w.globalBar().isVisible());
        QVERIFY(!w.toolPanel().isVisible());
        QVERIFY(!w.status().isVisible());
        QTest::keyClick(&w, Qt::Key_F8);
        w.toolPanel().setCollapsed(false);
    }

    void panelStateSurvivesRestart() {
        {
            MainWindow w;
            discard(w);
            showActive(w);
            QVERIFY(w.openFile(fixture("demo/report.pdf")));
            QTest::keyClick(&w, Qt::Key_F4, Qt::ShiftModifier);
            QVERIFY(w.toolPanel().isCollapsed());
            QTest::keyClick(&w, Qt::Key_F8);
            QVERIFY(w.panelsHidden());
        }
        MainWindow again;
        discard(again);
        showActive(again);
        QVERIFY(again.openFile(fixture("demo/report.pdf")));
        QVERIFY(again.panelsHidden());
        QVERIFY(again.toolPanel().isCollapsed());
        QVERIFY(!again.toolPanel().isVisible());
        QVERIFY(!again.status().isVisible());
        QTest::keyClick(&again, Qt::Key_F8);
        QTest::keyClick(&again, Qt::Key_F4, Qt::ShiftModifier);
        QVERIFY(!again.panelsHidden());
        QVERIFY(!again.toolPanel().isCollapsed());
    }

    void fullScreenOffscreenKeysAndExit() {
        std::printf("OT_FEATURE:view-22\n");
        MainWindow w;
        discard(w);
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf"), 0));
        DocumentView* doc = w.currentDocument();
        QTest::keyClick(&w, Qt::Key_L, Qt::ControlModifier);
        FullScreenWindow* fs = w.fullScreenWindow();
        QVERIFY(fs != nullptr);
        QTRY_VERIFY(fs->isVisible());
        QVERIFY(fs->isFullScreen() || fs->windowState().testFlag(Qt::WindowFullScreen));
        QVERIFY((fs->windowFlags() & Qt::FramelessWindowHint) != 0);
        QTRY_VERIFY(fs->hasTransitions());
        QCOMPARE(fs->currentPage(), 0);
        QCOMPARE(fs->view().pageMode(), ot::ui::PageMode::SinglePage);
        // → — вторая страница с растворением 0,5 с.
        QTest::keyClick(&fs->view(), Qt::Key_Right);
        QTRY_COMPARE(fs->currentPage(), 1);
        QCOMPARE(fs->lastTransition(), QStringLiteral("Dissolve"));
        QVERIFY(fs->isTransitionRunning());
        QTRY_VERIFY_WITH_TIMEOUT(!fs->isTransitionRunning(), 3000);
        // Щелчок — вперёд (шторка), правый — назад.
        QTest::mouseClick(fs->view().viewport(), Qt::LeftButton, {}, QPoint(200, 200));
        QTRY_COMPARE(fs->currentPage(), 2);
        QCOMPARE(fs->lastTransition(), QStringLiteral("Wipe"));
        QTest::mouseClick(fs->view().viewport(), Qt::RightButton, {}, QPoint(200, 200));
        QTRY_COMPARE(fs->currentPage(), 1);
        QTest::keyClick(&fs->view(), Qt::Key_End);
        QTRY_COMPARE(fs->currentPage(), 3);
        // Esc — выход, вид документа на показанной странице.
        QTest::keyClick(&fs->view(), Qt::Key_Escape);
        QTRY_VERIFY(w.fullScreenWindow() == nullptr);
        QTRY_COMPARE(doc->view().currentPage(), 3);
    }

    void fullScreenSettingsAdvanceLoopAndIgnore() {
        MainWindow w;
        discard(w);
        showActive(w);
        QVERIFY(w.openFile(fixture("view/present.pdf"), 0));
        // /PageMode /FullScreen — полоса с кнопкой; панель — /NonFullScreenPageMode /UseThumbs.
        DocumentView* doc = w.currentDocument();
        ot::ui::MessageBar* request = nullptr;
        for (ot::ui::MessageBar* b : doc->messages()) {
            if (b->objectName() == QLatin1String("fullScreenRequestBar")) {
                request = b;
            }
        }
        QVERIFY(request != nullptr);
        QTRY_VERIFY(w.navPane().isPanelOpen());
        QCOMPARE(w.navPane().currentPane(), QStringLiteral("thumbnails"));
        emit request->actionTriggered(QStringLiteral("fullScreen"));
        FullScreenWindow* fs = w.fullScreenWindow();
        QVERIFY(fs != nullptr);
        QTRY_VERIFY(fs->hasTransitions());
        // /Dur 1 у первой — сама на вторую (угасание).
        QTRY_COMPARE_WITH_TIMEOUT(fs->currentPage(), 1, 4000);
        QCOMPARE(fs->lastTransition(), QStringLiteral("Fade"));
        // «Игнорировать все переходы», листать каждую секунду, по кругу.
        FullScreenSettings s = fs->settings();
        s.transitions = false;
        s.advanceSeconds = 1;
        s.loop = true;
        s.background = QColor(Qt::darkBlue);
        fs->setSettings(s);
        QTRY_COMPARE_WITH_TIMEOUT(fs->currentPage(), 2, 4000);
        QCOMPARE(fs->lastTransition(), QStringLiteral("None"));
        QVERIFY(!fs->isTransitionRunning());
        QTRY_COMPARE_WITH_TIMEOUT(fs->currentPage(), 0, 4000);  // по кругу
        // Настройки — в settings.ini.
        s.save();
        const FullScreenSettings loaded = FullScreenSettings::load();
        QCOMPARE(loaded.advanceSeconds, 1);
        QCOMPARE(loaded.loop, true);
        QCOMPARE(loaded.transitions, false);
        QCOMPARE(loaded.background, QColor(Qt::darkBlue));
        // Esc не выходит, если так настроено; Ctrl+L — выходит всегда.
        s.escapeExits = false;
        s.advanceSeconds = 0;
        fs->setSettings(s);
        QTest::keyClick(&fs->view(), Qt::Key_Escape);
        QVERIFY(w.fullScreenWindow() != nullptr);
        QTest::keyClick(&fs->view(), Qt::Key_L, Qt::ControlModifier);
        QTRY_VERIFY(w.fullScreenWindow() == nullptr);
    }

    void windowClosesWithFullScreenOpen() {
        auto* w = new MainWindow();
        discard(*w);
        showActive(*w);
        QVERIFY(w->openFile(fixture("view/nav.pdf")));
        w->enterFullScreen();
        QTRY_VERIFY(w->fullScreenWindow() != nullptr && w->fullScreenWindow()->isVisible());
        delete w;  // окно полного экрана уходит раньше пула и кеша окна — без падения
    }
};

OT_UI_TEST(ModesTest, "modes");
#include "modes_test.moc"
