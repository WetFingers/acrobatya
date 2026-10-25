// Сцены измерений (план 5.5; эталон 3.4 «Измерения», 6.2 пп. 3 и 14, 6.3 «Масштаб и точность измерений»,
// «Measurement Info»): measure-distance — план этажа (tests/fixtures/annots/measure-viewport.pdf, масштаб
// документа 1 cm = 1 m): готовое расстояние по нижней стене с подписью «15,52 м» и идущее расстояние по
// перегородке, конец привязан к вершине (квадрат), подпись значения у курсора, окно «Сведения об измерении»;
// measure-area — готовая площадь комнаты 2 и идущая площадь комнаты 1 (три вершины и резинка к курсору);
// measure-scale-dialog — «Масштаб и точность» с калибровкой по отрезку 15,52 см и заметкой о масштабе
// документа. Тёмные двойники — measure-distance-dark, measure-scale-dialog-dark.
#include <QCoreApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
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

// annot-align: четыре пометки вразброс выбраны «Выбором объектов», открыто контекстное меню с «Выровнять ▸».
std::function<QWidget*()> alignScene() {
    return withMainWindow([](MainWindow& w) {
        w.setProperty(kDoneProperty, false);
        w.openFile(sceneFixture(QStringLiteral("edit/word-paragraphs.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            ToolRegistry::instance().activate(QStringLiteral("comment"), doc);
            doc->view().tools().setTool(ViewToolId::AnnotSelect);
            const RectF places[] = {{400, 330, 420, 350}, {446, 385, 466, 405}, {416, 440, 436, 460}};
            for (const RectF& r : places) {
                annot::AnnotProps p =
                    comments::newProps(Settings(), annot::AnnotType::Text, QStringLiteral("note"));
                p.rect = r;
                annot::create(doc->session(), 0, std::move(p));
            }
            annot::AnnotProps box =
                comments::newProps(Settings(), annot::AnnotType::Square, QStringLiteral("rectangle"));
            box.rect = {440, 490, 520, 530};
            box.color = annot::AnnotColor::fromRgb(0x1E88E5);
            annot::create(doc->session(), 0, std::move(box)).then(&w, [&w](annot::AnnotRef) {
                DocumentView* d = w.currentDocument();
                if (d == nullptr) {
                    return;
                }
                annot::list(d->session(), 0).then(&w, [&w](std::vector<annot::AnnotInfo> all) {
                    DocumentView* d = w.currentDocument();
                    if (d == nullptr) {
                        return;
                    }
                    std::vector<annot::AnnotInfo> chosen;
                    for (const annot::AnnotInfo& a : all) {
                        if (a.type == annot::AnnotType::Text || a.type == annot::AnnotType::Square) {
                            chosen.push_back(a);
                        }
                    }
                    AnnotSelectTool& select = d->view().tools().annotSelect();
                    select.setSelection(chosen);
                    auto* menu = new QMenu(&w);
                    menu->setAttribute(Qt::WA_DeleteOnClose);
                    select.fillContextMenu(*menu);
                    embedWindow(w, menu);
                    menu->adjustSize();
                    // Меню — левее выбранных, подменю «Выровнять» — ещё левее (пометки остаются на виду).
                    QRectF around;
                    for (const QRectF& f : select.frames()) {
                        around = around.isNull() ? f : around.united(f);
                    }
                    const QPoint corner = d->view().viewport()->mapTo(&w, around.topLeft().toPoint());
                    menu->move(std::max(8, corner.x() - menu->width() - 16), corner.y());
                    menu->show();
                    menu->raise();
                    if (auto* sub = menu->findChild<QMenu*>(QStringLiteral("annotAlignMenu"))) {
                        menu->setActiveAction(sub->menuAction());
                        embedWindow(w, sub);
                        sub->adjustSize();
                        const QRect item = menu->actionGeometry(sub->menuAction());
                        sub->move(std::max(8, menu->x() - sub->width()), menu->y() + item.y());
                        sub->show();
                        sub->raise();
                    }
                    markDone(w);
                });
            });
        });
    });
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
    Scene align;
    align.name = QStringLiteral("annot-align");
    align.make = measure_scene::alignScene();
    align.ready = measure_scene::measureReady;
    scenes.push_back(std::move(align));
    // Тёмные двойники главных экранов (закреплённая тема).
    for (const char* name : {"measure-distance", "measure-area", "measure-scale-dialog", "annot-align"}) {
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
