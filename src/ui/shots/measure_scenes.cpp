// Сцены измерений (план 5.5; эталон 3.4 «Измерения», 6.2 пп. 3 и 14, 6.3 «Масштаб и точность измерений»,
// «Measurement Info»): measure-distance — план этажа (tests/fixtures/annots/measure-viewport.pdf, масштаб
// документа 1 cm = 1 m): готовое расстояние по нижней стене с подписью «15,52 м» и идущее расстояние по
// перегородке, конец привязан к вершине (квадрат), подпись значения у курсора, окно «Сведения об измерении»;
// measure-area — готовая площадь комнаты 2 и идущая площадь комнаты 1 (три вершины и резинка к курсору);
// measure-scale-dialog — «Масштаб и точность» с калибровкой по отрезку 15,52 см и заметкой о масштабе
// документа. Тёмные двойники — measure-distance-dark, measure-scale-dialog-dark.
#include <QCoreApplication>
#include <QMouseEvent>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

#include "services/document_session.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/measure/measure_mode.h"
#include "ui/tools/measure/measure_scale_dialog.h"
#include "ui/tools/measure/measure_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/number_field.h"

namespace ot::ui {

namespace measure_scene {  // не анонимное: unity-сборка сцен (как у соседей — свои имена)

constexpr const char* kDoneProperty = "measureSceneDone";
constexpr const char* kPlan = "annots/measure-viewport.pdf";
constexpr double kPageHeight = 841.8898;

using SceneStep = std::function<void(MainWindow&, DocumentView&, MeasureMode&)>;

void markDone(MainWindow& w) { w.setProperty(kDoneProperty, true); }

bool measureReady(QWidget* w) { return w->property(kDoneProperty).toBool() && sceneDocumentReady(w); }

// Точка генератора плана (y вверх от низа) → точка страницы MuPDF.
QPointF pt(double x, double pdfY) { return {x, kPageHeight - pdfY}; }

MeasureTool& sceneTool() {
    return *static_cast<MeasureTool*>(ToolRegistry::instance().find(QStringLiteral("measure")));
}

// Привязки и экраны /VP первой страницы готовы — затем then (опрос раз в 50 мс, не дольше 10 с).
void whenReady(MainWindow& w, MeasureMode& mode, std::function<void()> then, int tries = 200) {
    if (mode.snapIndex(0) != nullptr && mode.viewports(0) != nullptr) {
        then();
        return;
    }
    if (tries <= 0) {
        return;
    }
    QTimer::singleShot(50, &w, [&w, &mode, then = std::move(then), tries]() mutable {
        whenReady(w, mode, std::move(then), tries - 1);
    });
}

// Курсор над точкой страницы — как движение мыши.
void hoverAt(PageView& view, QPointF pagePoint) {
    const QPointF pos = view.pageTransform(0).map(pagePoint);
    QMouseEvent move(QEvent::MouseMove, pos, view.viewport()->mapToGlobal(pos), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QCoreApplication::sendEvent(view.viewport(), &move);
}

// Окно с планом этажа и «Измерить» (действие action); then — когда привязки и масштаб документа готовы.
std::function<QWidget*()> measureScene(MeasureAction action, SceneStep then) {
    return withMainWindow([action, then](MainWindow& w) {
        w.setProperty(kDoneProperty, false);
        w.openFile(sceneFixture(QLatin1String(kPlan)));
        QTimer::singleShot(0, &w, [&w, action, then] {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            doc->view().setZoomPercent(75);
            doc->view().goToPage(0);
            ToolRegistry::instance().activate(QStringLiteral("measure"), doc);
            sceneTool().start(action);
            MeasureMode& mode = MeasureTool::modeOf(doc->view());
            whenReady(w, mode, [&w, then, &mode] {
                if (DocumentView* d = w.currentDocument()) {
                    then(w, *d, mode);
                }
            });
        });
    });
}

// Измерение по вершинам (создаётся шагом истории), затем then.
void measured(MainWindow& w, MeasureMode& mode, const std::vector<QPointF>& points,
              std::function<void()> then) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(&mode, &MeasureMode::created, &w, [connection, then] {
        QObject::disconnect(*connection);
        then();
    });
    for (const QPointF& p : points) {
        mode.addPoint(0, p);
    }
    if (!mode.points().empty()) {
        mode.finish();
    }
}

void distanceScene(MainWindow& w, DocumentView& doc, MeasureMode& mode) {
    measured(w, mode, {pt(80, 340), pt(520, 340)}, [&w, &doc, &mode] {
        // Идёт расстояние по перегородке: начало — левый конец, курсор у правого (привязка).
        mode.addPoint(0, pt(80, 540));
        hoverAt(doc.view(), pt(297, 543));
        markDone(w);
    });
}

void areaScene(MainWindow& w, DocumentView& doc, MeasureMode& mode) {
    measured(w, mode, {pt(300, 540), pt(520, 540), pt(520, 740), pt(300, 740)}, [&w, &doc, &mode] {
        mode.addPoint(0, pt(80, 540));
        mode.addPoint(0, pt(300, 540));
        mode.addPoint(0, pt(300, 740));
        hoverAt(doc.view(), pt(84, 736));
        markDone(w);
    });
}

void scaleDialogScene(MainWindow& w, DocumentView& /*doc*/, MeasureMode& mode) {
    measure::Scale scale = mode.settings().scale;
    auto* dialog = new MeasureScaleDialog(scale, &w);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setCalibration(440);
    dialog->knownLength()->setValue(15.52);
    ScaleFields::selectUnit(dialog->knownUnit(), measure::Unit::M);
    dialog->setDocumentScale(mode.scaleAt(0, pt(200, 600)));
    dialog->resize(dialog->sizeHint().expandedTo(dialog->minimumSizeHint()));
    embedCentered(w, dialog);
    markDone(w);
}

}  // namespace measure_scene

std::vector<Scene> measureScenes() {
    std::vector<Scene> scenes;
    auto add = [&scenes](const char* name, MeasureAction action, measure_scene::SceneStep step) {
        Scene s;
        s.name = QLatin1String(name);
        s.make = measure_scene::measureScene(action, std::move(step));
        s.ready = measure_scene::measureReady;
        scenes.push_back(std::move(s));
    };
    add("measure-distance", MeasureAction::Distance, measure_scene::distanceScene);
    add("measure-area", MeasureAction::Area, measure_scene::areaScene);
    add("measure-scale-dialog", MeasureAction::Distance, measure_scene::scaleDialogScene);
    // Тёмные двойники главных экранов (закреплённая тема).
    for (const char* name : {"measure-distance", "measure-scale-dialog"}) {
        for (const Scene& s : std::vector<Scene>(scenes)) {
            if (s.name == QLatin1String(name)) {
                Scene dark = s;
                dark.name += QStringLiteral("-dark");
                dark.theme = ThemeChoice::Dark;
                scenes.push_back(std::move(dark));
            }
        }
    }
    return scenes;
}

}  // namespace ot::ui
