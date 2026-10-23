// Сцены режимов (план 2.4; эталон 3.1 «Режимы», 6.1, 6.10): read-mode — отчёт в режиме чтения (только холст и
// тонкая полоса внизу); full-screen — окно полного экрана со слайдом present.pdf на чёрном фоне. Кадр — когда
// видимые тайлы готовы.
#include <memory>
#include <vector>

#include "services/document_session.h"
#include "ui/shell/document_view.h"
#include "ui/shell/full_screen.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

namespace ot::ui {

std::vector<Scene> modeScenes() {
    std::vector<Scene> scenes;

    Scene read;
    read.name = QStringLiteral("read-mode");
    read.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")), 2);
        w.setReadMode(true);
    });
    read.ready = [](QWidget* w) {
        DocumentView* doc = static_cast<MainWindow*>(w)->currentDocument();
        if (doc == nullptr || doc->view().pageCount() == 0) {
            return false;
        }
        doc->view().viewport()->repaint();
        return doc->view().tilePainter().visibleComplete();
    };
    scenes.push_back(read);

    // Окно полного экрана без главного: своя сессия документа, свои пул и кеш вида.
    Scene full;
    full.name = QStringLiteral("full-screen");
    full.make = [] {
        auto session = std::make_shared<DocumentSession>();
        session->open(sceneFixture(QStringLiteral("view/present.pdf")));
        auto* window = new FullScreenWindow(session, nullptr, nullptr, 1);
        FullScreenSettings settings = FullScreenSettings::load();
        settings.advanceSeconds = 0;
        settings.useDocDuration = false;  // кадр не уходит сам
        window->setSettings(settings);
        return static_cast<QWidget*>(window);
    };
    full.ready = [](QWidget* w) {
        auto* window = static_cast<FullScreenWindow*>(w);
        window->view().viewport()->repaint();
        return window->currentPage() == 1 && window->view().tilePainter().visibleComplete();
    };
    scenes.push_back(full);
    return scenes;
}

}  // namespace ot::ui
