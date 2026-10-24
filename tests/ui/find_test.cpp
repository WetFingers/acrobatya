// ui.find — панель «Найти» (план 2.5; эталон 3.2 «Поиск»; view-18): Ctrl+F открывает панель в правом верхнем
// углу документа с фокусом в поле; find.pdf — «печать» 12 раз: подсвечены все 12, счётчик «1 из 12», F3
// обходит по кругу (12 → 1), Shift+F3 — назад; «Только целые слова» не находит «печат», «С учётом регистра» —
// 10; поиск начинается с текущей страницы; неверное выражение — чип «Неверное выражение»; Esc закрывает и
// снимает подсветку; скан без OCR — «Нет текста — распознать?» и серая «Распознать текст»; «Заменить» — 3.4
// (ui.replace).
#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QtTest/QtTest>

#include "core/util/features.h"
#include "services/app_info.h"
#include "support/ui_test.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/view/page_view.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::FindBar;
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

FindBar& openFind(MainWindow& w, const char* rel) {
    [&] {
        showActive(w);
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.currentDocument()->view().pageCount() > 0);
    }();
    return w.currentDocument()->findBar();
}

void search(FindBar& bar, const QString& text) {
    bar.field()->setText(text);
    QTest::keyClick(bar.field(), Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(!bar.isSearching(), 10000);
}

}  // namespace

class FindTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-find"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void ctrlFHighlightsTwelveAndF3Cycles() {
        std::printf("OT_FEATURE:view-18\n");
        MainWindow w;
        FindBar& bar = openFind(w, "text/find.pdf");
        QVERIFY(!bar.isVisible());
        QTest::keyClick(&w, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(bar.isVisible());
        QVERIFY(bar.field()->hasFocus());
        // Правый верхний угол области страниц.
        DocumentView* doc = w.currentDocument();
        const QRect area = doc->view().geometry();
        QVERIFY(bar.geometry().top() >= area.top());
        QVERIFY(bar.geometry().right() <= area.right());
        QTRY_VERIFY(bar.geometry().right() > area.center().x());  // место — после показа

        search(bar, QStringLiteral("печать"));
        QCOMPARE(bar.count(), 12);
        QCOMPARE(doc->view().searchHits().size(), std::size_t{12});
        QCOMPARE(bar.current(), 0);
        QCOMPARE(doc->view().currentSearchHit(), 0);
        QCOMPARE(bar.counterLabel()->text(), QStringLiteral("1 из 12"));
        for (int i = 1; i < 12; ++i) {
            QTest::keyClick(&w, Qt::Key_F3);
            QCOMPARE(bar.current(), i);
        }
        QCOMPARE(bar.counterLabel()->text(), QStringLiteral("12 из 12"));
        QCOMPARE(doc->view().currentPage(), 2);  // последнее — на третьей странице
        QTest::keyClick(&w, Qt::Key_F3);         // по кругу
        QCOMPARE(bar.current(), 0);
        QCOMPARE(doc->view().currentPage(), 0);
        QTest::keyClick(&w, Qt::Key_F3, Qt::ShiftModifier);
        QCOMPARE(bar.current(), 11);
        QTest::keyClick(&w, Qt::Key_G, Qt::ControlModifier);  // Ctrl+G — тоже «следующее»
        QCOMPARE(bar.current(), 0);
        // Enter по той же строке — следующее.
        QTest::keyClick(bar.field(), Qt::Key_Return);
        QCOMPARE(bar.current(), 1);

        // Esc закрывает панель и снимает подсветку.
        bar.field()->setFocus();
        QTest::keyClick(bar.field(), Qt::Key_Escape);
        QVERIFY(!bar.isVisible());
        QVERIFY(doc->view().searchHits().empty());
    }

    void optionsWholeWordAndCase() {
        MainWindow w;
        FindBar& bar = openFind(w, "text/find.pdf");
        bar.activate();
        ot::SearchOptions o;
        o.wholeWord = true;
        bar.setOptions(o);
        search(bar, QStringLiteral("печат"));
        QCOMPARE(bar.count(), 0);
        QCOMPARE(bar.counterLabel()->text(), QStringLiteral("Не найдено"));
        QVERIFY(!bar.nextButton()->isEnabled());
        o.wholeWord = false;
        o.caseSensitive = true;
        bar.setOptions(o);
        search(bar, QStringLiteral("печать"));
        QCOMPARE(bar.count(), 10);  // «Печать» и «ПЕЧАТЬ» — нет
        o.caseSensitive = false;
        o.regex = true;
        bar.setOptions(o);
        search(bar, QStringLiteral("("));
        QVERIFY(bar.hasError());
        QVERIFY(bar.stateChip()->isVisible());
        QCOMPARE(bar.stateChip()->tone(), ot::ui::Tone::Danger);
        // «Заменить» (3.4) — кнопка есть и включена: поле замены — по ней (ui.replace).
        QVERIFY(bar.replaceButton()->isVisible());
        QVERIFY(bar.replaceButton()->isEnabled());
        QVERIFY(!bar.replaceMode());
    }

    void startsFromCurrentPage() {
        MainWindow w;
        FindBar& bar = openFind(w, "text/find.pdf");
        DocumentView* doc = w.currentDocument();
        doc->view().goToPage(1);
        QTRY_COMPARE(doc->view().currentPage(), 1);
        bar.activate();
        search(bar, QStringLiteral("печать"));
        QCOMPARE(bar.count(), 12);
        QCOMPARE(bar.current(), 5);  // первое на второй странице
        QCOMPARE(bar.hits()[5].page, 1);
        // find() с заданным совпадением (двойной щелчок в «Расширенном поиске»).
        bar.find(QStringLiteral("печать"), std::nullopt, 2, -1);
        QTRY_VERIFY_WITH_TIMEOUT(!bar.isSearching(), 10000);
        QCOMPARE(bar.hits()[static_cast<std::size_t>(bar.current())].page, 2);
    }

    void scanWithoutTextOffersOcr() {
        MainWindow w;
        FindBar& bar = openFind(w, "scans/scan-01.pdf");
        bar.activate();
        search(bar, QStringLiteral("текст"));
        QVERIFY(bar.noText());
        QVERIFY(bar.stateChip()->isVisible());
        QVERIFY(bar.stateChip()->text().contains(QStringLiteral("распознать")));
        QVERIFY(bar.ocrButton()->isVisible());
        QVERIFY(!bar.ocrButton()->isEnabled());  // «Распознать текст» — 8.1
    }

    void globalSearchFieldOpensFindBar() {
        MainWindow w;
        FindBar& bar = openFind(w, "text/find.pdf");
        bar.find(QStringLiteral("подпись"));
        QTRY_VERIFY_WITH_TIMEOUT(!bar.isSearching(), 10000);
        QVERIFY(bar.isVisible());
        QCOMPARE(bar.count(), 1);
    }
};

OT_UI_TEST(FindTest, "find");

#include "find_test.moc"
