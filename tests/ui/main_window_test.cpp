// ui.main_window — QTest offscreen: окно создаётся, заголовок и меню на русском, Главная видна,
// снимок не пустой, документ открывается и показывает страницу.
#include <QDir>
#include <QLabel>
#include <QMenuBar>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "services/document_session.h"
#include "ui/app/cli.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { ot::ui::applyBaseTheme(QStringLiteral("light")); }

    void showsHomeWithRussianMenus() {
        ot::ui::MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QCOMPARE(w.windowTitle(), QStringLiteral("АкроБатя"));
        QStringList menus;
        for (QAction* a : w.menuBar()->actions()) {
            menus << a->text().remove(QLatin1Char('&'));
        }
        QCOMPARE(menus,
                 (QStringList{QStringLiteral("Файл"), QStringLiteral("Вид"), QStringLiteral("Справка")}));
        auto* home = w.findChild<QWidget*>(QStringLiteral("homePage"));
        QVERIFY(home != nullptr);
        QVERIFY(home->isVisible());
        const QPixmap shot = w.grab();
        QCOMPARE(shot.size(), QSize(1440, 900));
        std::printf("OT_FEATURE:view-29\n");
    }

    void opensPdfAndShowsPage() {
        const QString fixture = QStringLiteral(OTTISK_SOURCE_DIR "/tests/data/mini/blank-a4.pdf");
        if (!QFile::exists(fixture)) {
            QSKIP("tests/data/mini/blank-a4.pdf ещё не сгенерирован");
        }
        ot::ui::MainWindow w;
        w.show();
        QSignalSpy opened(w.session(), &ot::DocumentSession::opened);
        QVERIFY(w.openFile(fixture));
        QCOMPARE(opened.count(), 1);
        auto* image = w.findChild<QLabel*>(QStringLiteral("pageImage"));
        QVERIFY(image != nullptr);
        QVERIFY(!image->pixmap().isNull());
        QVERIFY(w.windowTitle().endsWith(QStringLiteral(" — АкроБатя")));
    }

    void shotsSceneWritesPng() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QCOMPARE(
            ot::ui::runShots({QStringLiteral("home")}, dir.path(), QSize(800, 600), QStringLiteral("light")),
            0);
        QImage img(QDir(dir.path()).filePath(QStringLiteral("home.png")));
        QCOMPARE(img.size(), QSize(800, 600));
        ot::ui::applyBaseTheme(QStringLiteral("light"));
    }

    void cliParsesServiceModes() {
        const auto o =
            ot::ui::parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("--shots"),
                              QStringLiteral("home,all"), QStringLiteral("--out"), QStringLiteral("x"),
                              QStringLiteral("--size"), QStringLiteral("800x600"), QStringLiteral("--smoke"),
                              QStringLiteral("8"), QStringLiteral("файл.pdf")});
        QVERIFY(o.error.isEmpty());
        QCOMPARE(o.shots, (QStringList{QStringLiteral("home"), QStringLiteral("all")}));
        QCOMPARE(o.shotsOut, QStringLiteral("x"));
        QCOMPARE(o.shotsSize, QSize(800, 600));
        QCOMPARE(o.smokeSeconds, 8);
        QCOMPARE(o.files, QStringList{QStringLiteral("файл.pdf")});
        QVERIFY(!ot::ui::parseCli({QStringLiteral("x"), QStringLiteral("--bogus")}).error.isEmpty());
    }
};

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
