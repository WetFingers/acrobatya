// Сцены Fill & Sign (план 5.3; эталон 3.5, 6.2 п. 4, 6.3 «Fill & Sign»): fill-sign-capture-type — «Подпись →
// Набрать» (имя рукописным шрифтом); fill-sign-capture-draw — «Нарисовать» с росчерком из трёх штрихов;
// fill-sign-capture-image — «Картинка»: скан подписи на сером листе, фон убран по порогу; fill-sign-text —
// фамилия, имя и отчество на подчёркиваниях и набор даты на линии; fill-sign-comb — дата рождения в клетках и
// набор ИНН в гребёнку; fill-sign-marks — ✓ и ✕ во флажках, ● и — у строки, «—» выбрана; fill-sign-placed —
// заполненная анкета с нарисованной подписью на линии «Подпись» (выбрана: рамка и ручки);
// fill-sign-flatten-dialog — «Сохранить как — плоская копия» с отмеченным флажком. Документ — плоская анкета
// tests/fixtures/forms/flat/anketa.pdf, панель «Заполнить и подписать» открыта.
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <cmath>
#include <functional>
#include <vector>

#include "core/annot/fillsign.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "services/signature_image.h"
#include "ui/dialogs/save_as_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/fillsign/comb_editor.h"
#include "ui/tools/fillsign/fillsign_mode.h"
#include "ui/tools/fillsign/fillsign_tool.h"
#include "ui/tools/fillsign/signature_capture_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace fillsign_scene {  // не анонимное: unity-сборка сцен (как у соседей — свои имена)

constexpr const char* kDoneProperty = "fillSignSceneDone";
constexpr const char* kAnketa = "forms/flat/anketa.pdf";

using SceneStep = std::function<void(MainWindow&, DocumentView&)>;

void markDone(MainWindow& w) { w.setProperty(kDoneProperty, true); }

bool fillSignReady(QWidget* w) { return w->property(kDoneProperty).toBool() && sceneDocumentReady(w); }

// Окно с анкетой и «Заполнить и подписать»; then — когда документ открыт.
std::function<QWidget*()> fillSignScene(SceneStep then) {
    return withMainWindow([then](MainWindow& w) {
        w.setProperty(kDoneProperty, false);
        w.openFile(sceneFixture(QLatin1String(kAnketa)));
        QTimer::singleShot(0, &w, [&w, then] {
            if (DocumentView* doc = w.currentDocument()) {
                ToolRegistry::instance().activate(QStringLiteral("fillsign"), doc);
                then(w, *doc);
            }
        });
    });
}

FillSignTool& sceneTool() {
    return *static_cast<FillSignTool*>(ToolRegistry::instance().find(QStringLiteral("fillsign")));
}

constexpr double kPageHeight = 841.8898;
// Точка генератора анкеты (y вверх от низа) → точка страницы MuPDF.
PointF pt(double x, double pdfY) { return {static_cast<float>(x), static_cast<float>(kPageHeight - pdfY)}; }

std::string sceneText(const char* text) {
    return QCoreApplication::translate("ot::ui::FillSignScenes", text).toStdString();
}

using GeometryStep = std::function<void(MainWindow&, DocumentView&, const fillsign::PageGeometry&)>;

// Геометрия бланка первой страницы — затем then.
void withGeometry(MainWindow& w, DocumentView& doc, GeometryStep then) {
    fillsign::geometry(doc.session(), 0).then(&w, [&w, then](fillsign::PageGeometry g) {
        if (DocumentView* d = w.currentDocument()) {
            then(w, *d, g);
        }
    });
}

// Текст на линии щелчком в точке at (как инструмент).
annot::AnnotProps lineText(const fillsign::PageGeometry& g, PointF at, const char* text) {
    return fillsign::textProps(fillsign::placeText(g, at), sceneText(text), annot::AnnotColor::gray(0), 595);
}

// Росчерк из трёх штрихов в поле size: волна имени, петля и подчёркивание.
std::vector<std::vector<QPointF>> demoStrokes(QSizeF size) {
    const double w = size.width();
    const double h = size.height();
    std::vector<std::vector<QPointF>> strokes(3);
    for (int i = 0; i <= 60; ++i) {
        const double t = i / 60.0;
        strokes[0].emplace_back(w * (0.12 + 0.42 * t),
                                h * (0.55 - 0.22 * std::sin(t * 9.0) * (1.0 - 0.4 * t)));
    }
    for (int i = 0; i <= 40; ++i) {
        const double a = i / 40.0 * 6.2;
        strokes[1].emplace_back(w * 0.62 + std::cos(a) * w * 0.06 + i * w * 0.004,
                                h * 0.48 + std::sin(a) * h * 0.16);
    }
    for (int i = 0; i <= 30; ++i) {
        const double t = i / 30.0;
        strokes[2].emplace_back(w * (0.15 + 0.65 * t), h * (0.78 - 0.06 * std::sin(t * 3.1)));
    }
    return strokes;
}

SignatureCaptureDialog* showCapture(MainWindow& w, fillsign::SignatureKind kind) {
    auto* dialog = new SignatureCaptureDialog(kind, &w);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    embedCentered(w, dialog);
    return dialog;
}

void captureTypeScene(MainWindow& w, DocumentView& /*doc*/) {
    SignatureCaptureDialog* dialog = showCapture(w, fillsign::SignatureKind::Signature);
    dialog->setMode(SignatureCaptureDialog::Mode::Type);
    dialog->nameEdit()->setText(QCoreApplication::translate("ot::ui::FillSignScenes", "Иван Петров"));
    markDone(w);
}

void captureDrawScene(MainWindow& w, DocumentView& /*doc*/) {
    SignatureCaptureDialog* dialog = showCapture(w, fillsign::SignatureKind::Signature);
    dialog->setMode(SignatureCaptureDialog::Mode::Draw);
    dialog->setInkColor(QColor(0x1A, 0x3B, 0x9C));
    // Поле получает размер после раскладки диалога.
    QTimer::singleShot(0, dialog, [dialog, &w] {
        for (std::vector<QPointF>& s : demoStrokes(QSizeF(dialog->pad()->size()))) {
            dialog->pad()->addStroke(std::move(s));
        }
        markDone(w);
    });
}

void captureImageScene(MainWindow& w, DocumentView& /*doc*/) {
    // «Скан» подписи: сероватая бумага, клетка тетради и синие чернила.
    QImage scan(640, 240, QImage::Format_RGB32);
    scan.fill(QColor(246, 244, 238));
    {
        QPainter p(&scan);
        p.setPen(QPen(QColor(222, 226, 236), 1));
        for (int x = 0; x < scan.width(); x += 20) {
            p.drawLine(x, 0, x, scan.height());
        }
        for (int y = 0; y < scan.height(); y += 20) {
            p.drawLine(0, y, scan.width(), y);
        }
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QPen(QColor(28, 46, 120), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        for (const std::vector<QPointF>& stroke : demoStrokes(QSizeF(scan.size()))) {
            QPainterPath path(stroke.front());
            for (std::size_t i = 1; i < stroke.size(); ++i) {
                path.lineTo(stroke[i]);
            }
            p.drawPath(path);
        }
    }
    const QString file = QDir(QDir::tempPath()).filePath(QStringLiteral("acrobatya-scene-signature.png"));
    scan.save(file, "PNG");
    SignatureCaptureDialog* dialog = showCapture(w, fillsign::SignatureKind::Signature);
    dialog->setMode(SignatureCaptureDialog::Mode::Image);
    QTimer::singleShot(0, dialog, [dialog, file, &w] {
        dialog->loadImageFile(file);
        markDone(w);
    });
}

void textScene(MainWindow& w, DocumentView& doc) {
    withGeometry(w, doc, [](MainWindow& w, DocumentView& d, const fillsign::PageGeometry& g) {
        std::vector<annot::AnnotProps> items = {
            lineText(g, pt(170, 724), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "Петров")),
            lineText(g, pt(170, 692), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "Иван")),
            lineText(g, pt(170, 660), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "Сергеевич"))};
        fillsign::createAll(d.session(), 0, std::move(items)).then(&w, [&w](std::vector<annot::AnnotRef>) {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            // Набор даты на линии «Дата» — редактор поверх страницы.
            FillSignTool::modeOf(doc->view()).placeAt(0, QPointF(410, kPageHeight - 384));
            QTimer::singleShot(300, &w, [&w] {
                if (DocumentView* d2 = w.currentDocument()) {
                    if (InlineTextEditor* editor = InlineTextEditor::active(d2->view())) {
                        editor->setPlainText(QStringLiteral("25.10.2026"));
                    }
                }
                markDone(w);
            });
        });
    });
}

void combScene(MainWindow& w, DocumentView& doc) {
    withGeometry(w, doc, [](MainWindow& w, DocumentView& d, const fillsign::PageGeometry& g) {
        const fillsign::Placement date = fillsign::placeText(g, pt(190, 612));
        fillsign::createAll(d.session(), 0,
                            fillsign::combProps(date, U"01021990", annot::AnnotColor::gray(0)))
            .then(&w, [&w](std::vector<annot::AnnotRef>) {
                DocumentView* doc = w.currentDocument();
                if (doc == nullptr) {
                    return;
                }
                FillSignTool::modeOf(doc->view()).placeAt(0, QPointF(190, kPageHeight - 562));
                QTimer::singleShot(300, &w, [&w] {
                    if (DocumentView* d2 = w.currentDocument()) {
                        if (CombEditor* comb = CombEditor::active(d2->view())) {
                            comb->setText(QStringLiteral("7701234"));
                        }
                    }
                    markDone(w);
                });
            });
    });
}

void marksScene(MainWindow& w, DocumentView& doc) {
    sceneTool().start(FillSignAction::Check);
    withGeometry(w, doc, [](MainWindow& w, DocumentView& d, const fillsign::PageGeometry& g) {
        const annot::AnnotColor ink = annot::AnnotColor::fromRgb(0x1A3B9C);
        std::vector<annot::AnnotProps> items = {
            fillsign::markProps(fillsign::Mark::Check, fillsign::placeMark(g, pt(78, 474)), ink),
            fillsign::markProps(fillsign::Mark::Cross, fillsign::placeMark(g, pt(78, 450)), ink),
            fillsign::markProps(fillsign::Mark::Dot, fillsign::placeMark(g, pt(64, 724), 10), ink),
            fillsign::markProps(fillsign::Mark::Line, fillsign::placeMark(g, pt(540, 724), 16), ink),
        };
        fillsign::createAll(d.session(), 0, std::move(items), QString::fromLatin1(fillsign::kAddMarkStep))
            .then(&w, [&w](std::vector<annot::AnnotRef> refs) {
                DocumentView* doc = w.currentDocument();
                if (doc == nullptr || refs.empty()) {
                    markDone(w);
                    return;
                }
                const annot::AnnotRef first = refs.back();  // выбрана «—» у строки: галочка во флажке видна
                annot::list(doc->session(), 0).then(&w, [&w, first](std::vector<annot::AnnotInfo> list) {
                    if (DocumentView* d2 = w.currentDocument()) {
                        for (const annot::AnnotInfo& a : list) {
                            if (a.ref == first) {
                                d2->view().tools().annotSelect().setSelection({a});
                            }
                        }
                        d2->view().viewport()->update();
                    }
                    markDone(w);
                });
            });
    });
}

// Анкета: фамилия, имя, отчество, дата рождения и отметка — для сцен подписи и плоской копии.
void fillAnketa(MainWindow& w, DocumentView& doc, std::function<void(MainWindow&, DocumentView&)> then) {
    withGeometry(w, doc, [then](MainWindow& w, DocumentView& d, const fillsign::PageGeometry& g) {
        std::vector<annot::AnnotProps> items = {
            lineText(g, pt(170, 724), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "Петров")),
            lineText(g, pt(170, 692), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "Иван")),
            lineText(g, pt(170, 660), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "Сергеевич")),
            lineText(g, pt(410, 384), QT_TRANSLATE_NOOP("ot::ui::FillSignScenes", "25.10.2026")),
            fillsign::markProps(fillsign::Mark::Check, fillsign::placeMark(g, pt(78, 474)),
                                annot::AnnotColor::gray(0)),
        };
        for (annot::AnnotProps& p : fillsign::combProps(fillsign::placeText(g, pt(190, 612)), U"01021990",
                                                        annot::AnnotColor::gray(0))) {
            items.push_back(std::move(p));
        }
        fillsign::createAll(d.session(), 0, std::move(items))
            .then(&w, [&w, then](std::vector<annot::AnnotRef>) {
                if (DocumentView* doc2 = w.currentDocument()) {
                    then(w, *doc2);
                }
            });
    });
}

void placedScene(MainWindow& w, DocumentView& doc) {
    fillAnketa(w, doc, [](MainWindow& w, DocumentView& d) {
        FillSignMode& mode = FillSignTool::modeOf(d.view());
        mode.setSignatureImage(fillsign::SignatureKind::Signature,
                               fillsign::strokesToImage(demoStrokes(QSizeF(520, 180)), QSizeF(520, 180),
                                                        QColor(0x1A, 0x3B, 0x9C), 2.5, 3.0));
        sceneTool().start(FillSignAction::Signature);
        QObject::connect(&mode, &FillSignMode::created, &w, [&w] {
            QTimer::singleShot(200, &w, [&w] { markDone(w); });  // выбор приходит задачей
        });
        mode.placeAt(0, QPointF(150, kPageHeight - 386));
    });
}

void flattenDialogScene(MainWindow& w, DocumentView& doc) {
    fillAnketa(w, doc, [](MainWindow& w, DocumentView& d) {
        const QFileInfo file(d.path());
        auto* dialog =
            new SaveAsDialog(file.absoluteDir().filePath(
                                 QCoreApplication::translate("ot::ui::SaveFlow", "%1 (плоская копия).pdf")
                                     .arg(file.completeBaseName())),
                             false, &w);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(
            QCoreApplication::translate("ot::ui::SaveFlow", "Сохранить как — плоская копия"));
        dialog->flatCopyBox()->setChecked(true);
        dialog->resize(dialog->sizeHint().expandedTo(dialog->minimumSizeHint()));
        embedCentered(w, dialog);
        markDone(w);
    });
}

}  // namespace fillsign_scene

std::vector<Scene> fillSignScenes() {
    std::vector<Scene> scenes;
    auto add = [&scenes](const char* name, fillsign_scene::SceneStep step) {
        Scene s;
        s.name = QLatin1String(name);
        s.make = fillsign_scene::fillSignScene(std::move(step));
        s.ready = fillsign_scene::fillSignReady;
        scenes.push_back(std::move(s));
    };
    add("fill-sign-capture-type", fillsign_scene::captureTypeScene);
    add("fill-sign-capture-draw", fillsign_scene::captureDrawScene);
    add("fill-sign-capture-image", fillsign_scene::captureImageScene);
    add("fill-sign-text", fillsign_scene::textScene);
    add("fill-sign-comb", fillsign_scene::combScene);
    add("fill-sign-marks", fillsign_scene::marksScene);
    add("fill-sign-placed", fillsign_scene::placedScene);
    add("fill-sign-flatten-dialog", fillsign_scene::flattenDialogScene);
    // Тёмные двойники главных экранов (закреплённая тема).
    for (const char* name : {"fill-sign-text", "fill-sign-capture-draw", "fill-sign-placed"}) {
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
