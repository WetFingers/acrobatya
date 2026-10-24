// Сцены Установок и палитры команд (план 2.4b; эталон 3.18, 6.3, 6.10): диалог и палитра — отдельные окна,
// поэтому для снимка они встраиваются в главное окно виджетами (как меню в menu-open) поверх открытого
// документа. preferences-general — «Общие», preferences-page-display — «Просмотр страницы», palette — палитра
// с фильтром «страниц»; 2.5b — preferences-page-display-full: «Просмотр страницы» целиком (предпросмотр
// наложения «Всегда», рамки областей включены — поля, не settings.ini). Кадр снимается, когда видимые тайлы
// документа готовы.
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsDropShadowEffect>
#include <QTimer>

#include <vector>

#include "ui/shell/command_palette.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/page_display_page.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/shots/scenes.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

namespace ot::ui {

bool sceneDocumentReady(QWidget* w) {
    auto* window = static_cast<MainWindow*>(w);
    DocumentView* doc = window->currentDocument();
    if (doc == nullptr || doc->view().pageCount() == 0) {
        return false;
    }
    doc->view().viewport()->repaint();
    return doc->view().tilePainter().visibleComplete();
}

void embedWindow(MainWindow& w, QWidget* window) {
    window->setParent(&w, Qt::Widget);
    window->setAutoFillBackground(true);
    const Metrics& m = Theme::sizes();
    auto* shadow = new QGraphicsDropShadowEffect(window);
    shadow->setBlurRadius(m.shadowBlur * 2);
    shadow->setOffset(0, m.shadowOffset);
    shadow->setColor(Theme::colors().shadow.color());
    window->setGraphicsEffect(shadow);
}

void embedCentered(MainWindow& w, QWidget* dialog) {
    embedWindow(w, dialog);
    dialog->move((w.width() - dialog->width()) / 2, (w.height() - dialog->height()) / 2);
    dialog->show();
    dialog->raise();
}

namespace {

Scene preferencesScene(const char* name, const char* category) {
    Scene s;
    s.name = QLatin1String(name);
    const QString id = QLatin1String(category);
    s.make = withMainWindow([id](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        QTimer::singleShot(0, &w, [&w, id] { embedCentered(w, w.showPreferences(id)); });
    });
    s.ready = sceneDocumentReady;
    return s;
}

}  // namespace

std::vector<Scene> prefsScenes() {
    std::vector<Scene> scenes;
    scenes.push_back(preferencesScene("preferences-general", "general"));
    scenes.push_back(preferencesScene("preferences-page-display", "pageDisplay"));
    scenes.push_back(preferencesScene("preferences-print", "print"));      // 2.6
    scenes.push_back(preferencesScene("preferences-editing", "editing"));  // 3.4

    Scene display;
    display.name = QStringLiteral("preferences-page-display-full");
    display.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            PreferencesDialog* d = w.showPreferences(QStringLiteral("pageDisplay"));
            embedCentered(w, d);  // показ читает settings.ini в поля — правки полей после него
            if (auto* page = qobject_cast<PageDisplayPage*>(d->page(QStringLiteral("pageDisplay")))) {
                page->overprintBox()->setCurrentIndex(0);  // «Всегда»
                page->pageBoxesBox()->setChecked(true);
                page->smoothImagesBox()->setChecked(false);
            }
        });
    });
    display.ready = sceneDocumentReady;
    scenes.push_back(display);

    Scene palette;
    palette.name = QStringLiteral("palette");
    palette.make = withMainWindow([](MainWindow& w) {
        w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
        QTimer::singleShot(0, &w, [&w] {
            CommandPalette* p = w.showCommandPalette();
            embedWindow(w, p);
            p->setFilter(MainWindow::tr("страниц"));
            p->show();  // showEvent ставит палитру у верхнего края окна
            p->raise();
        });
    });
    palette.ready = sceneDocumentReady;
    scenes.push_back(palette);
    return scenes;
}

}  // namespace ot::ui
