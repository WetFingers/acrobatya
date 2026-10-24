// ui.tool_registry — реестр инструментов (план 2.3; эталон 6.2, 6.5; view-30): 16 инструментов в порядке 6.2
// с иконками 6.5, сейчас все — заглушки с этапом (серые, «появится в версии 0.N», включить нельзя);
// DummyTool, зарегистрированный в тесте, включается — панель показывает его виджет, deactivate возвращает
// список; смена вкладки переводит инструмент; настоящий инструмент под тем же id сменяет заглушку на её
// месте; закреплённые — первыми, список — в settings.ini.
#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QtTest/QtTest>

#include <memory>

#include "services/app_info.h"
#include "support/ui_test.h"
#include "ui/shell/tool_panel.h"
#include "ui/theme/icons.h"
#include "ui/tools/tool_registry.h"
#include "ui/widgets/list_row.h"

using ot::ui::Tool;
using ot::ui::ToolRegistry;

namespace {

class DummyTool : public Tool {
public:
    explicit DummyTool(QString id = QStringLiteral("dummy")) : id_(std::move(id)) {}
    ~DummyTool() override { delete panel_; }
    QString id() const override { return id_; }
    QString title() const override { return QStringLiteral("Dummy"); }
    QString icon() const override { return QStringLiteral("star"); }
    int stage() const override { return 2; }
    void activate(ot::ui::DocumentView& view) override {
        ++activated;
        lastView = &view;
    }
    void deactivate() override { ++deactivated; }
    QWidget* panel() override {
        if (panel_ == nullptr) {
            panel_ = new QLabel(QStringLiteral("dummy panel"));
            panel_->setObjectName(QStringLiteral("dummyPanel"));
        }
        return panel_;
    }
    int activated = 0;
    int deactivated = 0;
    ot::ui::DocumentView* lastView = nullptr;

private:
    QString id_;
    QLabel* panel_ = nullptr;
};

const char* const kTitles[] = {
    "Редактировать PDF",
    "Упорядочить страницы",
    "Добавить комментарии",
    "Заполнить и подписать",
    "Подготовить форму",
    "Создать PDF",
    "Объединить файлы",
    "Экспорт PDF",
    "Защитить",
    "Сжать PDF",
    "Использовать сертификат",
    "Сканировать и OCR",
    "Печать",
    "Измерить",
    "Специальные возможности",
    "Использовать JavaScript",
};
const int kStages[] = {3, 7, 5, 5, 6, 7, 7, 7, 7, 9, 4, 8, 2, 5, 8, 6};

}  // namespace

class ToolRegistryTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-tool_registry"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void sixteenToolsInOrder() {
        ToolRegistry& r = ToolRegistry::instance();
        const auto tools = r.tools();
        QCOMPARE(static_cast<int>(tools.size()), 16);
        QCOMPARE(ToolRegistry::builtinOrder().size(), 16);
        for (int i = 0; i < 16; ++i) {
            const Tool* t = tools[static_cast<std::size_t>(i)];
            QCOMPARE(t->id(), ToolRegistry::builtinOrder().at(i));
            QCOMPARE(t->title(), QString::fromUtf8(kTitles[i]));
            QCOMPARE(t->stage(), kStages[i]);
            // С 2.6 «Печать» — настоящий инструмент (tools/print/print_tool.h), прочие — заглушки.
            const bool live = t->id() == QLatin1String("print");
            QVERIFY2(t->isStub() != live, qPrintable(t->id()));
            QCOMPARE(t->icon(), QLatin1String(ot::ui::toolIconName(t->id().toStdString())));
            QVERIFY(ot::ui::Icons::exists(t->icon()));
            if (!live) {
                QVERIFY(!r.activate(t->id(), nullptr));  // заглушку не включить
            }
        }
        QCOMPARE(r.active(), nullptr);
    }

    void panelListsStubsGrey() {
        ot::ui::ToolPanel panel(ToolRegistry::instance());
        panel.show();
        const auto rows = panel.rows();
        QCOMPARE(rows.size(), 16);
        for (int i = 0; i < rows.size(); ++i) {
            QCOMPARE(rows[i]->property("toolId").toString(), ToolRegistry::builtinOrder().at(i));
            QCOMPARE(rows[i]->text(), QString::fromUtf8(kTitles[i]));
            QCOMPARE(rows[i]->isEnabled(), ToolRegistry::builtinOrder().at(i) == QLatin1String("print"));
        }
        QCOMPARE(panel.row(QStringLiteral("edit"))->toolTip(),
                 QStringLiteral("«Редактировать PDF» появится в версии 0.3"));
        QCOMPARE(panel.row(QStringLiteral("measure"))->toolTip(),
                 QStringLiteral("«Измерить» появится в версии 0.5"));
        QCOMPARE(panel.railButtons().size(), 16);
        std::printf("OT_FEATURE:view-30\n");
    }

    void dummyToolShowsItsPanel() {
        ToolRegistry& r = ToolRegistry::instance();
        auto* dummy = static_cast<DummyTool*>(r.add(std::make_unique<DummyTool>()));
        ot::ui::ToolPanel panel(r);
        panel.resize(320, 800);
        panel.show();
        QCOMPARE(panel.rows().size(), 17);
        QCOMPARE(panel.rows().last()->property("toolId").toString(), QStringLiteral("dummy"));
        QVERIFY(panel.row(QStringLiteral("dummy"))->isEnabled());
        QVERIFY(panel.showsList());
        QVERIFY(r.activate(QStringLiteral("dummy"), nullptr));
        QCOMPARE(r.active(), dummy);
        QVERIFY(!panel.showsList());
        QCOMPARE(panel.currentPanel(), dummy->panel());
        QVERIFY(dummy->panel()->isVisible());
        QCOMPARE(dummy->activated, 0);  // без вкладки — только панель
        r.deactivate();
        QVERIFY(panel.showsList());
        QCOMPARE(dummy->deactivated, 1);
        QCOMPARE(dummy->panel()->parent(), nullptr);  // виджет вернулся инструменту
        // «◀ Все инструменты» в панели инструмента — то же.
        QVERIFY(r.activate(QStringLiteral("dummy"), nullptr));
        panel.showList();
        QCOMPARE(r.active(), nullptr);
        QVERIFY(panel.showsList());
        QVERIFY(r.remove(QStringLiteral("dummy")));
        QCOMPARE(panel.rows().size(), 16);
    }

    void realToolReplacesStubInPlace() {
        ToolRegistry& r = ToolRegistry::instance();
        r.add(std::make_unique<DummyTool>(QStringLiteral("measure")));
        QCOMPARE(static_cast<int>(r.tools().size()), 16);
        QCOMPARE(r.tools()[13]->id(), QStringLiteral("measure"));
        QVERIFY(!r.tools()[13]->isStub());
        QVERIFY(r.activate(QStringLiteral("measure"), nullptr));
        ot::ui::registerBuiltinTools(r);  // заглушки обратно (и выключение активного)
        QCOMPARE(r.active(), nullptr);
        QVERIFY(r.find(QStringLiteral("measure"))->isStub());
        QVERIFY(!r.find(QStringLiteral("print"))->isStub());  // «Печать» — настоящий (2.6)
    }

    void pinnedGoFirstAndPersist() {
        ToolRegistry& r = ToolRegistry::instance();
        ot::ui::ToolPanel panel(r);
        panel.show();
        r.setPinned(QStringLiteral("print"), true);
        r.setPinned(QStringLiteral("scan"), true);
        QCOMPARE(r.pinned(), (QStringList{QStringLiteral("print"), QStringLiteral("scan")}));
        QCOMPARE(r.listed().front()->id(), QStringLiteral("print"));
        QCOMPARE(panel.rows().at(1)->property("toolId").toString(), QStringLiteral("scan"));
        QVERIFY(panel.rows().at(0)->isPinned());
        {
            const QSettings ini(ot::app::settingsPath(), QSettings::IniFormat);
            QCOMPARE(ini.value(QLatin1String(ToolRegistry::kPinnedKey)).toStringList(),
                     (QStringList{QStringLiteral("print"), QStringLiteral("scan")}));
        }
        r.setPinned(QStringLiteral("print"), false);
        r.setPinned(QStringLiteral("scan"), false);
        QCOMPARE(panel.rows().at(0)->property("toolId").toString(), QStringLiteral("edit"));
        QVERIFY(r.pinned().isEmpty());
    }
};

OT_UI_TEST(ToolRegistryTest, "tool_registry");
#include "tool_registry_test.moc"
