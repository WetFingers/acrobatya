// ui.tool_panel — левая панель и QuickBar в окне (план 2.3; эталон 6.1; view-30, cloud-38 основа): геометрия
// 48/320/48/36 (глобальная панель, левая, свёрнутая, вкладки), Shift+F4 сворачивает и разворачивает, «Все
// инструменты» возвращает список, на Главной панели нет; звёздочка строки закрепляет; QuickBar — живые
// Выделение, Рука, Масштаб рамкой и серые будущие с подсказкой, связь с инструментом вида, порядок из
// settings.ini, клавиши в подсказках; сцены all-tools, all-tools-collapsed, quick-bar.
#include <QAction>
#include <QDir>
#include <QSettings>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "support/ui_test.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/quick_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/shots/scenes.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/tool_button.h"

using ot::ui::MainWindow;
using ot::ui::QuickBar;
using ot::ui::ToolPanel;

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

class ToolPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-tool_panel"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void geometryAndShiftF4() {
        MainWindow w;
        showActive(w);
        QVERIFY(!w.toolPanel().isVisible());  // на Главной левой панели нет
        QVERIFY(w.openFile(fixture("demo/report.pdf")));
        ToolPanel& panel = w.toolPanel();
        QTRY_VERIFY(panel.isVisible());
        const QPoint topLeft = panel.mapTo(&w, QPoint(0, 0));
        QCOMPARE(w.globalBar().height(), 48);
        QCOMPARE(w.tabs().height(), 36);
        QCOMPARE(topLeft, QPoint(0, 48));
        QCOMPARE(panel.width(), 320);
        QCOMPARE(panel.height(), 900 - 48 - 32);  // строка состояния 32 — во всю ширину (2.4)
        QTest::keyClick(&w, Qt::Key_F4, Qt::ShiftModifier);
        QVERIFY(panel.isCollapsed());
        QTRY_COMPARE(panel.width(), 48);
        auto* doc = w.currentDocument();
        QTRY_COMPARE(doc->mapTo(&w, QPoint(0, 0)).x(), 48);
        QTest::keyClick(&w, Qt::Key_F4, Qt::ShiftModifier);
        QVERIFY(!panel.isCollapsed());
        QTRY_COMPARE(panel.width(), 320);
        QTRY_COMPARE(doc->mapTo(&w, QPoint(0, 0)).x(), 320);
        w.showHome();
        QVERIFY(!panel.isVisible());
        QTest::mouseClick(w.globalBar().allToolsButton(), Qt::LeftButton);
        QVERIFY(!w.isHomeShown());
        QVERIFY(panel.isVisible());
        QVERIFY(panel.showsList());
    }

    void starPinsTool() {
        ToolPanel panel(ot::ui::ToolRegistry::instance());
        panel.show();
        ot::ui::ListRow* scan = panel.row(QStringLiteral("scan"));
        QVERIFY(scan != nullptr);
        scan->setEnabled(true);  // заглушка серая: звёздочку проверяем на включённой строке
        scan->setPinnable(true);
        scan->pinButton()->click();
        QTRY_COMPARE(ot::ui::ToolRegistry::instance().pinned(), QStringList{QStringLiteral("scan")});
        QTRY_COMPARE(panel.rows().first()->property("toolId").toString(), QStringLiteral("scan"));
        ot::ui::ToolRegistry::instance().setPinned(QStringLiteral("scan"), false);
    }

    void quickBarDrivesViewTools() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(fixture("demo/comments.pdf")));
        ot::ui::DocumentView* doc = w.currentDocument();
        auto* bar = qobject_cast<QuickBar*>(doc->floatingBar());
        QVERIFY(bar != nullptr);
        QVERIFY(bar->isVisible());
        QCOMPARE(bar->order(), QuickBar::defaultOrder());
        for (const char* live : {"select", "hand", "zoom"}) {
            QVERIFY2(bar->button(QLatin1String(live))->isEnabled(), live);
        }
        for (const char* future : {"note", "highlight", "draw", "text", "sign"}) {
            QVERIFY2(!bar->button(QLatin1String(future))->isEnabled(), future);
        }
        QCOMPARE(bar->button(QStringLiteral("note"))->toolTip(),
                 QStringLiteral("«Записка» появится в версии 0.5"));
        QCOMPARE(bar->current(), QStringLiteral("select"));
        bar->button(QStringLiteral("hand"))->click();
        QCOMPARE(doc->view().tools().tool(), ot::ui::ViewToolId::Hand);
        doc->view().tools().setTool(ot::ui::ViewToolId::Loupe);
        QCOMPARE(bar->current(), QStringLiteral("zoom"));
        // По центру у нижнего края вида, внутри него.
        const QRect viewRect = doc->view().geometry();
        const QRect barRect = bar->geometry();
        QVERIFY(viewRect.contains(barRect));
        QVERIFY(qAbs(barRect.center().x() - viewRect.center().x()) <= 1);
        QVERIFY(viewRect.bottom() - barRect.bottom() < 40);
        QCOMPARE(bar->button(QStringLiteral("hand"))->toolTip(), QStringLiteral("Рука"));
        bar->setSingleKeyHints(true);
        QCOMPARE(bar->button(QStringLiteral("hand"))->toolTip(), QStringLiteral("Рука (H)"));
        std::printf("OT_FEATURE:cloud-38\n");
    }

    void quickBarOrderFromIni() {
        {
            QSettings ini(ot::app::settingsPath(), QSettings::IniFormat);
            ini.setValue(QLatin1String(QuickBar::kOrderKey),
                         QStringList{QStringLiteral("hand"), QStringLiteral("|"), QStringLiteral("select")});
        }
        QuickBar bar;
        QCOMPARE(bar.order(),
                 (QStringList{QStringLiteral("hand"), QStringLiteral("|"), QStringLiteral("select")}));
        QSettings(ot::app::settingsPath(), QSettings::IniFormat).remove(QLatin1String(QuickBar::kOrderKey));
    }

    void scenesWritePng() {
        QTemporaryDir dir;
        ot::ui::ShotOptions o;
        o.outDir = dir.path();
        QCOMPARE(ot::ui::runShots({QStringLiteral("all-tools"), QStringLiteral("all-tools-collapsed"),
                                   QStringLiteral("quick-bar")},
                                  o),
                 0);
        for (const char* n : {"all-tools.png", "all-tools-collapsed.png", "quick-bar.png"}) {
            QCOMPARE(QImage(QDir(dir.path()).filePath(QLatin1String(n))).size(), QSize(1440, 900));
        }
    }
};

OT_UI_TEST(ToolPanelTest, "tool_panel");
#include "tool_panel_test.moc"
