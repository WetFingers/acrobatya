// ui.measure_tools — «Измерить» (план 5.5, п. 2; эталон 3.4 «Измерения», 6.2 пп. 3 и 14; comment-29,
// comment-30) на плане этажа tests/fixtures/annots/measure-viewport.pdf (/VP «План» 1 cm = 1 m, «Узел А» 1 in
// = 10 ft): инструмент в ToolRegistry с панелью (расстояние, периметр, площадь, масштаб, калибровка,
// привязка); два щелчка у углов стены — расстояние по масштабу документа, концы привязаны к вершинам, подпись
// «15,52 м»; периметр — двойным щелчком, площадь — Enter (Backspace убирает вершину, правая кнопка — конец);
// вне экранов /VP — масштаб пользователя; Shift — 45°, «Ортогонально» — 90°; окно «Сведения об измерении»
// показывает значение и «из документа»; «Масштаб и точность» пишет measuring/scale, калибровка по отрезку 2
// см = 10 м даёт 1 см = 5 м; «Установки → Измерения (2D)»; «Измерить ▾» панели комментариев включает тот же
// инструмент; только чтение — кнопки серые.
#include <QAction>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

#include "core/annot/measure.h"
#include "core/util/features.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/measure_ops.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/measuring_page.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/measure/measure_info_pane.h"
#include "ui/tools/measure/measure_mode.h"
#include "ui/tools/measure/measure_scale_dialog.h"
#include "ui/tools/measure/measure_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/tool_button.h"

using ot::Settings;
using ot::measure::Kind;
using ot::measure::Measurement;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::MeasureAction;
using ot::ui::MeasureMode;
using ot::ui::MeasureScaleDialog;
using ot::ui::MeasureTool;
using ot::ui::PageView;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;
namespace keys = ot::settings;

namespace {

constexpr double kPageHeight = 841.8898;
constexpr double kCm = 72.0 / 2.54;
constexpr double kPlanUnitsPerPoint = 1.0 / kCm;  // «1 cm = 1 m»
double top(double pdfY) { return kPageHeight - pdfY; }

QString fixture() { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/annots/measure-viewport.pdf"); }
QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-measure-tools"); }

MeasureTool& measureTool() {
    return *static_cast<MeasureTool*>(ToolRegistry::instance().find(QStringLiteral("measure")));
}

DocumentView* openDoc(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture()));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(50);
        doc->view().goToPage(0);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint at(const PageView& v, double x, double y, int page = 0) {
    return v.pageTransform(page).map(QPointF(x, y)).toPoint();
}

// Инструмент включён действием action, привязки и экраны /VP первой страницы готовы.
MeasureMode* startTool(DocumentView& doc, MeasureAction action) {
    ToolRegistry::instance().activate(QStringLiteral("measure"), &doc);
    measureTool().start(action);
    MeasureMode& mode = MeasureTool::modeOf(doc.view());
    (void)QTest::qWaitFor([&] { return mode.snapIndex(0) != nullptr && mode.viewports(0) != nullptr; },
                          10000);
    return mode.snapIndex(0) != nullptr ? &mode : nullptr;
}

std::vector<Measurement> measurements(DocumentView& doc) { return ot::measure::list(doc.session()).result(); }

// Измерения, которых нет в фикстуре (свои).
std::vector<Measurement> ours(DocumentView& doc) {
    std::vector<Measurement> out;
    for (const Measurement& m : measurements(doc)) {
        if (m.ref.nm.rfind("measure-", 0) != 0) {
            out.push_back(m);
        }
    }
    return out;
}

// Масштаб измерения из файла (/R и точность).
ot::measure::Scale scaleOf(const Measurement& m) {
    return ot::measure::parseRatio(m.ratio, m.precision).value_or(ot::measure::Scale{});
}

void setSnap(MeasureMode& mode, bool snap, bool ortho = false) {
    ot::measure::MeasureSettings s = mode.settings();
    s.snap = snap;
    s.ortho = ortho;
    mode.setSettings(s);
}

}  // namespace

class MeasureToolsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() {
        ToolRegistry::instance().deactivate();
        Settings s;
        for (const char* key :
             {keys::kMeasureScale.name, keys::kMeasurePrecision.name, keys::kMeasureSnap.name,
              keys::kMeasureOrtho.name, keys::kMeasureLineEnds.name, keys::kMeasureShowInfo.name,
              keys::kMeasureUseDocumentScale.name}) {
            s.reset(key);
        }
    }

    void toolAndPanel() {
        ot::ui::Tool* tool = ToolRegistry::instance().find(QStringLiteral("measure"));
        QVERIFY(tool != nullptr);
        QVERIFY(!tool->isStub());
        QCOMPARE(tool->title(), QStringLiteral("Измерить"));
        QVERIFY(tool->panel() != nullptr);
        for (const MeasureAction a : {MeasureAction::Distance, MeasureAction::Perimeter, MeasureAction::Area,
                                      MeasureAction::Calibrate}) {
            QVERIFY2(measureTool().actionButton(a) != nullptr, qPrintable(MeasureMode::actionTitle(a)));
        }
        QVERIFY(measureTool().scaleButton() != nullptr);
        QVERIFY(measureTool().snapBox()->isChecked());
        QVERIFY(!measureTool().orthoBox()->isChecked());
        QCOMPARE(MeasureMode::scaleText(ot::measure::Scale{}), QStringLiteral("1 см = 1 см"));
    }

    void distanceUsesDocumentScaleAndSnaps() {
        OT_FEATURE("comment-29");
        OT_FEATURE("comment-30");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        MeasureMode* mode = startTool(*doc, MeasureAction::Distance);
        QVERIFY(mode != nullptr);
        PageView& v = doc->view();
        QCOMPARE(v.tools().tool(), ViewToolId::Measure);
        QVERIFY(measureTool().actionButton(MeasureAction::Distance)->isChecked());
        // Нижняя стена: углы (80, 340) и (520, 340) генератора; щелчки — мимо углов на 2–3 px.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 80, top(340)) + QPoint(3, -2));
        QCOMPARE(mode->points().size(), std::size_t{1});
        QVERIFY(std::abs(mode->points().front().x() - 80) < 0.05);
        QVERIFY(std::abs(mode->points().front().y() - top(340)) < 0.05);
        QTest::mouseMove(v.viewport(), at(v, 300, top(345)));
        QVERIFY(mode->reading().active);
        QVERIFY(mode->reading().documentScale);
        QVERIFY(mode->infoPanel() != nullptr && mode->infoPanel()->isVisible());
        QVERIFY(mode->infoPanel()->valueLabel()->text().endsWith(QStringLiteral(" м")));
        QVERIFY(mode->infoPanel()->scaleLabel()->text().contains(QStringLiteral("из документа")));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 520, top(340)) + QPoint(-2, 2));
        QTRY_COMPARE(ours(*doc).size(), std::size_t{1});
        const Measurement m = ours(*doc).front();
        QCOMPARE(m.kind, Kind::Distance);
        QVERIFY(m.hasValue);
        QVERIFY2(std::abs(m.value - 440 * kPlanUnitsPerPoint) < 1e-3, qPrintable(QString::number(m.value)));
        QCOMPARE(QString::fromStdString(m.unit), QStringLiteral("m"));
        QCOMPARE(QString::fromStdString(m.caption), QStringLiteral("15,52 м"));
        QCOMPARE(QString::fromStdString(m.ratio), QStringLiteral("1 cm = 1 m"));
        QVERIFY(std::abs(m.points[1].x - 520) < 0.05);
        // Шаг истории — «Отменить: расстояние».
        QTRY_COMPARE(doc->session().undoState().undoName, QStringLiteral("Add distance"));
        // Инструмент остаётся: следующее измерение.
        QCOMPARE(v.tools().tool(), ViewToolId::Measure);
        QVERIFY(mode->points().empty());
    }

    void perimeterAreaAndKeys() {
        OT_FEATURE("comment-29");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        MeasureMode* mode = startTool(*doc, MeasureAction::Perimeter);
        QVERIFY(mode != nullptr);
        setSnap(*mode, false);
        PageView& v = doc->view();
        // Периметр 100 + 100 пт (внутри «Плана»): двойной щелчок в последней вершине.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 120, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 220, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 220, 400));
        QTest::mouseDClick(v.viewport(), Qt::LeftButton, {}, at(v, 220, 400));
        QTRY_COMPARE(ours(*doc).size(), std::size_t{1});
        Measurement p = ours(*doc).front();
        QCOMPARE(p.kind, Kind::Perimeter);
        QCOMPARE(p.points.size(), std::size_t{3});
        // Точки — с точностью до пикселя при 50 % (1,5 пт): значение — по поставленным точкам.
        QVERIFY(std::abs(ot::measure::pathLength(p.points) - 200) < 4);
        QVERIFY(std::abs(p.value - ot::measure::pathLength(p.points) * kPlanUnitsPerPoint) < 1e-6);
        QCOMPARE(QString::fromStdString(p.caption), MeasureMode::formatValue(p.value, scaleOf(p), false));

        // Площадь: 5 вершин, Backspace убирает лишнюю, Enter — конец.
        measureTool().start(MeasureAction::Area);
        QCOMPARE(mode->action(), MeasureAction::Area);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 320, 200));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 420, 200));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 420, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 320, 300));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 500, 450));
        QCOMPARE(mode->points().size(), std::size_t{5});
        QTest::keyClick(v.viewport(), Qt::Key_Backspace);
        QCOMPARE(mode->points().size(), std::size_t{4});
        QTest::keyClick(v.viewport(), Qt::Key_Return);
        QTRY_COMPARE(ours(*doc).size(), std::size_t{2});
        const auto all = ours(*doc);
        const auto area =
            std::find_if(all.begin(), all.end(), [](const Measurement& m) { return m.kind == Kind::Area; });
        QVERIFY(area != all.end());
        QVERIFY(std::abs(ot::measure::polygonArea(area->points) - 100 * 100) < 600);
        QVERIFY(std::abs(area->value - ot::measure::polygonArea(area->points) * kPlanUnitsPerPoint *
                                           kPlanUnitsPerPoint) < 1e-6);
        QCOMPARE(QString::fromStdString(area->unit), QStringLiteral("sq m"));
        QVERIFY(QString::fromStdString(area->caption).endsWith(QStringLiteral(" м²")));

        // Правая кнопка заканчивает; Esc бросает.
        measureTool().start(MeasureAction::Perimeter);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 120, 450));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 200, 450));
        QTest::mouseClick(v.viewport(), Qt::RightButton, {}, at(v, 200, 450));
        QTRY_COMPARE(ours(*doc).size(), std::size_t{3});
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 120, 480));
        QCOMPARE(mode->points().size(), std::size_t{1});
        QTest::keyClick(v.viewport(), Qt::Key_Escape);
        QVERIFY(mode->points().empty());
        QCOMPARE(ours(*doc).size(), std::size_t{3});
    }

    void userScaleOutsideViewports() {
        OT_FEATURE("comment-30");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        Settings().set(keys::kMeasureScale, QStringLiteral("1 cm = 10 m"));
        Settings().set(keys::kMeasurePrecision, 1);
        measureTool().settingsChanged();
        MeasureMode* mode = startTool(*doc, MeasureAction::Distance);
        QVERIFY(mode != nullptr);
        setSnap(*mode, false);
        PageView& v = doc->view();
        // Вне «Плана» и «Узла А»: 100 пт = 3,5278 см → 35,3 м.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 360, 700));
        QVERIFY(!mode->reading().documentScale);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 460, 700));
        QTRY_COMPARE(ours(*doc).size(), std::size_t{1});
        const Measurement m = ours(*doc).front();
        QVERIFY(std::abs(ot::measure::pathLength(m.points) - 100) < 2);
        QVERIFY2(std::abs(m.value - ot::measure::pathLength(m.points) * 10 / kCm) < 1e-6,
                 qPrintable(QString::number(m.value)));
        QCOMPARE(QString::fromStdString(m.caption), MeasureMode::formatValue(m.value, scaleOf(m), false));
        QVERIFY(QString::fromStdString(m.caption).startsWith(QStringLiteral("35,")));
        QCOMPARE(QString::fromStdString(m.ratio), QStringLiteral("1 cm = 10 m"));
        // «Узел А» — футы документа.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 150, 650));
        QVERIFY(mode->reading().documentScale);
        QCOMPARE(mode->reading().scale.realUnit, ot::measure::Unit::Ft);
        QTest::keyClick(v.viewport(), Qt::Key_Escape);
        // Масштаб документа выключен — масштаб пользователя и там.
        Settings().set(keys::kMeasureUseDocumentScale, false);
        measureTool().settingsChanged();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 150, 650));
        QVERIFY(!mode->reading().documentScale);
        QCOMPARE(mode->reading().scale.realUnit, ot::measure::Unit::M);
        QTest::keyClick(v.viewport(), Qt::Key_Escape);
    }

    void shiftAndOrthoConstrain() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        MeasureMode* mode = startTool(*doc, MeasureAction::Perimeter);
        QVERIFY(mode != nullptr);
        setSnap(*mode, false);
        mode->addPoint(0, QPointF(360, 640));
        const QPointF free = mode->adjust(0, QPointF(460, 670), {});
        QCOMPARE(free, QPointF(460, 670));
        const QPointF shifted = mode->adjust(0, QPointF(460, 670), Qt::ShiftModifier);
        QVERIFY(std::abs(shifted.y() - 640) < 0.01);  // 16,7° → 0°
        const QPointF diag = mode->adjust(0, QPointF(460, 730), Qt::ShiftModifier);
        QVERIFY(std::abs((diag.x() - 360) - (diag.y() - 640)) < 0.01);  // 42° → 45°
        setSnap(*mode, false, true);
        const QPointF ortho = mode->adjust(0, QPointF(380, 740), {});
        QVERIFY(std::abs(ortho.x() - 360) < 0.01);
        QVERIFY(std::abs(ortho.y() - 740) < 0.01);
        // С привязкой вершина важнее ограничений.
        setSnap(*mode, true, true);
        bool snapped = false;
        const QPointF corner = mode->adjust(0, QPointF(122, top(212) + 3), {}, &snapped);
        QVERIFY(snapped);
        QVERIFY(std::abs(corner.x() - 120) < 0.05);
        QVERIFY(std::abs(corner.y() - top(212)) < 0.05);
        QVERIFY(mode->cancel());
    }

    void scaleDialogAndCalibration() {
        OT_FEATURE("comment-30");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        MeasureMode* mode = startTool(*doc, MeasureAction::Distance);
        QVERIFY(mode != nullptr);
        MeasureScaleDialog* dialog = measureTool().showScaleDialog();
        QVERIFY(dialog != nullptr);
        QTRY_VERIFY(dialog->isVisible());
        QVERIFY(!dialog->calibrating());
        ot::measure::Scale tenMeters;
        tenMeters.realValue = 10;
        tenMeters.realUnit = ot::measure::Unit::M;
        tenMeters.precision = 1;
        dialog->fields()->setScale(tenMeters);
        QTest::mouseClick(dialog->okButton(), Qt::LeftButton);
        QTRY_VERIFY(!dialog->isVisible());
        QCOMPARE(Settings().get(keys::kMeasureScale), QStringLiteral("1 cm = 10 m"));
        QCOMPARE(Settings().get(keys::kMeasurePrecision), 1);
        QVERIFY(measureTool().scaleLabel()->text().startsWith(QStringLiteral("1 см = 10 м")));
        QCOMPARE(mode->settings().scale.realValue, 10.0);

        // Калибровка: отрезок 2 см на странице — 10 м → 1 см = 5 м.
        setSnap(*mode, false);
        measureTool().start(MeasureAction::Calibrate);
        QSignalSpy requested(mode, &MeasureMode::calibrationRequested);
        mode->addPoint(0, QPointF(360, 700));
        mode->addPoint(0, QPointF(360 + 2 * kCm, 700));
        QCOMPARE(requested.count(), 1);
        MeasureScaleDialog* calibration = nullptr;
        QTRY_VERIFY((calibration = w.findChild<MeasureScaleDialog*>()) != nullptr &&
                    calibration->isVisible());
        QVERIFY(calibration->calibrating());
        QCOMPARE(calibration->segmentLabel()->text(), QStringLiteral("2,00 см"));
        calibration->knownLength()->setValue(10);
        ot::ui::ScaleFields::selectUnit(calibration->knownUnit(), ot::measure::Unit::M);
        QVERIFY(std::abs(calibration->scale().realValue - 5) < 1e-6);
        QCOMPARE(calibration->scale().realUnit, ot::measure::Unit::M);
        QTest::mouseClick(calibration->okButton(), Qt::LeftButton);
        QTRY_COMPARE(Settings().get(keys::kMeasureScale), QStringLiteral("1 cm = 5 m"));
        // Калибровка пометку не ставит.
        QCOMPARE(ours(*doc).size(), std::size_t{0});
    }

    void preferencesPage() {
        MainWindow w;
        w.resize(1440, 900);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        ot::ui::PreferencesDialog* d = w.showPreferences(QStringLiteral("measuring"));
        QVERIFY(d != nullptr);
        auto* page = qobject_cast<ot::ui::MeasuringPage*>(d->page(QStringLiteral("measuring")));
        QVERIFY(page != nullptr);
        QCOMPARE(page->title(), QStringLiteral("Измерения (2D)"));
        QVERIFY(page->documentScaleBox()->isChecked());
        QVERIFY(page->snapBox()->isChecked());
        page->orthoBox()->setChecked(true);
        page->lineEndsBox()->setCurrentIndex(page->lineEndsBox()->findData(QStringLiteral("tick")));
        page->leaderLengthBox()->setValue(6);
        page->captionBox()->setCurrentIndex(1);
        page->infoBox()->setChecked(false);
        d->accept();
        const ot::measure::MeasureSettings s = ot::measure::settings(Settings());
        QVERIFY(s.ortho);
        QCOMPARE(s.lineEnds, ot::measure::LineEnds::Tick);
        QCOMPARE(s.leaderLength, 6.0f);
        QVERIFY(s.captionTop);
        QVERIFY(!s.showInfo);
        QCOMPARE(ot::measure::lineEnding(s.lineEnds), ot::annot::LineEnding::Butt);
        // Инструмент перечитал Установки.
        QTRY_VERIFY(measureTool().orthoBox()->isChecked());
        Settings settings;
        settings.reset(keys::kMeasureLeaderLength.name);
        settings.reset(keys::kMeasureCaption.name);
    }

    void commentPanelMeasureMenu() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        ToolRegistry::instance().activate(QStringLiteral("comment"), doc);
        auto* comment =
            static_cast<ot::ui::CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
        auto* button = comment->panel()->findChild<ot::ui::ToolButton*>(QStringLiteral("commentMeasure"));
        QVERIFY(button != nullptr);
        QVERIFY(button->isEnabled());
        QMenu* menu = button->menu();
        QVERIFY(menu != nullptr);
        QCOMPARE(menu->actions().size(), 3);
        QCOMPARE(menu->actions().at(2)->text(), QStringLiteral("Площадь"));
        menu->actions().at(2)->trigger();
        QCOMPARE(ToolRegistry::instance().active(), static_cast<ot::ui::Tool*>(&measureTool()));
        QCOMPARE(doc->view().tools().tool(), ViewToolId::Measure);
        QCOMPARE(MeasureTool::modeOf(doc->view()).action(), MeasureAction::Area);
    }

    void readOnlyLocks() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w);
        QVERIFY(doc != nullptr);
        QVERIFY(startTool(*doc, MeasureAction::Distance) != nullptr);
        doc->setReadOnly(true);
        QTRY_VERIFY(!measureTool().lockReason().isEmpty());
        QVERIFY(!measureTool().actionButton(MeasureAction::Distance)->isEnabled());
        QVERIFY(measureTool().lockLabel()->isVisible());
        QVERIFY(doc->view().tools().tool() != ViewToolId::Measure);
        doc->setReadOnly(false);
        QTRY_VERIFY(measureTool().lockReason().isEmpty());
        QVERIFY(measureTool().actionButton(MeasureAction::Distance)->isEnabled());
    }
};

OT_UI_TEST(MeasureToolsTest, "measure_tools");
#include "measure_tools_test.moc"
