// Сцены оболочки (план 2.3; эталон 6.1, 6.10): главное окно 1440 × 900 с документом — глобальная панель 48,
// вкладки 36, вид на холсте (левая панель, правая навигация и строка состояния добавляются пунктами 2.3 и 2.4
// — сцены их покажут сами: владелец критерия Р1 «48/320/44+280/32 на снимке»). shell-two-tabs — шесть
// документов: вкладки не помещаются (◀ ▶), у второй — точка «несохранено» (правка рамки записки — шаг
// истории). Кадр снимается, когда видимые тайлы готовы.
#include <QFileInfo>
#include <QWidget>

#include <vector>

#include "services/annot_ops.h"
#include "services/recent.h"
#include "services/thumbnails.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/home/home_page.h"
#include "ui/shell/main_window.h"
#include "ui/shell/tool_panel.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

bool documentReady(QWidget* w) {
    auto* window = static_cast<MainWindow*>(w);
    DocumentView* doc = window->currentDocument();
    if (doc == nullptr || doc->view().pageCount() == 0) {
        return false;
    }
    doc->view().viewport()->repaint();
    return doc->view().tilePainter().visibleComplete();
}

// Шаг истории без видимой разницы: рамка первой записки — на то же место.
void touchFirstAnnot(DocumentSession& session) {
    const auto annots = annot::list(session, 0).result();
    if (!annots.empty()) {
        annot::setRect(session, annots.front().ref, annots.front().rect);
    }
}

HomePage& homeOf(QWidget* w) { return *static_cast<HomePage*>(static_cast<MainWindow*>(w)->homePage()); }

// Видимые карточки Главной с готовыми миниатюрами (или файлами без миниатюры).
bool homeReady(QWidget* w) {
    HomePage& home = homeOf(w);
    if (home.isEmptyShown()) {
        return true;
    }
    for (int i = 0; i < home.itemCount(); ++i) {
        const QString path = home.pathAt(i);
        if (!QFileInfo(path).isDir() && ThumbnailCache::shared().get(path).isNull() &&
            !ThumbnailCache::shared().failed(path)) {
            return false;
        }
    }
    return home.itemCount() > 0;
}

}  // namespace

// Главная без недавних — данные снимков общие для всех сцен (.queue/shots-data), список чистится.
Scene homeScene(const char* name) {
    Scene s;
    s.name = QLatin1String(name);
    s.make = withMainWindow([](MainWindow& w) {
        RecentFiles::shared().clear();
        w.showHome();
    });
    return s;
}

std::vector<Scene> homeScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(homeScene("home-empty"));

    // Недавние: шесть фикстур, «Отчёт» в избранном, «Комментарии» закреплены, выбран второй — «Краткие
    // сведения».
    Scene recent;
    recent.name = QStringLiteral("home-recent");
    recent.make = withMainWindow([](MainWindow& w) {
        RecentFiles& r = RecentFiles::shared();
        r.clear();
        const std::pair<const char*, int> files[] = {
            {"view/layout-mixed.pdf", 6}, {"text/search.pdf", 2},   {"demo/mixed-sizes.pdf", 7},
            {"demo/form.pdf", 1},         {"demo/comments.pdf", 3}, {"demo/report.pdf", 12},
        };
        for (const auto& [f, pages] : files) {
            r.touch(sceneFixture(QLatin1String(f)), pages);
        }
        r.setFavorite(sceneFixture(QStringLiteral("demo/report.pdf")), true);
        r.setPinned(sceneFixture(QStringLiteral("demo/comments.pdf")), true);
        w.showHome();
        homeOf(&w).showRecent();
        homeOf(&w).select(1);
    });
    recent.ready = homeReady;
    scenes.push_back(recent);

    Scene files;
    files.name = QStringLiteral("home-files");
    files.make = withMainWindow([](MainWindow& w) {
        RecentFiles::shared().clear();
        w.showHome();
        homeOf(&w).showFolder(QFileInfo(sceneFixture(QStringLiteral("demo/report.pdf"))).absolutePath());
    });
    files.ready = homeReady;
    scenes.push_back(files);
    return scenes;
}

std::vector<Scene> shellScenes() {
    std::vector<Scene> scenes;
    Scene document;
    document.name = QStringLiteral("shell-document");
    document.make =
        withMainWindow([](MainWindow& w) { w.openFile(sceneFixture(QStringLiteral("demo/report.pdf"))); });
    document.ready = documentReady;
    scenes.push_back(document);

    Scene tabs;
    tabs.name = QStringLiteral("shell-two-tabs");
    tabs.make = withMainWindow([](MainWindow& w) {
        for (const char* f : {"demo/report.pdf", "demo/comments.pdf", "demo/form.pdf", "demo/mixed-sizes.pdf",
                              "text/search.pdf", "view/layout-mixed.pdf"}) {
            w.openFile(sceneFixture(QLatin1String(f)));
        }
        w.showDocument(1);
        try {
            touchFirstAnnot(w.currentDocument()->session());
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без записки — без точки
        }
    });
    tabs.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        return documentReady(w) && window->tabs().isModified(1);
    };
    scenes.push_back(tabs);

    // «Все инструменты» (план 2.3, пункт 2): 16 строк эталона 6.2, будущие — серые с подсказкой.
    Scene all;
    all.name = QStringLiteral("all-tools");
    all.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        w.toolPanel().setCollapsed(false);
        w.toolPanel().showList();
    });
    all.ready = documentReady;
    scenes.push_back(all);

    Scene collapsed;
    collapsed.name = QStringLiteral("all-tools-collapsed");
    collapsed.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        w.toolPanel().setCollapsed(true);
    });
    collapsed.ready = documentReady;
    scenes.push_back(collapsed);

    // Плавающая панель внизу документа: «Рука» включена.
    Scene quick;
    quick.name = QStringLiteral("quick-bar");
    quick.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/comments.pdf")));
        w.currentDocument()->view().tools().setTool(ViewToolId::Hand);
    });
    quick.ready = documentReady;
    scenes.push_back(quick);
    return scenes;
}

}  // namespace ot::ui
