// ui.status_bar — строка состояния и начальный вид (план 2.4; эталон 3.1 «Навигация», 6.1 «Строка
// состояния»; view-08, view-47): 32 px во всю ширину внизу; поле страницы с логическими номерами (nav.pdf —
// i…iv: «iv» → четвёртая, «3» → третья, неизвестное — поле возвращает текущую), Ctrl+Shift+N — фокус в поле;
// ◀ ▶; масштаб (ZoomBox ↔ вид); режимы раскладки и поворот вида; индикаторы «Подписано», «PDF/A», «Только
// чтение»; фоновая задача с прогрессом и отменой; при открытии — /OpenAction (стр. 2 nav.pdf), /PageMode
// /UseOutlines (закладки report.pdf), /DisplayDocTitle (заголовок окна); F8 и Главная прячут строку.
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QSettings>
#include <QtTest/QtTest>

#include <cstdio>

#include "services/app_info.h"
#include "services/background_tasks.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/status_bar.h"
#include "ui/view/page_view.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/tool_button.h"
#include "ui/widgets/zoom_box.h"

using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::StatusBar;

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

}  // namespace

class StatusBarTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-status_bar"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        // Последний вид не восстанавливается — проверяется /OpenAction файла.
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void geometryAndVisibility() {
        MainWindow w;
        showActive(w);
        StatusBar& bar = w.status();
        QVERIFY(!bar.isVisible());  // Главная без фоновых задач
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QTRY_VERIFY(bar.isVisible());
        QCOMPARE(bar.height(), 32);
        QCOMPARE(bar.width(), 1440);
        QCOMPARE(bar.mapTo(&w, QPoint(0, 0)), QPoint(0, 900 - 32));
        QTest::keyClick(&w, Qt::Key_F8);
        QVERIFY(!bar.isVisible());
        QTest::keyClick(&w, Qt::Key_F8);
        QVERIFY(bar.isVisible());
        w.showHome();
        QVERIFY(!bar.isVisible());
    }

    void pageFieldAcceptsLogicalNumbers() {
        std::printf("OT_FEATURE:view-08\n");
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf"), 0));
        DocumentView* doc = w.currentDocument();
        StatusBar& bar = w.status();
        QTRY_COMPARE(bar.pageField()->text(), QStringLiteral("i"));
        QCOMPARE(bar.pageTotal()->text(), QStringLiteral("(1 из 4)"));
        QVERIFY(!bar.previousButton()->isEnabled());
        // Ctrl+Shift+N — фокус в поле, текст выделен; «iv» → четвёртая.
        QTest::keyClick(&w, Qt::Key_N, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(bar.pageField()->hasFocus());
        QVERIFY(bar.pageField()->hasSelectedText());
        bar.pageField()->setText(QStringLiteral("iv"));
        QTest::keyClick(bar.pageField(), Qt::Key_Return);
        QTRY_COMPARE(doc->view().currentPage(), 3);
        QTRY_COMPARE(bar.pageField()->text(), QStringLiteral("iv"));
        QVERIFY(!bar.nextButton()->isEnabled());
        // «3» — логической «3» нет: третья страница.
        QVERIFY(bar.goToInput(QStringLiteral("3")));
        QTRY_COMPARE(doc->view().currentPage(), 2);
        QTRY_COMPARE(bar.pageField()->text(), QStringLiteral("iii"));
        // Нет такой — поле возвращает текущую.
        QSignalSpy rejected(&bar, &StatusBar::pageInputRejected);
        QVERIFY(!bar.goToInput(QStringLiteral("zz")));
        QCOMPARE(rejected.size(), 1);
        QCOMPARE(doc->view().currentPage(), 2);
        QCOMPARE(bar.pageField()->text(), QStringLiteral("iii"));
        // ◀ ▶
        bar.previousButton()->click();
        QTRY_COMPARE(doc->view().currentPage(), 1);
        bar.nextButton()->click();
        QTRY_COMPARE(doc->view().currentPage(), 2);
        // Без логических номеров — физические.
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(StatusBar::kLogicalKey), false);
        bar.setDocument(doc);
        QCOMPARE(bar.pageField()->text(), QStringLiteral("3"));
        QCOMPARE(bar.pageTotal()->text(), QStringLiteral("из 4"));
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(StatusBar::kLogicalKey), true);
    }

    void zoomLayoutAndRotation() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("view/layout-mixed.pdf"), 0));
        ot::ui::PageView& v = w.currentDocument()->view();
        StatusBar& bar = w.status();
        emit bar.zoomBox()->zoomRequested(200);
        QTRY_COMPARE(qRound(v.zoomPercent()), 200);
        v.setZoomPercent(150);
        QTRY_COMPARE(qRound(bar.zoomBox()->zoomPercent()), 150);
        bar.zoomBox()->zoomOutButton()->click();
        QTRY_VERIFY(v.zoomPercent() < 150);
        bar.layoutBar()->setCurrentIndex(0);
        QCOMPARE(v.pageMode(), ot::ui::PageMode::SinglePage);
        bar.layoutBar()->setCurrentIndex(2);
        QCOMPARE(v.pageMode(), ot::ui::PageMode::FacingContinuous);
        v.setPageMode(ot::ui::PageMode::Continuous);
        QTRY_COMPARE(bar.layoutBar()->currentIndex(), 1);
        bar.rotateCwButton()->click();
        QCOMPARE(v.viewRotation(), 90);
        bar.rotateCcwButton()->click();
        bar.rotateCcwButton()->click();
        QCOMPARE(v.viewRotation(), 270);
    }

    void indicators() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        StatusBar& bar = w.status();
        QVERIFY(w.openFile(fixture("sig/signed-bb.pdf")));
        QTRY_VERIFY(bar.signedChip()->isVisible());
        QVERIFY(!bar.pdfaChip()->isVisible());
        QVERIFY(!bar.readOnlyChip()->isVisible());
        QVERIFY(w.openFile(fixture("pick/bfo/pdfa2-6-1-5-bfo-t01-pass.pdf")));
        QTRY_VERIFY(bar.pdfaChip()->isVisible());
        QVERIFY(!bar.signedChip()->isVisible());
        const QStringList images = QDir(fixture("images")).entryList({QStringLiteral("*.png")}, QDir::Files);
        QVERIFY(!images.isEmpty());
        QVERIFY(w.openFile(fixture("images/") + images.front()));
        QTRY_VERIFY(bar.readOnlyChip()->isVisible());
        QCOMPARE(bar.readOnlyChip()->text(), QStringLiteral("Только чтение"));
    }

    void backgroundTaskProgressAndCancel() {
        MainWindow w;
        showActive(w);
        StatusBar& bar = w.status();
        int cancelled = 0;
        ot::BackgroundTasks& tasks = ot::BackgroundTasks::shared();
        const quint64 id = tasks.start(QStringLiteral("Распознавание текста"), [&cancelled] { ++cancelled; });
        QTRY_VERIFY(bar.isVisible());  // и на Главной, пока идёт задача
        QVERIFY(bar.tasksIndicator()->isVisible());
        QCOMPARE(bar.taskLabel()->text(), QStringLiteral("Распознавание текста"));
        QCOMPARE(bar.taskProgress()->maximum(), 0);  // прогресс неизвестен — бегущая полоса
        tasks.setProgress(id, 40);
        QCOMPARE(bar.taskProgress()->maximum(), 100);
        QCOMPARE(bar.taskProgress()->value(), 40);
        const quint64 second = tasks.start(QStringLiteral("Сохранение"));
        QVERIFY(bar.taskLabel()->text().contains(QStringLiteral("ещё 1")));
        bar.taskCancel()->click();
        QCOMPARE(cancelled, 1);
        tasks.finish(id);
        QCOMPARE(bar.taskLabel()->text(), QStringLiteral("Сохранение"));
        QVERIFY(!bar.taskCancel()->isEnabled());  // без отмены
        tasks.finish(second);
        QVERIFY(!bar.tasksIndicator()->isVisible());
        QVERIFY(!bar.isVisible());  // Главная без задач
    }

    void initialViewOnOpen() {
        std::printf("OT_FEATURE:view-47\n");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        // /OpenAction [стр. 2 /Fit] и /DisplayDocTitle.
        QVERIFY(w.openFile(fixture("view/nav.pdf")));
        QTRY_COMPARE(w.currentDocument()->view().currentPage(), 1);
        QVERIFY(w.currentDocument()->view().fitMode().has_value());
        QCOMPARE(w.windowTitle(), QStringLiteral("Навигация: слои, вложения, назначения — АкроБатя"));
        // Явная страница сильнее /OpenAction.
        w.closeDocument(w.currentIndex());
        QVERIFY(w.openFile(fixture("view/nav.pdf"), 3));
        QTRY_COMPARE(w.currentDocument()->view().currentPage(), 3);
        // /PageMode /UseOutlines — панель закладок.
        w.navPane().closePanel();
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QTRY_VERIFY(w.navPane().isPanelOpen());
        QCOMPARE(w.navPane().currentPane(), QStringLiteral("bookmarks"));
        QCOMPARE(w.windowTitle(), QStringLiteral("report.pdf — АкроБатя"));
    }
};

OT_UI_TEST(StatusBarTest, "status_bar");
#include "status_bar_test.moc"
