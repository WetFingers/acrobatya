// Сцены печати (план 2.6; эталон 3.3, 6.3 «OCR, сканирование, печать», 6.10): print-dialog — «Печать»
// (Ctrl+P) на demo/report.pdf: принтер, копии, страницы, «Размер», ориентация, «Комментарии и формы»,
// предпросмотр первого листа; print-dialog-booklet — буклет «Обе стороны» (12 страниц → 3 листа, 6 сторон),
// предпросмотр лицевой стороны листа 1 (стр. 12 и 1); print-dialog-poster — плакат A1 (pages/mixed-sizes.pdf,
// стр. 4) на A4 при 100 % с перекрытием 10 мм, метками обреза и подписями — 9 листов; print-preview-nup — 4
// на листе с границами страниц, предпросмотр листа 2. Принтеры — демонстрационные (runShots: demoPrinters),
// живые не перечисляются. Кадр — когда растр предпросмотра готов и тайлы документа дорисованы.
#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QRadioButton>
#include <QTimer>

#include <functional>
#include <vector>

#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/print/print_dialog.h"
#include "ui/tools/print/print_preview.h"
#include "ui/widgets/number_field.h"

namespace ot::ui {

namespace {

Scene printScene(const char* name, const char* fixture, std::function<void(PrintDialog&)> setup) {
    Scene s;
    s.name = QLatin1String(name);
    const QString file = QLatin1String(fixture);
    s.make = withMainWindow([file, setup](MainWindow& w) {
        w.openFile(sceneFixture(file));
        QTimer::singleShot(0, &w, [&w, setup] {
            PrintDialog* d = w.showPrintDialog();
            if (d == nullptr) {
                return;
            }
            embedCentered(w, d);
            if (setup) {
                setup(*d);
            }
            d->refresh();
        });
    });
    s.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        PrintDialog* d = window->printDialog();
        return d != nullptr && d->preview()->isReady() && sceneDocumentReady(w);
    };
    return s;
}

}  // namespace

std::vector<Scene> printScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(printScene("print-dialog", "demo/report.pdf", nullptr));
    scenes.push_back(printScene("print-dialog-booklet", "demo/report.pdf",
                                [](PrintDialog& d) { d.setLayoutMode(print::Layout::Booklet); }));
    scenes.push_back(printScene("print-dialog-poster", "pages/mixed-sizes.pdf", [](PrintDialog& d) {
        d.setLayoutMode(print::Layout::Poster);
        d.rangeEdit()->setText(QStringLiteral("4"));
        d.labelsBox()->setChecked(true);
    }));
    scenes.push_back(printScene("print-preview-nup", "demo/report.pdf", [](PrintDialog& d) {
        d.setLayoutMode(print::Layout::Multiple);
        d.bordersBox()->setChecked(true);
        d.refresh();
        d.preview()->setCurrentSheet(1);
    }));
    return scenes;
}

}  // namespace ot::ui
