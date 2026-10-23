// ui.home — Главная (план 2.3; эталон 3.18, 4.7; view-29, view-31): пустое состояние и «Создать PDF ▾»
// (заглушки с тостом), недавние после открытия (новые сверху, закреплённые — первыми, не больше 50),
// «Избранное» после звёздочки, «Убрать из списка», поиск по имени и сортировка, «Краткие сведения», миниатюры
// только для видимых карточек и кеш thumbs\<sha1>.png, «Файлы» — только *.pdf папки, последний вид файла
// восстанавливается при открытии, сцены home-*.
#include <QDir>
#include <QFile>
#include <QListView>
#include <QMenu>
#include <QScrollBar>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

#include "services/recent.h"
#include "services/state_db.h"
#include "services/thumbnails.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/home/brief_pane.h"
#include "ui/shell/home/home_page.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/widgets/empty_state.h"

using ot::RecentFiles;
using ot::ThumbnailCache;
using ot::ui::HomePage;
using ot::ui::MainWindow;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

HomePage& homeOf(MainWindow& w) { return *static_cast<HomePage*>(w.homePage()); }

}  // namespace

class HomeTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-home"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void init() { RecentFiles::shared().clear(); }

    void emptyStateAndCreateMenu() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        HomePage& home = homeOf(w);
        QVERIFY(home.isVisible());
        QVERIFY(home.isEmptyShown());
        QCOMPARE(home.emptyState()->text(), QStringLiteral("Откройте PDF (Ctrl+O) или перетащите файл сюда"));
        QStringList items;
        for (QAction* a : home.createMenu()->actions()) {
            items << a->text();
        }
        QCOMPARE(items.size(), 6);
        QVERIFY(items.contains(QStringLiteral("Пустая страница")));
        QSignalSpy later(&home, &HomePage::notifyLater);
        home.createMenu()->actions().first()->trigger();
        QCOMPARE(later.count(), 1);
        QCOMPARE(later.at(0).at(1).toInt(), 7);
    }

    void recentAfterOpenAndFavoritesAfterStar() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QVERIFY(w.openFile(fixture("demo/form.pdf")));
        w.showHome();
        HomePage& home = homeOf(w);
        QVERIFY(!home.isEmptyShown());
        QCOMPARE(home.itemCount(), 2);
        QVERIFY(home.pathAt(0).endsWith(QStringLiteral("form.pdf")));  // новые — сверху
        QVERIFY(home.pathAt(1).endsWith(QStringLiteral("report.pdf")));
        // «Краткие сведения»
        home.select(1);
        QVERIFY(home.brief()->isVisible());
        QCOMPARE(home.brief()->valueText(QStringLiteral("pages")), QStringLiteral("12"));
        QVERIFY(!home.brief()->valueText(QStringLiteral("size")).isEmpty());
        // Закреплённый — первым.
        home.togglePinned(1);
        QTRY_VERIFY(home.pathAt(0).endsWith(QStringLiteral("report.pdf")));
        // Звёздочка — «Избранное».
        home.showFavorites();
        QVERIFY(home.isEmptyShown());
        home.showRecent();
        home.toggleFavorite(0);
        home.showFavorites();
        QCOMPARE(home.itemCount(), 1);
        QVERIFY(home.pathAt(0).endsWith(QStringLiteral("report.pdf")));
        // Поиск по имени и «Убрать из списка».
        home.showRecent();
        home.setSearchText(QStringLiteral("FOR"));
        QCOMPARE(home.itemCount(), 1);
        home.setSearchText(QString());
        home.removeFromRecent(0);
        QCOMPARE(home.itemCount(), 1);
        QCOMPARE(RecentFiles::shared().favorites().size(), 1);  // избранное осталось
        std::printf("OT_FEATURE:view-31\n");
        std::printf("OT_FEATURE:view-29\n");
    }

    void recentKeepsFiftyAndPinned() {
        QTemporaryDir dir;
        ot::StateDb db(dir.filePath(QStringLiteral("state.sqlite")));
        RecentFiles r(db);
        r.touch(QStringLiteral("C:/a/закреплённый.pdf"), 3);
        r.setPinned(QStringLiteral("C:/a/закреплённый.pdf"), true);
        for (int i = 0; i < 60; ++i) {
            r.touch(QStringLiteral("C:/a/f%1.pdf").arg(i));
        }
        const auto list = r.recent();
        QCOMPARE(list.size(), RecentFiles::kLimit + 1);
        QCOMPARE(list.front().path, QStringLiteral("C:/a/закреплённый.pdf"));
        QCOMPARE(list.at(1).path, QStringLiteral("C:/a/f59.pdf"));
        QCOMPARE(list.front().pageCount, 3);
        QVERIFY(!r.find(QStringLiteral("C:/a/f0.pdf")).has_value());  // вытеснен
        // Тот же файл другим регистром и разделителем — та же запись.
        r.touch(QStringLiteral("c:\\A\\F10.PDF"));
        QCOMPARE(r.recent().at(1).path, QStringLiteral("c:\\A\\F10.PDF"));
        QCOMPARE(r.recent().size(), RecentFiles::kLimit + 1);
        // Последний вид.
        r.saveView(QStringLiteral("C:/a/f59.pdf"), {4, 150.0, QStringLiteral("Facing")});
        QCOMPARE(*r.lastView(QStringLiteral("C:/a/f59.pdf")),
                 (ot::LastView{4, 150.0, QStringLiteral("Facing")}));
        QVERIFY(!r.lastView(QStringLiteral("C:/a/f58.pdf")).has_value());
    }

    // Миниатюры — только для видимых карточек; прокрутка добавляет видимые; готовые лежат в
    // thumbs\<sha1>.png.
    void thumbnailsOnlyForVisible() {
        QTemporaryDir dir;
        ot::StateDb db(dir.filePath(QStringLiteral("state.sqlite")));
        RecentFiles r(db);
        const QString blank = QStringLiteral(OTTISK_SOURCE_DIR "/tests/data/mini/blank-a4.pdf");
        for (int i = 0; i < 40; ++i) {
            const QString copy = dir.filePath(QStringLiteral("файл-%1.pdf").arg(i, 2, 10, QLatin1Char('0')));
            QVERIFY(QFile::copy(blank, copy));
            r.touch(copy, 1);
        }
        ThumbnailCache thumbs(dir.filePath(QStringLiteral("thumbs")));
        HomePage home(r, thumbs);
        home.resize(1000, 520);
        home.show();
        QVERIFY(QTest::qWaitForWindowExposed(&home));
        QCOMPARE(home.itemCount(), 40);
        QListView* view = home.view();
        auto visibleCount = [view] {
            int n = 0;
            for (int i = 0; i < view->model()->rowCount(); ++i) {
                n +=
                    view->viewport()->rect().intersects(view->visualRect(view->model()->index(i, 0))) ? 1 : 0;
            }
            return n;
        };
        QTRY_VERIFY(thumbs.renders() > 0);
        const int visible = visibleCount();
        QVERIFY2(visible < 40, qPrintable(QString::number(visible)));
        QVERIFY2(thumbs.renders() <= visible,
                 qPrintable(QStringLiteral("%1 > %2").arg(thumbs.renders()).arg(visible)));
        thumbs.waitForDone();
        QTRY_VERIFY(!thumbs.get(home.pathAt(0)).isNull());
        QVERIFY(QFile::exists(thumbs.cacheFile(home.pathAt(0))));
        QCOMPARE(thumbs.get(home.pathAt(0)).width(), ThumbnailCache::kWidth);
        const int before = thumbs.renders();
        view->verticalScrollBar()->setValue(view->verticalScrollBar()->maximum());
        QTRY_VERIFY(thumbs.renders() > before);
        // Новый кеш на той же папке: миниатюра — с диска, без рендера.
        thumbs.waitForDone();
        ThumbnailCache again(dir.filePath(QStringLiteral("thumbs")));
        QVERIFY(!again.get(home.pathAt(0)).isNull());
        QCOMPARE(again.renders(), 0);
    }

    void filesShowOnlyPdf() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        HomePage& home = homeOf(w);
        home.showFolder(fixture("demo"));
        QCOMPARE(home.section(), HomePage::Section::Files);
        QTRY_VERIFY(home.itemCount() >= 4);
        QStringList names;
        for (int i = 0; i < home.itemCount(); ++i) {
            names << QFileInfo(home.pathAt(i)).fileName();
        }
        QVERIFY(names.contains(QStringLiteral("report.pdf")));
        QVERIFY(!names.contains(QStringLiteral("demo.json")));
        home.setSearchText(QStringLiteral("repo"));
        QTRY_COMPARE(home.itemCount(), 1);
        home.setSearchText(QString());
        home.showScans();
        QCOMPARE(home.section(), HomePage::Section::Scans);
        QVERIFY(home.folder().endsWith(QStringLiteral("АкроБатя/Сканы")));
    }

    void lastViewIsRestored() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(w.openFile(fixture("demo/report.pdf"), 6));
        QTRY_COMPARE(w.currentPage(), 6);
        w.currentDocument()->view().setZoomPercent(75);
        QVERIFY(w.closeDocument(0));
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QTRY_COMPARE(w.currentPage(), 6);
        QTRY_COMPARE(qRound(w.currentDocument()->view().zoomPercent()), 75);
    }

    void scenesWritePng() {
        QTemporaryDir dir;
        ot::ui::ShotOptions o;
        o.outDir = dir.path();
        QCOMPARE(ot::ui::runShots({QStringLiteral("home-empty"), QStringLiteral("home-recent"),
                                   QStringLiteral("home-files"), QStringLiteral("home")},
                                  o),
                 0);
        for (const char* n : {"home-empty.png", "home-recent.png", "home-files.png", "home.png"}) {
            QCOMPARE(QImage(QDir(dir.path()).filePath(QLatin1String(n))).size(), QSize(1440, 900));
        }
    }
};

OT_UI_TEST(HomeTest, "home");
#include "home_test.moc"
