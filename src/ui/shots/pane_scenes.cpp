// Сцены правой навигации (план 2.4; эталон 6.1 «Правая панель», 6.10): главное окно 1440 × 900 с документом и
// открытой панелью 280 на нужной странице — pane-thumbnails (отчёт, миниатюры готовы), pane-bookmarks (дерево
// отчёта, глава раскрыта), pane-layers, pane-attachments, pane-destinations (nav.pdf: слои, вложения,
// назначения). Кадр снимается, когда видимые тайлы и данные панели готовы.
#include <QListView>
#include <QStandardItemModel>
#include <QTreeView>
#include <QTreeWidget>

#include <vector>

#include "ui/panes/attachments_pane.h"
#include "ui/panes/bookmarks_pane.h"
#include "ui/panes/destinations_pane.h"
#include "ui/panes/layers_pane.h"
#include "ui/panes/thumbnails_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

namespace ot::ui {

namespace {

bool paneDocumentReady(QWidget* w) {
    auto* window = static_cast<MainWindow*>(w);
    DocumentView* doc = window->currentDocument();
    if (doc == nullptr || doc->view().pageCount() == 0) {
        return false;
    }
    doc->view().viewport()->repaint();
    return doc->view().tilePainter().visibleComplete();
}

template <class Pane>
Pane* paneOf(QWidget* w, const char* id) {
    return static_cast<Pane*>(static_cast<MainWindow*>(w)->navPane().page(QLatin1String(id)));
}

Scene paneScene(const char* name, const char* fixture, const char* pane,
                std::function<void(MainWindow&)> after, std::function<bool(QWidget*)> ready) {
    Scene s;
    s.name = QLatin1String(name);
    const QString file = QLatin1String(fixture);
    const QString id = QLatin1String(pane);
    s.make = withMainWindow([file, id, after = std::move(after)](MainWindow& w) {
        w.openFile(sceneFixture(file));
        w.navPane().setDestinationsButton(id == QLatin1String("destinations"));
        w.navPane().showPane(id);
        if (after) {
            after(w);
        }
    });
    s.ready = [ready = std::move(ready)](QWidget* w) { return paneDocumentReady(w) && (!ready || ready(w)); };
    return s;
}

}  // namespace

std::vector<Scene> paneScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(paneScene(
        "pane-thumbnails", "demo/report.pdf", "thumbnails",
        [](MainWindow& w) { w.currentDocument()->view().goToPage(1); },
        [](QWidget* w) {
            auto* pane = paneOf<ThumbnailsPane>(w, "thumbnails");
            pane->list()->viewport()->repaint();
            const int count = pane->list()->model()->rowCount();
            int shown = 0;
            for (int p = 0; p < count; ++p) {
                if (!pane->thumbRect(p).isEmpty()) {
                    ++shown;
                    if (!pane->hasThumbnail(p)) {
                        return false;
                    }
                }
            }
            return shown > 0;
        }));
    scenes.push_back(paneScene("pane-bookmarks", "demo/report.pdf", "bookmarks", {}, [](QWidget* w) {
        auto* pane = paneOf<BookmarksPane>(w, "bookmarks");
        QStandardItemModel* model = pane->model();
        if (model->rowCount() < 3) {
            return false;
        }
        const QModelIndex chapter = model->index(2, 0);
        if (!pane->tree()->isExpanded(chapter)) {
            pane->tree()->expand(chapter);
            pane->tree()->setCurrentIndex(model->index(0, 0, chapter));
            return false;
        }
        return model->rowCount(chapter) > 0 &&
               !model->index(0, 0, chapter).data(BookmarksPane::PlaceholderRole).toBool();
    }));
    scenes.push_back(paneScene(
        "pane-layers", "view/nav.pdf", "layers",
        [](MainWindow& w) { w.currentDocument()->view().goToPage(0); },
        [](QWidget* w) { return !paneOf<LayersPane>(w, "layers")->rows().empty(); }));
    scenes.push_back(paneScene("pane-attachments", "view/nav.pdf", "attachments", {}, [](QWidget* w) {
        auto* pane = paneOf<AttachmentsPane>(w, "attachments");
        if (pane->files().size() < 4) {
            return false;
        }
        if (pane->current() != 3) {
            pane->select(3);  // setup.exe — «Открыть» недоступно
        }
        return true;
    }));
    scenes.push_back(paneScene("pane-destinations", "view/nav.pdf", "destinations", {}, [](QWidget* w) {
        return !paneOf<DestinationsPane>(w, "destinations")->destinations().empty();
    }));
    return scenes;
}

}  // namespace ot::ui
