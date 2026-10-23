// ui.panes — правая навигация в окне (план 2.4; эталон 3.1 «Навигация», 6.1, 7.3; view-10, view-11, view-15,
// view-17, view-30, view-50, security-43): полоса 44 у правого края, панель 280 (F4, одна страница за раз,
// состояние и ширина в settings.ini, на Главной и по F8 скрыта); миниатюры запрашиваются только для видимых
// строк, щелчок ведёт на страницу, текущая следует за видом, рамку области просмотра можно тащить; закладки
// PDF32000_2008 строятся лениво (первый уровень < 200 мс, дочерние — по раскрытию) и переход работает; слои
// скрываются и рендер меняется (пиксель квадрата слоя «Сетка»), «Сбросить к исходному»; вложение .exe —
// только «Сохранить», .docx — подтверждение и openFile в FakeShellLauncher, PDF — вкладка; назначения —
// список и переход; пустые состояния будущих страниц.
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTreeView>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include <cstdio>

#include "services/app_info.h"
#include "services/shell_launcher.h"
#include "support/ui_test.h"
#include "ui/panes/attachments_pane.h"
#include "ui/panes/bookmarks_pane.h"
#include "ui/panes/destinations_pane.h"
#include "ui/panes/layers_pane.h"
#include "ui/panes/thumbnails_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::NavPane;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

QString corpusFile(const char* rel) {
    const QByteArray env = qgetenv("OTTISK_CORPUS");
    const QString root =
        env.isEmpty() ? QStringLiteral("C:/Tools/ottisk/corpus") : QString::fromLocal8Bit(env);
    return root + QLatin1Char('/') + QLatin1String(rel);
}

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    QTest::qWaitForWindowActive(&w);
}

bool rendered(DocumentView* doc) {
    doc->view().viewport()->repaint();
    return doc->view().tilePainter().visibleComplete();
}

template <class Pane>
Pane* pageOf(MainWindow& w, const char* id) {
    return static_cast<Pane*>(w.navPane().page(QLatin1String(id)));
}

}  // namespace

class PanesTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-panes"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void init() {
        QSettings ini(ot::app::settingsPath(), QSettings::IniFormat);
        ini.remove(QStringLiteral("navpane"));
        ini.remove(QStringLiteral("trust"));
    }

    void railPanelGeometryAndF4() {
        std::printf("OT_FEATURE:view-30\n");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        NavPane& nav = w.navPane();
        QVERIFY(!nav.isVisible());  // на Главной навигации нет
        QVERIFY(w.openFile(fixture("view/nav.pdf")));
        QTRY_VERIFY(nav.isVisible());
        QCOMPARE(nav.width(), 44);
        QCOMPARE(nav.mapTo(&w, QPoint(0, 0)), QPoint(1440 - 44, 48));
        QVERIFY(!nav.panel()->isVisible());
        for (const char* id :
             {"thumbnails", "bookmarks", "comments", "layers", "attachments", "signatures"}) {
            QVERIFY2(nav.button(QLatin1String(id))->isVisible(), id);
        }
        QVERIFY(!nav.button(QStringLiteral("fields"))->isVisible());        // только «Подготовить форму»
        QVERIFY(!nav.button(QStringLiteral("destinations"))->isVisible());  // по правому щелчку

        QTest::keyClick(&w, Qt::Key_F4);
        QTRY_VERIFY(nav.panel()->isVisible());
        QCOMPARE(nav.currentPane(), QStringLiteral("thumbnails"));
        QTRY_COMPARE(nav.panel()->width(), 280);
        QCOMPARE(nav.panel()->mapTo(&w, QPoint(0, 0)).x(), 1440 - 44 - 280);
        QVERIFY(nav.button(QStringLiteral("thumbnails"))->isChecked());

        // Одна страница за раз: другая кнопка — другая страница; нажатая — закрыть.
        nav.button(QStringLiteral("bookmarks"))->click();
        QCOMPARE(nav.currentPane(), QStringLiteral("bookmarks"));
        QVERIFY(nav.isPanelOpen());
        QVERIFY(!nav.button(QStringLiteral("thumbnails"))->isChecked());
        nav.button(QStringLiteral("bookmarks"))->click();
        QVERIFY(!nav.isPanelOpen());
        QVERIFY(!nav.panel()->isVisible());
        QTest::keyClick(&w, Qt::Key_F4);  // F4 — последняя открытая
        QVERIFY(nav.isPanelOpen());
        QCOMPARE(nav.currentPane(), QStringLiteral("bookmarks"));
        {
            QSettings ini(ot::app::settingsPath(), QSettings::IniFormat);
            QCOMPARE(ini.value(QLatin1String(NavPane::kOpenKey)).toBool(), true);
            QCOMPARE(ini.value(QLatin1String(NavPane::kCurrentKey)).toString(), QStringLiteral("bookmarks"));
        }

        // Ширина тянется 220–480 и запоминается.
        auto* splitter = w.findChild<QSplitter*>(QStringLiteral("shellBody"));
        QVERIFY(splitter != nullptr);
        QList<int> sizes = splitter->sizes();
        splitter->setSizes({sizes[0] + sizes[1] - 600, 600});
        QTRY_COMPARE(nav.panel()->width(), 480);
        nav.setPanelWidth(nav.panel()->width());
        QCOMPARE(nav.panelWidth(), 480);

        // Правый щелчок по полосе — «Назначения».
        QMenu* menu = nav.railMenu();
        auto* dests = menu->findChild<QAction*>(QStringLiteral("navMenu.destinations"));
        QVERIFY(dests != nullptr);
        dests->trigger();
        QVERIFY(nav.button(QStringLiteral("destinations"))->isVisible());
        QCOMPARE(nav.currentPane(), QStringLiteral("destinations"));

        // F8 и Главная прячут полосу и панель.
        QTest::keyClick(&w, Qt::Key_F8);
        QVERIFY(!nav.isVisible());
        QVERIFY(!nav.panel()->isVisible());
        QTest::keyClick(&w, Qt::Key_F8);
        QVERIFY(nav.isVisible());
        QVERIFY(nav.panel()->isVisible());
        w.showHome();
        QVERIFY(!nav.isVisible());
        w.showDocument(0);
        QVERIFY(nav.isVisible());

        // Новое окно — то же состояние и ширина из settings.ini.
        MainWindow again;
        again.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(again);
        QVERIFY(again.openFile(fixture("view/nav.pdf")));
        QTRY_VERIFY(again.navPane().panel()->isVisible());
        QCOMPARE(again.navPane().currentPane(), QStringLiteral("destinations"));
        QTRY_COMPARE(again.navPane().panel()->width(), 480);
    }

    void thumbnailsOnlyVisibleAndSync() {
        std::printf("OT_FEATURE:view-10\n");
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        DocumentView* doc = w.currentDocument();
        auto* pane = pageOf<ot::ui::ThumbnailsPane>(w, "thumbnails");
        w.navPane().showPane(QStringLiteral("thumbnails"));
        const int count = doc->view().pageCount();
        QCOMPARE(count, 12);
        QTRY_COMPARE(pane->list()->model()->rowCount(), count);
        // Видимые строки получают миниатюры; невидимые не запрашиваются.
        const auto visibleRows = [pane, count] {
            QList<int> rows;
            for (int p = 0; p < count; ++p) {
                if (!pane->thumbRect(p).isEmpty()) {
                    rows << p;
                }
            }
            return rows;
        };
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                pane->list()->viewport()->repaint();
                const QList<int> rows = visibleRows();
                for (const int p : rows) {
                    if (!pane->hasThumbnail(p)) {
                        return false;
                    }
                }
                return !rows.isEmpty();
            }(),
            15000);
        const QList<int> rows = visibleRows();
        QVERIFY(rows.size() < count);
        for (const int p : pane->requestedPages()) {
            QVERIFY2(rows.contains(p), qPrintable(QStringLiteral("requested invisible page %1").arg(p)));
        }
        QVERIFY(!pane->hasThumbnail(count - 1));
        // Прокрутка к концу — запрашиваются нижние.
        pane->clearRequestLog();
        pane->list()->scrollToBottom();
        QTRY_VERIFY_WITH_TIMEOUT(
            [&] {
                pane->list()->viewport()->repaint();
                return pane->hasThumbnail(count - 1);
            }(),
            15000);
        QVERIFY(!pane->requestedPages().contains(0));
        // Щелчок — переход; вид — текущая строка.
        const QRect r5 = pane->list()->visualRect(pane->list()->model()->index(count - 2, 0));
        QTest::mouseClick(pane->list()->viewport(), Qt::LeftButton, {}, r5.center());
        QTRY_COMPARE(doc->view().currentPage(), count - 2);
        doc->view().goToPage(3);
        QTRY_COMPARE(pane->currentPage(), 3);
        QTRY_VERIFY(!pane->visibleFrame(3).isEmpty());
        // Рамка области просмотра тянется — вид прокручивается.
        doc->view().setZoomPercent(400);
        doc->view().goToPage(3);
        QTRY_VERIFY(!pane->thumbRect(3).isEmpty());
        QTRY_VERIFY(pane->visibleFrame(3).height() < 0.5);
        const QRect thumb = pane->thumbRect(3);
        const QRectF f = pane->visibleFrame(3);
        const QPoint start(qRound(thumb.x() + (f.x() + f.width() / 2) * thumb.width()),
                           qRound(thumb.y() + (f.y() + f.height() / 2) * thumb.height()));
        const int before = doc->view().verticalScrollBar()->value();
        QTest::mousePress(pane->list()->viewport(), Qt::LeftButton, {}, start);
        QTest::mouseMove(pane->list()->viewport(), start + QPoint(0, 20));
        QTest::mouseRelease(pane->list()->viewport(), Qt::LeftButton, {}, start + QPoint(0, 20));
        QVERIFY(doc->view().verticalScrollBar()->value() > before);
        // Множественный выбор и перетаскивание — список страниц.
        QCOMPARE(pane->list()->selectionMode(), QAbstractItemView::ExtendedSelection);
        QVERIFY(pane->list()->dragEnabled());
    }

    void bookmarksLazyOnSpecification() {
        std::printf("OT_FEATURE:view-11\n");
        const QString spec = corpusFile("adobe-spec/PDF32000_2008.pdf");
        if (!QFile::exists(spec)) {
            QSKIP("PDF32000_2008.pdf нет в корпусе (adobe-spec)");
        }
        MainWindow w;
        showActive(w);
        auto* pane = pageOf<ot::ui::BookmarksPane>(w, "bookmarks");
        // Спецификация — /PageMode /UseOutlines: панель закладок открывается уже при открытии файла.
        QSignalSpy levels(pane, &ot::ui::BookmarksPane::levelLoaded);
        QVERIFY(w.openFile(spec));
        DocumentView* doc = w.currentDocument();
        w.navPane().showPane(QStringLiteral("bookmarks"));
        QTRY_VERIFY(!levels.isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(rendered(doc), 20000);
        const QList<QVariant> top = levels.first();
        QVERIFY(top[0].toList().isEmpty());
        QVERIFY2(top[2].toLongLong() < 200,
                 qPrintable(QStringLiteral("first level %1 ms").arg(top[2].toLongLong())));
        std::printf("bookmarks first level: %d items, %lld ms\n", top[1].toInt(), top[2].toLongLong());
        QStandardItemModel* model = pane->model();
        QVERIFY(model->rowCount() > 10);
        // Дочерние не загружены, пока узел закрыт: у него одна строка-заглушка.
        int closed = -1;
        for (int i = 0; i < model->rowCount(); ++i) {
            const QModelIndex idx = model->index(i, 0);
            if (model->rowCount(idx) == 1 &&
                model->index(0, 0, idx).data(ot::ui::BookmarksPane::PlaceholderRole).toBool()) {
                closed = i;
                break;
            }
        }
        QVERIFY(closed >= 0);
        levels.clear();
        pane->tree()->expand(model->index(closed, 0));
        QTRY_VERIFY(!levels.isEmpty());
        const QModelIndex node = model->index(closed, 0);
        QVERIFY(model->rowCount(node) > 0);
        QVERIFY(!model->index(0, 0, node).data(ot::ui::BookmarksPane::PlaceholderRole).toBool());
        // Переход по дочерней закладке.
        const QModelIndex child = model->index(0, 0, node);
        const int before = doc->view().currentPage();
        pane->activate(child);
        QTRY_VERIFY(doc->view().currentPage() != before);
        QVERIFY(doc->view().currentPage() > 0);
    }

    void bookmarksStyleOpenAndEmpty() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf")));
        auto* pane = pageOf<ot::ui::BookmarksPane>(w, "bookmarks");
        w.navPane().showPane(QStringLiteral("bookmarks"));
        QStandardItemModel* model = pane->model();
        QTRY_COMPARE(model->rowCount(), 4);
        const QModelIndex ch1 = model->index(1, 0);
        QCOMPARE(ch1.data().toString(), QStringLiteral("Глава 1"));
        QVERIFY(ch1.data(Qt::FontRole).value<QFont>().bold());
        QCOMPARE(ch1.data(Qt::ForegroundRole).value<QBrush>().color(), QColor(204, 0, 0));
        QVERIFY(model->index(2, 0).data(Qt::FontRole).value<QFont>().italic());
        // /Count > 0 — раскрыта и загружена сама, /Count < 0 — закрыта.
        QTRY_VERIFY(pane->tree()->isExpanded(model->index(2, 0)));
        QTRY_COMPARE(model->rowCount(model->index(2, 0)), 2);
        QVERIFY(!pane->tree()->isExpanded(model->index(3, 0)));
        pane->activate(model->index(3, 0));  // «Итоги» — стр. 4
        QTRY_COMPARE(w.currentDocument()->view().currentPage(), 3);
        // Документ без закладок — пустое состояние.
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/data/mini/blank-a4.pdf")));
        QTRY_VERIFY(pane->isEmptyShown());
    }

    void layersHideAndRenderChanges() {
        std::printf("OT_FEATURE:view-15\n");
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf"), 0));
        DocumentView* doc = w.currentDocument();
        PageViewFit(doc);
        auto* pane = pageOf<ot::ui::LayersPane>(w, "layers");
        w.navPane().showPane(QStringLiteral("layers"));
        QTRY_COMPARE(pane->rows().size(), std::size_t{6});
        QCOMPARE(pane->item(5)->toolTip(0).contains(QStringLiteral("заблокирован")), true);
        QTRY_VERIFY_WITH_TIMEOUT(rendered(doc), 15000);
        const QPoint center = doc->view().pageTransform(0).map(QPointF(595.2756 / 2, 841.8898 / 2)).toPoint();
        QCOMPARE(QColor(doc->view().viewport()->grab().toImage().pixel(center)).lightness() < 40, true);

        pane->toggle(0);  // «Сетка» скрыта
        QTRY_VERIFY(!pane->rows()[0].on);
        QTRY_VERIFY_WITH_TIMEOUT(
            rendered(doc) && QColor(doc->view().viewport()->grab().toImage().pixel(center)).lightness() > 220,
            15000);
        pane->toggle(5);  // заблокирован — не меняется
        QTest::qWait(50);
        QVERIFY(pane->rows()[5].on);

        pane->resetToDefault();
        QTRY_VERIFY(pane->rows()[0].on);
        QTRY_VERIFY_WITH_TIMEOUT(
            rendered(doc) && QColor(doc->view().viewport()->grab().toImage().pixel(center)).lightness() < 40,
            15000);
        QVERIFY(!doc->isModified());  // видимость слоёв — не правка
    }

    void attachmentsBlockedConfirmAndPdf() {
        std::printf("OT_FEATURE:view-17\nOT_FEATURE:security-43\n");
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf")));
        auto* pane = pageOf<ot::ui::AttachmentsPane>(w, "attachments");
        QStringList asked;
        QStringList chosen;
        const QString saveDir = QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-panes/saved");
        QDir().mkpath(saveDir);
        bool answer = true;
        pane->setConfirm([&](const QString& name) {
            asked << name;
            return answer;
        });
        pane->setSaveChooser([&](const QString& suggested) {
            chosen << suggested;
            return saveDir + QLatin1Char('/') + QFileInfo(suggested).fileName();
        });
        w.navPane().showPane(QStringLiteral("attachments"));
        QTRY_COMPARE(pane->files().size(), std::size_t{5});
        QCOMPARE(QString::fromStdString(pane->files()[3].fileName), QStringLiteral("setup.exe"));

        // .exe — «Открыть» недоступно, двойной щелчок и openCurrent — только «Сохранить…».
        pane->select(3);
        QVERIFY(!pane->openButton()->isEnabled());
        QVERIFY(pane->saveButton()->isEnabled());
        QSignalSpy saved(pane, &ot::ui::AttachmentsPane::saved);
        pane->openCurrent();
        QVERIFY(asked.isEmpty());
        QVERIFY(shell.calls.isEmpty());
        QCOMPARE(chosen.size(), 1);
        QVERIFY(chosen[0].endsWith(QStringLiteral("setup.exe")));
        QTRY_COMPARE(saved.size(), 1);
        QFile exe(saveDir + QStringLiteral("/setup.exe"));
        QVERIFY(exe.open(QIODevice::ReadOnly));
        QVERIFY(exe.readAll().startsWith("MZ"));

        // .docx — подтверждение, отказ — ничего; согласие — копия во временной папке и openFile.
        pane->select(2);
        QVERIFY(pane->openButton()->isEnabled());
        answer = false;
        pane->openCurrent();
        QCOMPARE(asked, QStringList{QStringLiteral("report.docx")});
        QVERIFY(shell.calls.isEmpty());
        answer = true;
        pane->openCurrent();
        QCOMPARE(shell.calls.size(), 1);
        QCOMPARE(shell.calls[0].kind, QStringLiteral("openFile"));
        QVERIFY(shell.calls[0].target.endsWith(QStringLiteral("/report.docx")));
        QVERIFY(shell.calls[0].target.startsWith(ot::ui::AttachmentsPane::tempFolder()));
        QFile docx(shell.calls[0].target);
        QVERIFY(docx.open(QIODevice::ReadOnly));
        QVERIFY(docx.readAll().startsWith("PK"));

        // Настройка «не открывать вложения внешними программами».
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(ot::ui::AttachmentsPane::kAllowOpenKey), false);
        pane->select(1);
        pane->select(2);
        QVERIFY(!pane->openButton()->isEnabled());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(ot::ui::AttachmentsPane::kAllowOpenKey), true);

        // PDF — вкладкой с жёлтой полосой, без подтверждения и без оболочки.
        asked.clear();
        pane->select(0);
        const int tabs = w.documentCount();
        pane->openCurrent();
        QTRY_COMPARE(w.documentCount(), tabs + 1);
        QVERIFY(asked.isEmpty());
        QCOMPARE(shell.calls.size(), 1);
        QVERIFY(w.currentDocument()->path().endsWith(QStringLiteral("inner.pdf")));
        bool bar = false;
        for (ot::ui::MessageBar* m : w.currentDocument()->messages()) {
            bar = bar || m->objectName() == QLatin1String("attachmentBar");
        }
        QVERIFY(bar);
    }

    void destinationsListAndJump() {
        std::printf("OT_FEATURE:view-50\n");
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf"), 0));
        auto* pane = pageOf<ot::ui::DestinationsPane>(w, "destinations");
        w.navPane().showPane(QStringLiteral("destinations"));
        QTRY_COMPARE(pane->tree()->topLevelItemCount(), 4);
        QCOMPARE(pane->tree()->topLevelItem(0)->text(0), QStringLiteral("chapter1"));
        QCOMPARE(pane->tree()->topLevelItem(3)->text(0), QStringLiteral("summary"));
        pane->tree()->sortByColumn(1, Qt::AscendingOrder);
        QCOMPARE(pane->tree()->topLevelItem(0)->text(0), QStringLiteral("intro"));
        QTreeWidgetItem* summary =
            pane->tree()->findItems(QStringLiteral("summary"), Qt::MatchExactly).value(0);
        QVERIFY(summary != nullptr);
        pane->activate(summary);
        QTRY_COMPARE(w.currentDocument()->view().currentPage(), 3);
    }

    void futurePanesSayStage() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("view/nav.pdf")));
        for (const auto& [id, stage] : {std::pair{"comments", 5}, std::pair{"signatures", 4}}) {
            w.navPane().showPane(QLatin1String(id));
            auto* page = w.navPane().page(QLatin1String(id));
            bool found = false;
            for (QLabel* l : page->findChildren<QLabel*>()) {
                found = found || l->text().contains(QStringLiteral("появится в версии 0.%1").arg(stage));
            }
            QVERIFY2(found, id);
        }
    }

private:
    static void PageViewFit(DocumentView* doc) {
        doc->view().fit(ot::FitMode::Page);
        doc->view().goToPage(0);
    }
};

OT_UI_TEST(PanesTest, "panes");
#include "panes_test.moc"
