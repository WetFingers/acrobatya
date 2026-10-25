// Сцены Fill & Sign (план 5.3; эталон 3.5, 6.2 п. 4, 6.3 «Fill & Sign»): fill-sign-capture-type — «Подпись →
// Набрать» (имя рукописным шрифтом); fill-sign-capture-draw — «Нарисовать» с росчерком из трёх штрихов;
// fill-sign-capture-image — «Картинка»: скан подписи на сером листе, фон убран по порогу. Документ —
// плоская анкета tests/fixtures/forms/flat/anketa.pdf.
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

#include <cmath>
#include <functional>
#include <vector>

#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/fillsign/signature_capture_dialog.h"

namespace ot::ui {

namespace fillsign_scene {  // не анонимное: unity-сборка сцен (как у соседей — свои имена)

constexpr const char* kDoneProperty = "fillSignSceneDone";
constexpr const char* kAnketa = "forms/flat/anketa.pdf";

using SceneStep = std::function<void(MainWindow&, DocumentView&)>;

void markDone(MainWindow& w) { w.setProperty(kDoneProperty, true); }

bool fillSignReady(QWidget* w) { return w->property(kDoneProperty).toBool() && sceneDocumentReady(w); }

// Окно с анкетой; then — когда документ открыт.
std::function<QWidget*()> fillSignScene(SceneStep then) {
    return withMainWindow([then](MainWindow& w) {
        w.setProperty(kDoneProperty, false);
        w.openFile(sceneFixture(QLatin1String(kAnketa)));
        QTimer::singleShot(0, &w, [&w, then] {
            if (DocumentView* doc = w.currentDocument()) {
                then(w, *doc);
            }
        });
    });
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
    return scenes;
}

}  // namespace ot::ui
