// Сцены стиля рендера (план 2.5b; эталон 3.1, 3.16, 6.10; view-26, ocr-38, pro-47): view-night — отчёт
// demo/report.pdf в ночном режиме (картинка не инвертирована), view-high-contrast — он же в высокой
// контрастности (белый на чёрном), view-overprint — view/overprint.pdf (PDF/X) с предпросмотром наложения:
// пересечение голубого и пурпурного синее; view-boxes-overlay — view/boxes.pdf с рамками выпуска, обрезки и
// иллюстрации (Установки → Просмотр страницы). Стиль ставится вкладке, а не в settings.ini (сцены не меняют
// настройки друг другу).
#include <vector>

#include "services/settings.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/view/boxes_overlay.h"
#include "ui/view/page_view.h"
#include "ui/view/view_style.h"

namespace ot::ui {

namespace {

Scene recolorScene(const char* name, const char* fixture, const char* mode) {
    Scene s;
    s.name = QLatin1String(name);
    const QString file = QLatin1String(fixture);
    const QString recolor = QLatin1String(mode);
    s.make = withMainWindow([file, recolor](MainWindow& w) {
        w.openFile(sceneFixture(file), 0);
        if (DocumentView* d = w.currentDocument()) {
            const Settings settings;
            ViewStyle style = ViewStyle::fromSettings(settings);
            style.base.recolor = ViewStyle::recolorFor(recolor, settings);
            d->setViewStyle(style);
        }
    });
    s.ready = sceneDocumentReady;
    return s;
}

}  // namespace

std::vector<Scene> renderScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(recolorScene("view-night", "demo/report.pdf", "night"));
    scenes.push_back(recolorScene("view-high-contrast", "demo/report.pdf", "highContrast"));
    scenes.push_back(recolorScene("view-overprint", "view/overprint.pdf", "none"));

    Scene boxes;
    boxes.name = QStringLiteral("view-boxes-overlay");
    boxes.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("view/boxes.pdf")), 0);
        if (DocumentView* d = w.currentDocument()) {
            ViewStyle style = ViewStyle::fromSettings(Settings());
            style.showPageBoxes = true;
            d->setViewStyle(style);
        }
    });
    boxes.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        DocumentView* d = window->currentDocument();
        return d != nullptr && d->view().boxesOverlay().hasBoxes(0) && sceneDocumentReady(w);
    };
    scenes.push_back(boxes);
    return scenes;
}

}  // namespace ot::ui
