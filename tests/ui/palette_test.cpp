// ui.palette — палитра команд (план 2.4b; эталон 6.3, 6.8; view-32): Ctrl+Shift+P открывает палитру с
// командами реестра окна; фильтр по подстроке названия и id (без учёта регистра, каждое слово), начало
// названия — выше; ↑ ↓ по списку, Enter выполняет и закрывает; заглушки будущих этапов — в списке, но не
// выполняются; Esc закрывает.
#include <QDir>
#include <QListWidget>
#include <QtTest/QtTest>

#include "services/commands.h"
#include "support/ui_test.h"
#include "ui/shell/command_palette.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/widgets/search_field.h"

using ot::Command;
using ot::ui::CommandPalette;
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
}

}  // namespace

class PaletteTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-palette"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void matchingRules() {
        Command c;
        c.id = QStringLiteral("view.fitWidth");
        c.title = QStringLiteral("По &ширине");
        int rank = -1;
        QVERIFY(CommandPalette::matches(c, QString(), &rank));
        QCOMPARE(rank, 0);
        QVERIFY(CommandPalette::matches(c, QStringLiteral("по ш"), &rank));
        QCOMPARE(rank, 0);
        QVERIFY(CommandPalette::matches(c, QStringLiteral("ШИР"), &rank));
        QCOMPARE(rank, 1);
        QVERIFY(CommandPalette::matches(c, QStringLiteral("fitwidth"), &rank));
        QCOMPARE(rank, 2);
        QVERIFY(!CommandPalette::matches(c, QStringLiteral("высоте")));
        QVERIFY(!CommandPalette::matches(c, QStringLiteral("ширине xyz")));
        QCOMPARE(CommandPalette::plainTitle(QStringLiteral("&Установки…")), QStringLiteral("Установки"));
        QCOMPARE(CommandPalette::plainTitle(QStringLiteral("A && B")), QStringLiteral("A & B"));
    }

    void findsBySubstringAndRuns() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QTest::keyClick(&w, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
        CommandPalette* p = w.commandPalette();
        QVERIFY(p != nullptr);
        QTRY_VERIFY(p->isVisible());
        QVERIFY(p->visibleIds().size() > 40);  // весь реестр окна
        QVERIFY(!p->visibleIds().contains(QStringLiteral("app.palette")));
        // «режим чт» — «Режим чтения»; Enter включает режим и закрывает палитру.
        p->filterField()->setText(QStringLiteral("режим чт"));
        QCOMPARE(p->visibleIds().value(0), QStringLiteral("view.readMode"));
        QCOMPARE(p->currentId(), QStringLiteral("view.readMode"));
        QSignalSpy triggered(p, &CommandPalette::commandTriggered);
        QTest::keyClick(p->filterField(), Qt::Key_Return);
        QCOMPARE(triggered.count(), 1);
        QTRY_VERIFY(!p->isVisible());
        QVERIFY(w.isReadMode());
        w.setReadMode(false);
    }

    void arrowsMoveAndStubsDoNotRun() {
        MainWindow w;
        showActive(w);
        CommandPalette* p = w.showCommandPalette();
        QTRY_VERIFY(p->isVisible());
        // «Печать» — заглушка этапа 2.6 (stage 2): видна, но Enter её не выполняет.
        p->setFilter(QStringLiteral("doc.print"));
        QCOMPARE(p->visibleIds(), QStringList{QStringLiteral("doc.print")});
        QVERIFY(!p->runCurrent());
        QVERIFY(p->isVisible());
        // ↓ ↑ по кругу, не уходя из поля.
        p->setFilter(QStringLiteral("масштаб"));
        const QStringList ids = p->visibleIds();
        QVERIFY(ids.size() >= 2);
        const QString first = p->currentId();
        QTest::keyClick(p->filterField(), Qt::Key_Down);
        QVERIFY(p->currentId() != first);
        QTest::keyClick(p->filterField(), Qt::Key_Up);
        QCOMPARE(p->currentId(), first);
        QTest::keyClick(p->filterField(), Qt::Key_Up);
        QCOMPARE(p->currentId(), p->visibleIds().last());
        QCOMPARE(p->filterField()->text(), QStringLiteral("масштаб"));
        // Повторное открытие — пустой фильтр.
        QTest::keyClick(p->filterField(), Qt::Key_Escape);
        QTRY_VERIFY(!p->isVisible());
        w.showCommandPalette();
        QVERIFY(p->filterField()->text().isEmpty());
        // Установки из палитры — диалог Установок.
        p->setFilter(QStringLiteral("установки"));
        QCOMPARE(p->currentId(), QStringLiteral("app.preferences"));
        QVERIFY(p->runCurrent());
        QTRY_VERIFY(w.preferencesDialog() != nullptr && w.preferencesDialog()->isVisible());
        w.preferencesDialog()->reject();
    }
};

OT_UI_TEST(PaletteTest, "palette");
#include "palette_test.moc"
