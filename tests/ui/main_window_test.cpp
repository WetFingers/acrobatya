// ui.main_window — оболочка (план 2.3; эталон 6.1, 3.1 «Открытие», 4.2): Главная и глобальная панель 48 с
// вкладками 36, строка меню скрыта (F9), два файла — две вкладки, Ctrl+Tab / Ctrl+Shift+Tab, тот же файл — та
// же вкладка, средняя кнопка закрывает, правка — точка «несохранено» и «Сохранить / Не сохранять / Отмена»,
// не-PDF — только просмотр и «Сохранить как PDF», перетаскивание, --page, сеанс (state.sqlite) и его
// восстановление по настройке, ошибка открытия — неблокирующий диалог, сцены shell-*, разбор командной
// строки.
#include <QAction>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QSettings>
#include <QSignalSpy>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest/QtTest>

#include <memory>

#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/convert_ops.h"
#include "services/document_registry.h"
#include "services/document_session.h"
#include "services/session_store.h"
#include "support/ui_test.h"
#include "ui/app/cli.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"

using ot::ui::DocumentView;
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

QAction* action(MainWindow& w, const char* name) { return w.findChild<QAction*>(QLatin1String(name)); }

// Шаг истории: рамка первой аннотации на то же место (документ «изменён»).
void touch(DocumentView& view) {
    const auto annots = ot::annot::list(view.session(), 0).result();
    QVERIFY(!annots.empty());
    ot::annot::setRect(view.session(), annots.front().ref, annots.front().rect).waitForFinished();
    QTRY_VERIFY(view.isModified());
}

}  // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    // Свой каталог данных набора (настройки и сеанс; state.sqlite открыт до конца процесса — не во временной
    // папке).
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-main_window"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void showsHomeWithGlobalBar() {
        MainWindow w;
        showActive(w);
        QCOMPARE(w.windowTitle(), QStringLiteral("АкроБатя"));
        QVERIFY(w.isHomeShown());
        QVERIFY(w.homePage()->isVisible());
        QVERIFY(!w.menuBar()->isVisible());
        QCOMPARE(w.globalBar().height(), 48);
        QCOMPARE(w.globalBar().geometry().top(), 0);
        QCOMPARE(w.tabs().height(), 36);
        QStringList menus;
        for (QAction* a : w.menuBar()->actions()) {
            menus << a->text().remove(QLatin1Char('&'));
        }
        QCOMPARE(menus, (QStringList{QStringLiteral("Файл"), QStringLiteral("Правка"), QStringLiteral("Вид"),
                                     QStringLiteral("Окно"), QStringLiteral("Справка")}));
        action(w, "view.menuBar")->trigger();  // F9
        QVERIFY(w.menuBar()->isVisible());
        QTest::keyClick(&w, Qt::Key_F9);
        QTRY_VERIFY(!w.menuBar()->isVisible());
        QCOMPARE(w.grab().size(), QSize(1440, 900));
        std::printf("OT_FEATURE:view-29\n");
    }

    void twoFilesTwoTabsCtrlTab() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QVERIFY(w.openFile(fixture("demo/comments.pdf")));
        QCOMPARE(w.documentCount(), 2);
        QCOMPARE(w.currentIndex(), 1);
        QCOMPARE(w.tabs().tabText(0), QStringLiteral("report.pdf"));
        QCOMPARE(w.windowTitle(), QStringLiteral("comments.pdf — АкроБатя"));
        QTest::keyClick(&w, Qt::Key_Tab, Qt::ControlModifier);
        QCOMPARE(w.currentIndex(), 0);
        QTest::keyClick(&w, Qt::Key_Tab, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(w.currentIndex(), 1);
        QCOMPARE(w.windowTitle(), QStringLiteral("comments.pdf — АкроБатя"));
        // Тот же файл — та же вкладка.
        QVERIFY(w.openFile(QDir::toNativeSeparators(fixture("demo/report.pdf"))));
        QCOMPARE(w.documentCount(), 2);
        QCOMPARE(w.currentIndex(), 0);
        // Главная и обратно по щелчку вкладки.
        w.showHome();
        QVERIFY(w.isHomeShown());
        QCOMPARE(w.currentDocument(), nullptr);
        QTest::mouseClick(&w.tabs(), Qt::LeftButton, {}, w.tabs().tabRect(1).center());
        QCOMPARE(w.currentIndex(), 1);
        QVERIFY(!w.isHomeShown());
        // Перетаскивание вкладки меняет порядок, вид следует за id.
        w.tabs().moveTab(1, 0);
        QCOMPARE(w.document(0)->title(), QStringLiteral("comments.pdf"));
        std::printf("OT_FEATURE:view-27\n");
    }

    void middleButtonClosesTab() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QVERIFY(w.openFile(fixture("demo/form.pdf")));
        QTest::mouseClick(&w.tabs(), Qt::MiddleButton, {}, w.tabs().tabRect(0).center());
        QCOMPARE(w.documentCount(), 1);
        QCOMPARE(w.document(0)->title(), QStringLiteral("form.pdf"));
        QCOMPARE(w.registry().count(), 1);
        action(w, "doc.close")->trigger();
        QCOMPARE(w.documentCount(), 0);
        QVERIFY(w.isHomeShown());
    }

    void closeWithChangesAsks() {
        QTemporaryDir dir;
        const QString copy = dir.filePath(QStringLiteral("комментарии.pdf"));
        QVERIFY(QFile::copy(fixture("demo/comments.pdf"), copy));
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(copy));
        DocumentView* view = w.currentDocument();
        QVERIFY(!view->isModified());
        touch(*view);
        QTRY_VERIFY(w.tabs().isModified(0));
        int asked = 0;
        w.setCloseConfirm([&](DocumentView&) {
            ++asked;
            return MainWindow::CloseAnswer::Cancel;
        });
        QVERIFY(!w.closeDocument(0));
        QCOMPARE(asked, 1);
        QCOMPARE(w.documentCount(), 1);
        // Закрытие окна с правками — тот же вопрос; «Отмена» оставляет окно.
        QVERIFY(!w.close());
        QCOMPARE(asked, 2);
        QVERIFY(w.isVisible());
        // «Сохранить» — файл записан, точка ушла, вкладка закрыта.
        const qint64 before = QFileInfo(copy).size();
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Save; });
        QVERIFY(w.closeDocument(0));
        QCOMPARE(w.documentCount(), 0);
        QVERIFY(QFileInfo(copy).size() > before);  // лёгкая правка — инкрементальная запись
        // «Не сохранять» — закрыто без записи.
        QVERIFY(w.openFile(copy));
        touch(*w.currentDocument());
        const QDateTime saved = QFileInfo(copy).lastModified();
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        QVERIFY(w.closeDocument(0));
        QCOMPARE(QFileInfo(copy).lastModified(), saved);
    }

    void nonPdfIsViewOnly() {
        QTemporaryDir dir;  // раньше окна: файлы закрываются до удаления папки
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("images/multipage-g4.tif")));
        DocumentView* view = w.currentDocument();
        QVERIFY(view->isViewOnly());
        QVERIFY(view->view().pageCount() > 1);
        auto* bar = view->findChild<ot::ui::MessageBar*>(QStringLiteral("viewOnlyBar"));
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());
        QVERIFY(!action(w, "edit.undo")->isEnabled());
        // «Сохранить как PDF» — новый PDF с теми же страницами, открывается во вкладке.
        const QString out = dir.filePath(QStringLiteral("скан.pdf"));
        ot::convert::saveAsPdf(view->session(), out).waitForFinished();
        QVERIFY(w.openFile(out));
        QCOMPARE(w.documentCount(), 2);
        QVERIFY(!w.currentDocument()->isViewOnly());
        QCOMPARE(w.currentDocument()->view().pageCount(), view->view().pageCount());
        std::printf("OT_FEATURE:view-01\n");
    }

    void dropOpensFiles() {
        MainWindow w;
        showActive(w);
        auto* mime = new QMimeData();
        mime->setUrls({QUrl::fromLocalFile(fixture("demo/report.pdf")),
                       QUrl::fromLocalFile(fixture("demo/form.pdf")),
                       QUrl(QStringLiteral("https://example.org/x.pdf"))});
        QDragEnterEvent enter(QPoint(400, 400), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(400, 400), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &drop);
        QCOMPARE(w.documentCount(), 2);
        delete mime;
    }

    void pageArgumentAndOpenError() {
        QTemporaryDir dir;
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf"), 3));
        QTRY_COMPARE(w.currentPage(), 3);
        const QString junk = dir.filePath(QStringLiteral("не-pdf.pdf"));
        {
            QFile f(junk);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("this is not a pdf at all");
        }
        QVERIFY(!w.openFile(junk));
        QVERIFY(!w.errorString().isEmpty());
        auto* box = w.findChild<QMessageBox*>(QStringLiteral("openErrorBox"));
        QVERIFY(box != nullptr);
        box->close();
        QVERIFY(!w.openFile(dir.filePath(QStringLiteral("нет такого.pdf"))));
        QCOMPARE(w.documentCount(), 1);
    }

    void sessionIsSavedAndRestored() {
        {
            MainWindow w;
            showActive(w);
            QVERIFY(w.openFile(fixture("demo/report.pdf"), 2));
            QVERIFY(w.openFile(fixture("demo/form.pdf")));
            QTRY_COMPARE(w.document(0)->view().currentPage(), 2);
            w.showDocument(0);
            QVERIFY(w.close());
        }
        const ot::SessionState saved = ot::session::load();
        QCOMPARE(saved.tabs.size(), 2);
        QCOMPARE(saved.current, 0);
        QCOMPARE(saved.tabs[0].page, 2);
        QVERIFY(saved.tabs[1].path.endsWith(QStringLiteral("form.pdf")));

        QVERIFY(!MainWindow::restoreSessionEnabled());  // по умолчанию выключено, как в Acrobat
        {
            QSettings s(ot::app::settingsPath(), QSettings::IniFormat);
            s.setValue(QLatin1String(ot::session::kRestoreKey), true);
        }
        QVERIFY(MainWindow::restoreSessionEnabled());
        MainWindow again;
        showActive(again);
        again.restoreSession(saved);
        QCOMPARE(again.documentCount(), 2);
        QCOMPARE(again.currentIndex(), 0);
        QTRY_COMPARE(again.currentPage(), 2);
        std::printf("OT_FEATURE:view-31\n");
    }

    void shellScenesWritePng() {
        QTemporaryDir dir;
        ot::ui::ShotOptions o;
        o.outDir = dir.path();
        QCOMPARE(ot::ui::runShots({QStringLiteral("shell-document"), QStringLiteral("shell-two-tabs")}, o),
                 0);
        for (const char* n : {"shell-document.png", "shell-two-tabs.png"}) {
            QCOMPARE(QImage(QDir(dir.path()).filePath(QLatin1String(n))).size(), QSize(1440, 900));
        }
    }

    void cliParsesArguments() {
        const auto o = ot::ui::parseCli(
            {QStringLiteral("AcroBatya.exe"), QStringLiteral("--shots"), QStringLiteral("home,all"),
             QStringLiteral("--out"), QStringLiteral("x"), QStringLiteral("--size"),
             QStringLiteral("800x600"), QStringLiteral("--smoke"), QStringLiteral("8"),
             QStringLiteral("--page"), QStringLiteral("5"), QStringLiteral("--log"), QStringLiteral("debug"),
             QStringLiteral("--data-dir"), QStringLiteral("D:/данные"), QStringLiteral("файл.pdf"),
             QStringLiteral("второй.pdf")});
        QVERIFY(o.error.isEmpty());
        QCOMPARE(o.shots, (QStringList{QStringLiteral("home"), QStringLiteral("all")}));
        QCOMPARE(o.shotsOut, QStringLiteral("x"));
        QCOMPARE(o.shotsSize, QSize(800, 600));
        QCOMPARE(o.smokeSeconds, 8);
        QCOMPARE(o.page, 5);
        QCOMPARE(o.logLevel, QStringLiteral("debug"));
        QCOMPARE(o.dataDir, QStringLiteral("D:/данные"));
        QCOMPARE(o.files, (QStringList{QStringLiteral("файл.pdf"), QStringLiteral("второй.pdf")}));
        QVERIFY(!ot::ui::parseCli({QStringLiteral("x"), QStringLiteral("--bogus")}).error.isEmpty());
        QVERIFY(!ot::ui::parseCli({QStringLiteral("x"), QStringLiteral("--page"), QStringLiteral("0")})
                     .error.isEmpty());
        QVERIFY(!ot::ui::parseCli({QStringLiteral("x"), QStringLiteral("--log"), QStringLiteral("loud")})
                     .error.isEmpty());
    }
};

OT_UI_TEST(MainWindowTest, "main_window");
#include "main_window_test.moc"
