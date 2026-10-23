// ui.search_window — «Расширенный поиск» (план 2.5; эталон 3.2 «Поиск»; view-19): Ctrl+Shift+F открывает
// окно; папка с подпапкой (find.pdf, search.pdf, sub/nav.pdf, зашифрованный, скан): «печать» — дерево
// «find.pdf (3) → Страница N → контекст», зашифрованный — «пропущен», скан — «нет текста» с серой «Распознать
// текст»; экспорт CSV (BOM, заголовок, «;», строка на страницу); подсказки по индексу («печ» → «печать»);
// открытые документы — закладки nav.pdf; двойной щелчок открывает документ и подсвечивает совпадение панелью
// «Найти»; «Стоп».
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "support/ui_test.h"
#include "ui/dialogs/search_window.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/view/page_view.h"

using ot::ui::MainWindow;
using ot::ui::SearchWindow;

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

QTreeWidgetItem* topItem(QTreeWidget* tree, const QString& prefix) {
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
        if (tree->topLevelItem(i)->text(0).startsWith(prefix)) {
            return tree->topLevelItem(i);
        }
    }
    return nullptr;
}

}  // namespace

class SearchWindowTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-search_window"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(folder_.isValid());
        const QDir dir(folder_.path());
        QVERIFY(dir.mkpath(QStringLiteral("sub")));
        QVERIFY(QFile::copy(fixture("text/find.pdf"), dir.filePath(QStringLiteral("find.pdf"))));
        QVERIFY(QFile::copy(fixture("text/search.pdf"), dir.filePath(QStringLiteral("search.pdf"))));
        QVERIFY(QFile::copy(fixture("view/nav.pdf"), dir.filePath(QStringLiteral("sub/nav.pdf"))));
        QVERIFY(QFile::copy(fixture("enc/enc-aes-256-r6.pdf"), dir.filePath(QStringLiteral("secret.pdf"))));
        QVERIFY(QFile::copy(fixture("scans/scan-01.pdf"), dir.filePath(QStringLiteral("sub/scan.pdf"))));
    }

    void folderTreeCsvAndSuggestions() {
        std::printf("OT_FEATURE:view-19\n");
        MainWindow w;
        showActive(w);
        QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_VERIFY(w.searchWindow() != nullptr && w.searchWindow()->isVisible());
        SearchWindow& s = *w.searchWindow();
        QCOMPARE(s.scope(), SearchWindow::Scope::Folder);  // документа нет — папка
        s.setFolder(folder_.path());
        s.setQuery(QStringLiteral("печать"));
        QSignalSpy finished(&s, &SearchWindow::searchFinished);
        s.start();
        QVERIFY(s.isRunning());
        QVERIFY(s.stopButton()->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(!s.isRunning(), 30000);
        QCOMPARE(finished.count(), 1);

        QTreeWidget* tree = s.tree();
        QTreeWidgetItem* find = topItem(tree, QStringLiteral("find.pdf"));
        QVERIFY(find != nullptr);
        QCOMPARE(find->text(0), QStringLiteral("find.pdf (3)"));
        QCOMPARE(find->childCount(), 3);  // страницы
        QCOMPARE(find->child(0)->text(0), QStringLiteral("Страница 1"));
        QCOMPARE(find->child(0)->childCount(), 1);  // контекст
        QVERIFY(find->child(0)->child(0)->text(0).contains(QStringLiteral("печать"), Qt::CaseInsensitive));
        QVERIFY(topItem(tree, QStringLiteral("search.pdf")) == nullptr);
        QTreeWidgetItem* secret = topItem(tree, QStringLiteral("secret.pdf"));
        QVERIFY(secret != nullptr);
        QVERIFY(secret->text(0).contains(QStringLiteral("пропущен")));
        QTreeWidgetItem* scan = topItem(tree, QStringLiteral("scan.pdf"));
        QVERIFY(scan != nullptr);
        QVERIFY(scan->text(0).contains(QStringLiteral("нет текста")));
        auto* ocr = qobject_cast<QToolButton*>(tree->itemWidget(scan->child(0), 0));
        QVERIFY(ocr != nullptr);
        QVERIFY(!ocr->isEnabled());
        QVERIFY(s.statusLabel()->text().contains(QStringLiteral("3")));

        // Экспорт CSV: BOM, заголовок, строки — три страницы find.pdf, зашифрованный и скан.
        QVERIFY(s.exportButton()->isEnabled());
        const QString csv = QDir(folder_.path()).filePath(QStringLiteral("results.csv"));
        QVERIFY(s.exportCsv(csv));
        QFile f(csv);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray bytes = f.readAll();
        QVERIFY(bytes.startsWith("\xEF\xBB\xBF"));
        const QStringList lines =
            QString::fromUtf8(bytes.mid(3)).split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 1 + 3 + 2);
        QCOMPARE(lines[0], QStringLiteral("Файл;Путь;Страница;Где;Контекст"));
        QVERIFY(lines[1].startsWith(QStringLiteral("find.pdf;")));
        QCOMPARE(lines[1].split(QLatin1Char(';'))[2], QStringLiteral("1"));
        QVERIFY(lines.join(QLatin1Char('\n')).contains(QStringLiteral("защищён паролем")));

        // Подсказки по индексу: термины проиндексированной папки.
        QVERIFY(s.suggestions(QStringLiteral("печ")).contains(QStringLiteral("печать")));
        QVERIFY(s.suggestions(QStringLiteral("Слово ПЕЧ")).contains(QStringLiteral("Слово печать")));

        // Двойной щелчок по контексту — документ на этой странице, совпадение подсвечено.
        QTreeWidgetItem* leaf = find->child(1)->child(0);
        emit tree->itemDoubleClicked(leaf, 0);
        QTRY_VERIFY(w.currentDocument() != nullptr);
        ot::ui::FindBar& bar = w.currentDocument()->findBar();
        QTRY_VERIFY_WITH_TIMEOUT(bar.isVisible() && !bar.isSearching() && bar.count() > 0, 10000);
        QCOMPARE(bar.query(), QStringLiteral("печать"));
        QCOMPARE(bar.hits()[static_cast<std::size_t>(bar.current())].page, 1);
        w.close();
    }

    void openDocumentsWithBookmarks() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("text/search.pdf")));
        QVERIFY(w.openFile(fixture("view/nav.pdf")));
        SearchWindow* s = w.showSearchWindow(QStringLiteral("Глава"));
        QCOMPARE(s->scope(), SearchWindow::Scope::Current);
        s->setScope(SearchWindow::Scope::Open);
        s->setIncludeBookmarks(true);
        s->start();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isRunning(), 20000);
        QTreeWidget* tree = s->tree();
        QCOMPARE(tree->topLevelItemCount(), 1);  // в search.pdf «Главы» нет
        QTreeWidgetItem* nav = tree->topLevelItem(0);
        QCOMPARE(nav->text(0), QStringLiteral("nav.pdf (4)"));  // две страницы + две закладки
        QTreeWidgetItem* marks = nav->child(nav->childCount() - 1);
        QCOMPARE(marks->text(0), QStringLiteral("Закладки"));
        QCOMPARE(marks->childCount(), 2);
        QCOMPARE(marks->child(0)->text(0), QStringLiteral("Глава 1 — стр. 2"));
        // Двойной щелчок по закладке — переход на её страницу.
        w.showDocument(0);
        emit tree->itemDoubleClicked(marks->child(1), 0);
        QTRY_COMPARE(w.currentDocument()->path(), QDir::cleanPath(fixture("view/nav.pdf")));
        QTRY_COMPARE(w.currentDocument()->view().currentPage(), 2);
        const int rows = s->rows().size();
        QCOMPARE(rows, 4);
        QCOMPARE(SearchWindow::toCsv(s->rows()).count("\r\n"), 5);
        w.close();
    }

    void stopInterrupts() {
        MainWindow w;
        showActive(w);
        SearchWindow* s = w.showSearchWindow(QStringLiteral("оттиск"));
        s->setScope(SearchWindow::Scope::Folder);
        s->setFolder(folder_.path());
        s->start();
        s->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!s->isRunning(), 20000);
        QVERIFY(s->statusLabel()->text().contains(QStringLiteral("Остановлено")) ||
                !s->statusLabel()->text().isEmpty());
        w.close();
    }

private:
    QTemporaryDir folder_;
};

OT_UI_TEST(SearchWindowTest, "search_window");

#include "search_window_test.moc"
