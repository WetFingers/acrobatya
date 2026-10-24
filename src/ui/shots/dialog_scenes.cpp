// Сцены диалогов документа (план 2.5; эталон 3.1, 3.13, 6.3, 6.10): properties-description, properties-fonts,
// properties-initial-view — «Свойства документа» (Ctrl+D) на demo/report.pdf на своей вкладке; xmp-editor —
// «Дополнительные метаданные» поверх «Свойств» (категория «Дополнительно»). Диалоги — окна верхнего уровня:
// для снимка встраиваются в главное окно (embedCentered). Кадр — когда диалог загружен и тайлы готовы.
#include <QTimer>

#include <vector>

#include "ui/dialogs/properties_dialog.h"
#include "ui/dialogs/xmp_editor.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"

namespace ot::ui {

namespace {

Scene propertiesScene(const char* name, PropertiesDialog::Tab tab, bool xmp = false) {
    Scene s;
    s.name = QLatin1String(name);
    s.make = withMainWindow([tab, xmp](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        QTimer::singleShot(0, &w, [&w, tab, xmp] {
            PropertiesDialog* d = w.showProperties(tab);
            if (d == nullptr) {
                return;
            }
            embedCentered(w, d);
            if (xmp) {
                QObject::connect(
                    d, &PropertiesDialog::loaded, d,
                    [&w, d] {
                        XmpEditor* x = d->showXmpEditor();
                        x->showCategory(XmpEditor::Advanced);
                        embedWindow(w, x);
                        x->move((w.width() - x->width()) / 2 + Theme::sizes().grid * 8,
                                (w.height() - x->height()) / 2 + Theme::sizes().grid * 8);
                        x->show();
                        x->raise();
                    },
                    Qt::SingleShotConnection);
            }
        });
    });
    s.ready = [tab, xmp](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        PropertiesDialog* d = window->propertiesDialog();
        if (d == nullptr || !d->isLoaded() || (tab == PropertiesDialog::Fonts && !d->fontsLoaded())) {
            return false;
        }
        if (xmp && (d->xmpEditor() == nullptr || !d->xmpEditor()->isVisible())) {
            return false;
        }
        return sceneDocumentReady(w);
    };
    return s;
}

}  // namespace

std::vector<Scene> dialogScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(propertiesScene("properties-description", PropertiesDialog::Description));
    scenes.push_back(propertiesScene("properties-fonts", PropertiesDialog::Fonts));
    scenes.push_back(propertiesScene("properties-initial-view", PropertiesDialog::InitialViewTab));
    scenes.push_back(propertiesScene("xmp-editor", PropertiesDialog::Description, true));
    return scenes;
}

}  // namespace ot::ui
