// Сцены поиска (план 2.5; эталон 3.2 «Поиск», 6.3, 6.10): find-bar — панель «Найти» на find.pdf с подсветкой
// всех 12 «печать» и текущим вторым совпадением («2 из 12»); search-window — «Расширенный поиск» по текущему
// документу (дерево «файл → страница → контекст» с жирным совпадением) поверх окна. Кадр — когда поиск
// закончен и видимые тайлы готовы.
#include <QTimer>

#include <vector>

#include "ui/dialogs/search_window.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"

namespace ot::ui {

std::vector<Scene> searchScenes() {
    std::vector<Scene> scenes;

    Scene bar;
    bar.name = QStringLiteral("find-bar");
    bar.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("text/find.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            if (DocumentView* doc = w.currentDocument()) {
                FindBar& find = doc->findBar();
                QObject::connect(
                    &find, &FindBar::searchFinished, &find, [&find] { find.findNext(); },
                    Qt::SingleShotConnection);
                find.find(MainWindow::tr("печать"));
            }
        });
    });
    bar.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        DocumentView* doc = window->currentDocument();
        return doc != nullptr && doc->findBar().isVisible() && !doc->findBar().isSearching() &&
               doc->findBar().current() == 1 && sceneDocumentReady(w);
    };
    scenes.push_back(bar);

    Scene window;
    window.name = QStringLiteral("search-window");
    window.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("text/find.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            SearchWindow* s = w.showSearchWindow(MainWindow::tr("печать"));
            embedCentered(w, s);
            s->setScope(SearchWindow::Scope::Current);
            s->setIncludeBookmarks(true);
            s->start();
        });
    });
    window.ready = [](QWidget* w) {
        auto* main = static_cast<MainWindow*>(w);
        return main->searchWindow() != nullptr && !main->searchWindow()->isRunning() &&
               !main->searchWindow()->rows().isEmpty() && sceneDocumentReady(w);
    };
    scenes.push_back(window);
    return scenes;
}

}  // namespace ot::ui
