// ui.commands — реестр команд, меню и клавиши (план 2.3; эталон 6.8; view-32 ядро, view-51): у каждой команды
// уникальный id, конфликтов клавиш нет; одна клавиша у двух команд — только при разных when, и режим забирает
// её у команды окна (Ctrl+Shift+E — «Извлечь» и «стоп» чтения вслух); меню — из реестра (Файл, Правка, Вид,
// Инструменты, Подпись, Окно, Справка), «Меню ▾» — те же меню, заглушки серые с подсказкой; F9 — строка меню,
// F8 — все панели; однобуквенные ускорители выключены по умолчанию и включаются настройкой; клавиши вида не
// срабатывают в поле поиска; переназначение из settings.ini; сцена menu-open.
#include <QAction>
#include <QDir>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QSettings>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/commands.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/quick_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/tool_button.h"

using ot::Command;
using ot::CommandRegistry;
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

QAction* action(MainWindow& w, const char* id) { return w.findChild<QAction*>(QLatin1String(id)); }

QStringList titles(const QList<QAction*>& actions) {
    QStringList out;
    for (QAction* a : actions) {
        if (!a->isSeparator()) {
            out << a->text().remove(QLatin1Char('&'));
        }
    }
    return out;
}

}  // namespace

class CommandsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-commands"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void idsUniqueAndNoConflicts() {
        MainWindow w;
        const CommandRegistry& r = w.commands();
        QSet<QString> ids;
        for (const Command* c : r.all()) {
            QVERIFY2(!ids.contains(c->id), qPrintable(c->id));
            ids.insert(c->id);
            QVERIFY2(c->id.contains(QLatin1Char('.')), qPrintable(c->id));  // группа.действие
            QVERIFY2(!c->title.isEmpty(), qPrintable(c->id));
        }
        QVERIFY(r.count() > 60);
        QVERIFY2(r.conflicts().isEmpty(),
                 qPrintable(r.conflicts().isEmpty() ? QString()
                                                    : r.conflicts().first().ids.join(QLatin1Char(','))));
        QCOMPARE(r.shortcuts(QStringLiteral("doc.open")),
                 QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+O"))});
        QCOMPARE(r.shortcuts(QStringLiteral("doc.close")),
                 (QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+W")),
                                      QKeySequence(QStringLiteral("Ctrl+F4"))}));
        QCOMPARE(r.find(QStringLiteral("view.firstPage"))->when, QStringLiteral("view"));
        std::printf("OT_FEATURE:view-32\n");
    }

    void conflictOnlyWithDifferentWhen() {
        CommandRegistry r;
        int extracted = 0;
        int stopped = 0;
        const QKeySequence key(QStringLiteral("Ctrl+Shift+E"));
        r.add({.id = QStringLiteral("pages.extract"),
               .title = QStringLiteral("x"),
               .shortcuts = {key},
               .run = [&] { ++extracted; }});
        r.add({.id = QStringLiteral("readAloud.stop"),
               .title = QStringLiteral("y"),
               .shortcuts = {key},
               .run = [&] { ++stopped; },
               .when = QStringLiteral("readAloud.active")});
        QVERIFY(r.conflicts().isEmpty());
        QCOMPARE(r.effectiveShortcuts(QStringLiteral("pages.extract")), QList<QKeySequence>{key});
        QVERIFY(r.effectiveShortcuts(QStringLiteral("readAloud.stop")).isEmpty());
        QVERIFY(!r.trigger(QStringLiteral("readAloud.stop")));
        r.setContextActive(QStringLiteral("readAloud.active"), true);
        QVERIFY(r.effectiveShortcuts(QStringLiteral("pages.extract")).isEmpty());  // приоритет у чтения
        QCOMPARE(r.effectiveShortcuts(QStringLiteral("readAloud.stop")), QList<QKeySequence>{key});
        QVERIFY(r.trigger(QStringLiteral("readAloud.stop")));
        QCOMPARE(stopped, 1);
        r.add({.id = QStringLiteral("other"), .title = QStringLiteral("z"), .shortcuts = {key}});
        QCOMPARE(r.conflicts().size(), 1);
        QCOMPARE(r.conflicts().first().ids,
                 (QStringList{QStringLiteral("pages.extract"), QStringLiteral("other")}));
        // Заглушка этапа не выполняется.
        r.add({.id = QStringLiteral("later"),
               .title = QStringLiteral("l"),
               .run = [&] { ++extracted; },
               .stage = 5});
        QVERIFY(!r.trigger(QStringLiteral("later")));
        QCOMPARE(extracted, 0);
    }

    void menusFromRegistry() {
        MainWindow w;
        showActive(w);
        QStringList bar;
        for (QAction* a : w.menuBar()->actions()) {
            bar << a->text().remove(QLatin1Char('&'));
        }
        const QStringList expected{QStringLiteral("Файл"),    QStringLiteral("Правка"),
                                   QStringLiteral("Вид"),     QStringLiteral("Инструменты"),
                                   QStringLiteral("Подпись"), QStringLiteral("Окно"),
                                   QStringLiteral("Справка")};
        QCOMPARE(bar, expected);
        auto* main = w.findChild<QMenu*>(QStringLiteral("mainMenu"));
        QVERIFY(main != nullptr);
        QCOMPARE(w.globalBar().menuButton()->menu(), main);
        QCOMPARE(titles(main->actions()), expected);
        auto* tools = w.findChild<QMenu*>(QStringLiteral("menu.tools"));
        QStringList toolTitles = titles(tools->actions());
        QVERIFY(toolTitles.contains(QStringLiteral("Редактировать PDF")));
        QVERIFY(toolTitles.contains(QStringLiteral("Использовать JavaScript")));
        QVERIFY(action(w, "tools.edit")->isEnabled());  // 3.4 — настоящий инструмент
        QVERIFY(!action(w, "tools.organize")->isEnabled());
        QCOMPARE(action(w, "tools.organize")->toolTip(),
                 QStringLiteral("«Упорядочить страницы» появится в версии 0.7"));
        QVERIFY(!action(w, "doc.print")->isEnabled());  // «Печать» (2.6) — на Главной документа нет
        QCOMPARE(action(w, "doc.open")->shortcut(), QKeySequence(QStringLiteral("Ctrl+O")));
        QVERIFY(!action(w, "doc.close")->isEnabled());  // на Главной документа нет
        // F9 — строка меню.
        QVERIFY(!w.menuBar()->isVisible());
        QTest::keyClick(&w, Qt::Key_F9);
        QTRY_VERIFY(w.menuBar()->isVisible());
        QTest::keyClick(&w, Qt::Key_F9);
        QTRY_VERIFY(!w.menuBar()->isVisible());
        // Команда по id.
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        QVERIFY(action(w, "doc.close")->isEnabled());
        const float before = w.currentDocument()->view().zoom();
        QVERIFY(w.commands().trigger(QStringLiteral("view.zoomIn")));
        QVERIFY(w.currentDocument()->view().zoom() > before);
        std::printf("OT_FEATURE:view-51\n");
    }

    void singleKeysAreASetting() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        ot::ui::PageView& view = w.currentDocument()->view();
        view.setFocus();
        QVERIFY(!w.singleKeyAccelerators());
        QVERIFY(action(w, "tool.hand")->shortcuts().isEmpty());
        QTest::keyClick(view.viewport(), Qt::Key_H);
        QCOMPARE(view.tools().tool(), ot::ui::ViewToolId::Select);
        w.setSingleKeyAccelerators(true);
        QCOMPARE(action(w, "tool.hand")->shortcut(), QKeySequence(Qt::Key_H));
        QTest::keyClick(view.viewport(), Qt::Key_H);
        QCOMPARE(view.tools().tool(), ot::ui::ViewToolId::Hand);
        QTest::keyClick(view.viewport(), Qt::Key_V);
        QCOMPARE(view.tools().tool(), ot::ui::ViewToolId::Select);
        auto* quick = qobject_cast<ot::ui::QuickBar*>(w.currentDocument()->floatingBar());
        QCOMPARE(quick->button(QStringLiteral("hand"))->toolTip(), QStringLiteral("Рука (H)"));
        QVERIFY(QSettings(ot::app::settingsPath(), QSettings::IniFormat)
                    .value(QLatin1String(CommandRegistry::kSingleKeyKey))
                    .toBool());
        {
            MainWindow again;  // настройка читается при запуске
            QVERIFY(again.singleKeyAccelerators());
        }
        w.setSingleKeyAccelerators(false);
        QVERIFY(action(w, "tool.hand")->shortcuts().isEmpty());
    }

    void viewKeysOnlyInDocumentAndF8() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/report.pdf"), 3));
        ot::ui::PageView& view = w.currentDocument()->view();
        QTRY_COMPARE(view.currentPage(), 3);
        w.globalBar().search()->setFocus();
        QTest::keyClick(w.globalBar().search(), Qt::Key_Home);
        QCOMPARE(view.currentPage(), 3);  // Home в поле поиска — к началу строки, не к первой странице
        view.setFocus();
        QTest::keyClick(view.viewport(), Qt::Key_Home);
        QTRY_COMPARE(view.currentPage(), 0);
        // F8 — все панели.
        QVERIFY(w.toolPanel().isVisible());
        QVERIFY(w.currentDocument()->floatingBar()->isVisible());
        QTest::keyClick(&w, Qt::Key_F8);
        QVERIFY(w.panelsHidden());
        QVERIFY(!w.toolPanel().isVisible());
        QVERIFY(!w.currentDocument()->floatingBar()->isVisible());
        QTest::keyClick(&w, Qt::Key_F8);
        QVERIFY(w.toolPanel().isVisible());
        QVERIFY(w.currentDocument()->floatingBar()->isVisible());
    }

    void overridesFromIni() {
        {
            QSettings ini(ot::app::settingsPath(), QSettings::IniFormat);
            ini.setValue(QStringLiteral("hotkeys/doc.open"), QStringList{QStringLiteral("Ctrl+Shift+O")});
        }
        MainWindow w;
        QCOMPARE(w.commands().effectiveShortcuts(QStringLiteral("doc.open")),
                 QList<QKeySequence>{QKeySequence(QStringLiteral("Ctrl+Shift+O"))});
        QCOMPARE(action(w, "doc.open")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+O")));
        QSettings(ot::app::settingsPath(), QSettings::IniFormat).remove(QStringLiteral("hotkeys/doc.open"));
    }

    void menuSceneWritesPng() {
        QTemporaryDir dir;
        ot::ui::ShotOptions o;
        o.outDir = dir.path();
        QCOMPARE(ot::ui::runShots({QStringLiteral("menu-open")}, o), 0);
        QCOMPARE(QImage(QDir(dir.path()).filePath(QStringLiteral("menu-open.png"))).size(), QSize(1440, 900));
    }
};

OT_UI_TEST(CommandsTest, "commands");
#include "commands_test.moc"
