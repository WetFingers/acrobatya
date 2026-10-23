// Сцены оболочки (план 2.3; эталон 6.1, 6.10): главное окно 1440 × 900 с документом — глобальная панель 48,
// вкладки 36, вид на холсте (левая панель, правая навигация и строка состояния добавляются пунктами 2.3 и 2.4
// — сцены их покажут сами: владелец критерия Р1 «48/320/44+280/32 на снимке»). shell-two-tabs — шесть
// документов: вкладки не помещаются (◀ ▶), у второй — точка «несохранено» (правка рамки записки — шаг
// истории). Кадр снимается, когда видимые тайлы готовы.
#include <QWidget>

#include <vector>

#include "services/annot_ops.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

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

}  // namespace

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
    return scenes;
}

}  // namespace ot::ui
