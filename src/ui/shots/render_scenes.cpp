// Сцены стиля рендера (план 2.5b; эталон 3.1, 3.16, 6.10; view-26, ocr-38, pro-47): view-night — отчёт
// demo/report.pdf в ночном режиме (картинка не инвертирована), view-high-contrast — он же в высокой
// контрастности (белый на чёрном), view-overprint — view/overprint.pdf (PDF/X) с предпросмотром наложения:
// пересечение голубого и пурпурного синее. Стиль ставится вкладке, а не в settings.ini (сцены не меняют
// настройки друг другу).
#include <vector>

#include "services/settings.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
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
    return scenes;
}

}  // namespace ot::ui
