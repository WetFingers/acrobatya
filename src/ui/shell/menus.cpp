// Команды и меню окна (эталон 6.1, 6.8; view-32, view-51): все действия окна — команды реестра
// services/commands.h с клавишами таблицы 6.8; строка меню (Файл, Правка, Вид, Инструменты, Подпись, Окно,
// Справка; скрыта, F9) и «Меню ▾» глобальной панели — одни и те же QMenu, собранные из реестра; QAction
// команды живёт в окне (клавиши работают и без строки меню), команды контекста "view" — в области документов
// (Qt::WidgetWithChildrenShortcut: Home, PgUp, ←, Ctrl+C не мешают полям ввода; инструменту, которому клавиша
// нужна самому, PageView отвечает на ShortcutOverride). Инструменты — из ToolRegistry («Инструменты», команды
// Tool::commands()); заглушки будущих этапов — серые с подсказкой. Однобуквенные ускорители — настройка.
#include <QAction>
#include <QHash>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QStackedWidget>

#include <tuple>

#include "services/commands.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/quick_bar.h"
#include "ui/shell/status_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/theme/icons.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

struct MainWindow::Actions {
    QHash<QString, QMenu*> menus;  // "file", "view/zoom"…
    QHash<QString, QAction*> byId;
    QMenu* main = nullptr;   // «Меню ▾»
    QMenu* share = nullptr;  // «Поделиться ▾»
    QList<QAction*> tabActions;
};

namespace {

QKeySequence key(QKeyCombination k) { return QKeySequence(k); }

}  // namespace

// Команды окна. Повторный вызов (смена языка) заменяет их по id — названия по новому переводчику.
void MainWindow::registerCommands() {
    CommandRegistry& r = *commands_;
    auto hasDoc = [this] { return currentDocument() != nullptr; };
    auto editable = [this] {
        DocumentView* d = currentDocument();
        return d != nullptr && d->canEdit();
    };
    auto onView = [this](std::function<void(PageView&)> fn) -> std::function<void()> {
        return [this, fn = std::move(fn)] {
            if (DocumentView* d = currentDocument()) {
                fn(d->view());
            }
        };
    };
    auto tool = [onView](ViewToolId id) { return onView([id](PageView& v) { v.tools().setTool(id); }); };
    auto toolChecked = [this](ViewToolId id) -> std::function<bool()> {
        return [this, id] {
            DocumentView* d = currentDocument();
            return d != nullptr && d->view().tools().tool() == id;
        };
    };
    auto mode = [this](PageMode m) -> std::function<bool()> {
        return [this, m] {
            DocumentView* d = currentDocument();
            return d != nullptr && d->view().pageMode() == m;
        };
    };
    const QString view = QLatin1String(CommandRegistry::kViewContext);

    // Файл
    r.add({.id = QStringLiteral("doc.open"),
           .title = tr("&Открыть…"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_O)},
           .icon = QStringLiteral("folder-open"),
           .run = [this] { openDialog(); },
           .menu = QStringLiteral("file")});
    r.add({.id = QStringLiteral("doc.save"),
           .title = tr("&Сохранить"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_S)},
           .icon = QStringLiteral("save"),
           .run = [this] { saveCurrent(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("file"),
           .separatorBefore = true});
    r.add({.id = QStringLiteral("doc.saveAs"),
           .title = tr("Сохранить &как…"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_S)},
           .run = [this] { saveCurrentAs(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("file")});
    r.add({.id = QStringLiteral("doc.close"),
           .title = tr("&Закрыть"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_W), key(Qt::CTRL | Qt::Key_F4)},
           .run = [this] { closeDocument(currentIndex()); },
           .enabled = hasDoc,
           .menu = QStringLiteral("file"),
           .separatorBefore = true});
    r.add({.id = QStringLiteral("doc.print"),
           .title = tr("&Печать…"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_P)},
           .icon = QStringLiteral("printer"),
           .menu = QStringLiteral("file"),
           .separatorBefore = true,
           .stage = 2});
    r.add({.id = QStringLiteral("doc.properties"),
           .title = tr("Сво&йства документа…"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_D)},
           .run = [this] { showProperties(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("file")});
    r.add({.id = QStringLiteral("app.quit"),
           .title = tr("В&ыход"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_Q)},
           .run = [this] { close(); },
           .menu = QStringLiteral("file"),
           .separatorBefore = true});

    // Правка
    r.add({.id = QStringLiteral("edit.undo"),
           .title = tr("&Отменить"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_Z)},
           .run =
               [this] {
                   if (DocumentView* d = currentDocument()) {
                       d->session().undo();
                   }
               },
           .enabled = [this,
                       editable] { return editable() && currentDocument()->session().undoState().canUndo; },
           .menu = QStringLiteral("edit")});
    r.add({.id = QStringLiteral("edit.redo"),
           .title = tr("&Вернуть"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_Y)},
           .run =
               [this] {
                   if (DocumentView* d = currentDocument()) {
                       d->session().redo();
                   }
               },
           .enabled = [this,
                       editable] { return editable() && currentDocument()->session().undoState().canRedo; },
           .menu = QStringLiteral("edit")});
    r.add({.id = QStringLiteral("edit.copy"),
           .title = tr("&Копировать"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_C)},
           .run = onView([](PageView& v) { v.tools().select().copySelection(); }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("edit"),
           .separatorBefore = true});
    r.add({.id = QStringLiteral("edit.selectAll"),
           .title = tr("Выделить &всё"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_A)},
           .run = onView([](PageView& v) {
               v.tools().setTool(ViewToolId::Select);
               v.tools().select().selectAll();
           }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("edit")});
    // Поиск (2.5): панель «Найти» вкладки и «Расширенный поиск».
    auto onFind = [this](std::function<void(FindBar&)> fn) -> std::function<void()> {
        return [this, fn = std::move(fn)] {
            if (DocumentView* d = currentDocument()) {
                fn(d->findBar());
            }
        };
    };
    r.add({.id = QStringLiteral("edit.find"),
           .title = tr("&Найти…"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_F)},
           .icon = QStringLiteral("search"),
           .run = onFind([](FindBar& f) { f.activate(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("edit"),
           .separatorBefore = true});
    r.add({.id = QStringLiteral("edit.findNext"),
           .title = tr("Найти &следующее"),
           .shortcuts = {key(Qt::Key_F3), key(Qt::CTRL | Qt::Key_G)},
           .run = onFind([](FindBar& f) { f.findNext(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("edit")});
    r.add({.id = QStringLiteral("edit.findPrevious"),
           .title = tr("Найти &предыдущее"),
           .shortcuts = {key(Qt::SHIFT | Qt::Key_F3)},
           .run = onFind([](FindBar& f) { f.findPrevious(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("edit")});
    r.add({.id = QStringLiteral("edit.advancedSearch"),
           .title = tr("&Расширенный поиск…"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_F)},
           .run = [this] { showSearchWindow(); },
           .menu = QStringLiteral("edit")});
    r.add({.id = QStringLiteral("edit.replace"),
           .title = tr("Найти и &заменить…"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_H)},
           .menu = QStringLiteral("edit"),
           .stage = 3});
    r.add({.id = QStringLiteral("app.preferences"),
           .title = tr("&Установки…"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_K)},
           .icon = QStringLiteral("settings"),
           .run = [this] { showPreferences(); },
           .menu = QStringLiteral("edit"),
           .separatorBefore = true});

    // Вид
    r.add({.id = QStringLiteral("view.home"),
           .title = tr("&Главная"),
           .icon = QStringLiteral("home"),
           .run = [this] { showHome(); },
           .menu = QStringLiteral("view")});
    r.add({.id = QStringLiteral("view.zoomIn"),
           .title = tr("&Увеличить"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_Equal), key(Qt::CTRL | Qt::Key_Plus)},
           .icon = QStringLiteral("zoom-in"),
           .run = onView([](PageView& v) { v.zoomIn(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/zoom")});
    r.add({.id = QStringLiteral("view.zoomOut"),
           .title = tr("У&меньшить"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_Minus)},
           .icon = QStringLiteral("zoom-out"),
           .run = onView([](PageView& v) { v.zoomOut(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/zoom")});
    const std::tuple<const char*, QString, QKeyCombination, FitMode, bool> fits[] = {
        {"view.actualSize", tr("&Реальный размер"), Qt::CTRL | Qt::Key_1, FitMode::ActualSize, true},
        {"view.fitPage", tr("Уместить &страницу"), Qt::CTRL | Qt::Key_0, FitMode::Page, false},
        {"view.fitWidth", tr("По &ширине"), Qt::CTRL | Qt::Key_2, FitMode::Width, false},
        {"view.fitVisible", tr("По &видимому"), Qt::CTRL | Qt::Key_3, FitMode::Visible, false},
    };
    for (const auto& [id, title, k, fit, sep] : fits) {
        const FitMode f = fit;
        r.add({.id = QLatin1String(id),
               .title = title,
               .shortcuts = {key(k)},
               .run = onView([f](PageView& v) { v.fit(f); }),
               .enabled = hasDoc,
               .menu = QStringLiteral("view/zoom"),
               .separatorBefore = sep});
    }
    const std::tuple<const char*, QString, PageMode> modes[] = {
        {"view.singlePage", tr("&Одна страница"), PageMode::SinglePage},
        {"view.continuous", tr("&Непрерывно"), PageMode::Continuous},
        {"view.facing", tr("&Разворот"), PageMode::Facing},
        {"view.facingContinuous", tr("Разворот н&епрерывно"), PageMode::FacingContinuous},
    };
    for (const auto& [id, title, pm] : modes) {
        const PageMode m = pm;
        r.add({.id = QLatin1String(id),
               .title = title,
               .run = onView([m](PageView& v) { v.setPageMode(m); }),
               .enabled = hasDoc,
               .menu = QStringLiteral("view/layout"),
               .checkable = true,
               .checked = mode(m)});
    }
    r.add({.id = QStringLiteral("view.coverPage"),
           .title = tr("&Титульная страница отдельно"),
           .run = onView([](PageView& v) { v.setCoverPage(!v.coverPage()); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/layout"),
           .separatorBefore = true,
           .checkable = true,
           .checked = [this] {
               return currentDocument() != nullptr && currentDocument()->view().coverPage();
           }});
    r.add({.id = QStringLiteral("view.rotateCw"),
           .title = tr("По &часовой стрелке"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal), key(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus)},
           .icon = QStringLiteral("rotate-cw"),
           .run = onView([](PageView& v) { v.rotateView(90); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/rotate")});
    r.add({.id = QStringLiteral("view.rotateCcw"),
           .title = tr("&Против часовой стрелки"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus)},
           .icon = QStringLiteral("rotate-ccw"),
           .run = onView([](PageView& v) { v.rotateView(-90); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/rotate")});
    r.add({.id = QStringLiteral("view.firstPage"),
           .title = tr("&Первая страница"),
           .shortcuts = {key(Qt::Key_Home)},
           .run = onView([](PageView& v) { v.firstPage(); }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate")});
    r.add({.id = QStringLiteral("view.previousPage"),
           .title = tr("П&редыдущая страница"),
           .shortcuts = {key(Qt::Key_PageUp), key(Qt::Key_Left)},
           .run = onView([](PageView& v) { v.previousPage(); }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate")});
    r.add({.id = QStringLiteral("view.nextPage"),
           .title = tr("&Следующая страница"),
           .shortcuts = {key(Qt::Key_PageDown), key(Qt::Key_Right)},
           .run = onView([](PageView& v) { v.nextPage(); }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate")});
    r.add({.id = QStringLiteral("view.lastPage"),
           .title = tr("П&оследняя страница"),
           .shortcuts = {key(Qt::Key_End)},
           .run = onView([](PageView& v) { v.lastPage(); }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate")});
    r.add({.id = QStringLiteral("view.goToPage"),
           .title = tr("Перейти к &странице…"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_N)},
           .run = [this] { status_->focusPageField(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate")});
    r.add({.id = QStringLiteral("view.back"),
           .title = tr("Предыдущий &вид"),
           .shortcuts = {key(Qt::ALT | Qt::Key_Left)},
           .run = onView([](PageView& v) { v.goBack(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate"),
           .separatorBefore = true});
    r.add({.id = QStringLiteral("view.forward"),
           .title = tr("Следующий в&ид"),
           .shortcuts = {key(Qt::ALT | Qt::Key_Right)},
           .run = onView([](PageView& v) { v.goForward(); }),
           .enabled = hasDoc,
           .menu = QStringLiteral("view/navigate")});
    r.add({.id = QStringLiteral("view.readMode"),
           .title = tr("Режим &чтения"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_H)},
           .icon = QStringLiteral("book-open"),
           .run = [this] { setReadMode(!readMode_); },
           .enabled = hasDoc,
           .menu = QStringLiteral("view"),
           .separatorBefore = true,
           .checkable = true,
           .checked = [this] { return readMode_; }});
    r.add({.id = QStringLiteral("view.fullScreen"),
           .title = tr("&Полноэкранный режим"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_L)},
           .icon = QStringLiteral("maximize"),
           .run = [this] { enterFullScreen(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("view")});
    // Esc — выход из режима чтения (только пока он включён; полный экран — своё окно, Esc ловит оно).
    r.add({.id = QStringLiteral("view.exitMode"),
           .title = tr("Выйти из режима чтения"),
           .shortcuts = {key(Qt::Key_Escape)},
           .run = [this] { setReadMode(false); },
           .when = QLatin1String(kReadModeContext)});
    r.add({.id = QStringLiteral("view.navPane"),
           .title = tr("Панель &навигации"),
           .shortcuts = {key(Qt::Key_F4)},
           .run = [this] { navPane_->togglePanel(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("view/panels"),
           .checkable = true,
           .checked = [this] { return navPane_->isPanelOpen(); }});
    // Страницы правой навигации (эталон 6.1, 6.3): по одной, без клавиш (F4 — последняя открытая).
    const std::tuple<const char*, QString, const char*> panes[] = {
        {"view.pane.thumbnails", tr("&Миниатюры страниц"), "thumbnails"},
        {"view.pane.bookmarks", tr("&Закладки"), "bookmarks"},
        {"view.pane.comments", tr("&Комментарии"), "comments"},
        {"view.pane.layers", tr("&Слои"), "layers"},
        {"view.pane.attachments", tr("&Вложения"), "attachments"},
        {"view.pane.signatures", tr("&Подписи"), "signatures"},
        {"view.pane.destinations", tr("&Назначения"), "destinations"},
    };
    for (const auto& [id, title, pane] : panes) {
        const QString p = QLatin1String(pane);
        r.add({.id = QLatin1String(id),
               .title = title,
               .icon = QLatin1String(toolIconName(pane)),
               .run =
                   [this, p] {
                       if (navPane_->isPanelOpen() && navPane_->currentPane() == p) {
                           navPane_->closePanel();
                       } else {
                           navPane_->showPane(p);
                       }
                   },
               .enabled = hasDoc,
               .menu = QStringLiteral("view/navpanes"),
               .checkable = true,
               .checked = [this, p] { return navPane_->isPanelOpen() && navPane_->currentPane() == p; }});
    }
    r.add({.id = QStringLiteral("view.toolPanel"),
           .title = tr("Панель &инструментов"),
           .shortcuts = {key(Qt::SHIFT | Qt::Key_F4)},
           .run = [this] { toolPanel_->toggleCollapsed(); },
           .menu = QStringLiteral("view/panels"),
           .checkable = true,
           .checked = [this] { return !toolPanel_->isCollapsed(); }});
    r.add({.id = QStringLiteral("view.allPanels"),
           .title = tr("&Все панели"),
           .shortcuts = {key(Qt::Key_F8)},
           .run = [this] { setPanelsHidden(!panelsHidden_); },
           .menu = QStringLiteral("view/panels"),
           .checkable = true,
           .checked = [this] { return !panelsHidden_; }});
    r.add({.id = QStringLiteral("view.menuBar"),
           .title = tr("&Строка меню"),
           .shortcuts = {key(Qt::Key_F9)},
           .run = [this] { menuBar()->setVisible(!menuBar()->isVisible()); },
           .menu = QStringLiteral("view/panels"),
           .checkable = true,
           .checked = [this] { return menuBar()->isVisible(); }});
    r.add({.id = QStringLiteral("app.palette"),
           .title = tr("Палитра &команд…"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_P)},
           .run = [this] { showCommandPalette(); },
           .menu = QStringLiteral("view"),
           .separatorBefore = true});

    // Инструменты просмотра (однобуквенные клавиши — настройка, эталон 6.8).
    const std::tuple<const char*, QString, QKeySequence, ViewToolId, const char*> viewTools[] = {
        {"tool.select", tr("&Выделение"), key(Qt::Key_V), ViewToolId::Select, "mouse-pointer-2"},
        {"tool.hand", tr("&Рука"), key(Qt::Key_H), ViewToolId::Hand, "hand"},
        {"tool.marqueeZoom", tr("&Увеличение области"), key(Qt::Key_Z), ViewToolId::MarqueeZoom, "zoom-in"},
        {"tool.loupe", tr("&Лупа"), QKeySequence(), ViewToolId::Loupe, ""},
        {"tool.panZoom", tr("&Панорамирование и масштаб"), QKeySequence(), ViewToolId::PanZoom, ""},
        {"tool.snapshot", tr("&Снимок"), key(Qt::Key_X), ViewToolId::Snapshot, ""},
    };
    for (const auto& [id, title, k, t, icon] : viewTools) {
        r.add({.id = QLatin1String(id),
               .title = title,
               .shortcuts = k.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{k},
               .icon = QLatin1String(icon),
               .run = tool(t),
               .when = view,
               .enabled = hasDoc,
               .menu = QStringLiteral("tools"),
               .singleKey = !k.isEmpty(),
               .checkable = true,
               .checked = toolChecked(t)});
    }
    r.add({.id = QStringLiteral("tool.zoomCycle"),
           .title = tr("Сменить инструмент &масштаба"),
           .shortcuts = {key(Qt::SHIFT | Qt::Key_Z)},
           .run = onView([](PageView& v) { v.tools().cycleZoomTools(); }),
           .when = view,
           .enabled = hasDoc,
           .menu = QStringLiteral("tools"),
           .singleKey = true});
    r.add({.id = QStringLiteral("tools.all"),
           .title = tr("Все &инструменты"),
           .icon = QStringLiteral("grid-2x2"),
           .run = [this] { showAllTools(); },
           .menu = QStringLiteral("tools"),
           .separatorBefore = true});

    // Подпись (этапы 4–5).
    const std::tuple<const char*, QString, int, bool> sign[] = {
        {"sign.fillSign", tr("&Заполнить и подписать"), 5, false},
        {"sign.digital", tr("Поставить &цифровую подпись…"), 4, true},
        {"sign.certify", tr("&Сертифицировать…"), 4, false},
        {"sign.timestamp", tr("&Метка времени документа"), 4, false},
        {"sign.validateAll", tr("&Проверить все подписи"), 4, true},
        {"sign.identities", tr("&Удостоверения…"), 4, false},
    };
    for (const auto& [id, title, stage, sep] : sign) {
        r.add({.id = QLatin1String(id),
               .title = title,
               .menu = QStringLiteral("sign"),
               .separatorBefore = sep,
               .stage = stage});
    }

    // Окно
    r.add({.id = QStringLiteral("window.nextTab"),
           .title = tr("&Следующая вкладка"),
           .shortcuts = {key(Qt::CTRL | Qt::Key_Tab)},
           .run =
               [this] {
                   tabs().selectNext();
                   showDocument(tabs().currentIndex());
               },
           .enabled = hasDoc,
           .menu = QStringLiteral("window")});
    r.add({.id = QStringLiteral("window.previousTab"),
           .title = tr("&Предыдущая вкладка"),
           .shortcuts = {key(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), key(Qt::CTRL | Qt::Key_Backtab)},
           .run =
               [this] {
                   tabs().selectPrevious();
                   showDocument(tabs().currentIndex());
               },
           .enabled = hasDoc,
           .menu = QStringLiteral("window")});

    // Справка
    r.add({.id = QStringLiteral("help.contents"),
           .title = tr("&Справка АкроБати"),
           .shortcuts = {key(Qt::Key_F1)},
           .menu = QStringLiteral("help"),
           .stage = 9});
    r.add({.id = QStringLiteral("help.about"),
           .title = tr("&О программе"),
           .run = [this] { about(); },
           .menu = QStringLiteral("help"),
           .separatorBefore = true});

    // «Поделиться ▾»
    r.add({.id = QStringLiteral("share.mail"),
           .title = tr("Отправить по &почте…"),
           .icon = QStringLiteral("mail"),
           .menu = QStringLiteral("share"),
           .stage = 9});
    r.add({.id = QStringLiteral("share.copyPath"),
           .title = tr("&Копировать путь"),
           .run = [this] { copyPath(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("share")});
    r.add({.id = QStringLiteral("share.showInExplorer"),
           .title = tr("Показать в &Проводнике"),
           .run = [this] { showInExplorer(); },
           .enabled = hasDoc,
           .menu = QStringLiteral("share")});
    registerToolCommands();
}

// «Инструменты»: 16 инструментов эталона 6.2 (заглушки — серые) и команды самих инструментов.
void MainWindow::registerToolCommands() {
    CommandRegistry& r = *commands_;
    QStringList stale;
    for (const Command* c : r.all()) {
        if (c->id.startsWith(QLatin1String("tools.")) && c->id != QLatin1String("tools.all")) {
            stale << c->id;
        }
    }
    for (const QString& id : stale) {
        r.remove(id);
    }
    bool first = true;
    for (Tool* t : ToolRegistry::instance().tools()) {
        const QString id = t->id();
        r.add({.id = QStringLiteral("tools.") + id,
               .title = t->title(),
               .icon = t->icon(),
               .run = [this, id] { ToolRegistry::instance().activate(id, currentDocument()); },
               .menu = QStringLiteral("tools"),
               .separatorBefore = first,
               .stage = t->isStub() ? t->stage() : 0});
        first = false;
        for (Command c : t->commands()) {
            r.add(std::move(c));
        }
    }
}

void MainWindow::buildMenus() {
    actions_ = std::make_unique<Actions>();
    commands_->loadSettings();
    registerCommands();
    actions_->main = new QMenu(this);
    actions_->main->setObjectName(QStringLiteral("mainMenu"));
    globalBar_->setMainMenu(actions_->main);
    actions_->share = new QMenu(this);
    actions_->share->setObjectName(QStringLiteral("shareMenu"));
    globalBar_->setShareMenu(actions_->share);
    rebuildMenus();
    connect(commands_, &CommandRegistry::shortcutsChanged, this, &MainWindow::updateActions);
    connect(&ToolRegistry::instance(), &ToolRegistry::toolsChanged, this, [this] {
        registerToolCommands();
        rebuildMenus();
    });
}

// Меню из реестра: верхние меню по порядку эталона 6.1, подменю «Вида» — там, где встречена первая их
// команда.
void MainWindow::rebuildMenus() {
    Actions& a = *actions_;
    for (QAction* act : std::as_const(a.byId)) {
        delete act;
    }
    a.byId.clear();
    menuBar()->clear();
    a.main->clear();
    a.share->clear();
    for (auto it = a.menus.constBegin(); it != a.menus.constEnd(); ++it) {
        if (!it.key().contains(QLatin1Char('/'))) {
            delete it.value();  // подменю — дети верхних меню
        }
    }
    a.menus.clear();
    a.tabActions.clear();

    const std::pair<const char*, QString> tops[] = {
        {"file", tr("&Файл")},         {"edit", tr("&Правка")},  {"view", tr("&Вид")},
        {"tools", tr("&Инструменты")}, {"sign", tr("П&одпись")}, {"window", tr("О&кно")},
        {"help", tr("&Справка")},
    };
    for (const auto& [id, title] : tops) {
        auto* m = new QMenu(title, this);
        m->setObjectName(QStringLiteral("menu.") + QLatin1String(id));
        m->setToolTipsVisible(true);
        a.menus.insert(QLatin1String(id), m);
        menuBar()->addMenu(m);
        a.main->addMenu(m);
    }
    a.share->setToolTipsVisible(true);
    const QHash<QString, QString> subTitles = {
        {QStringLiteral("view/zoom"), tr("&Масштаб")},
        {QStringLiteral("view/layout"), tr("&Отображение страниц")},
        {QStringLiteral("view/rotate"), tr("&Повернуть вид")},
        {QStringLiteral("view/navigate"), tr("Пере&ход по страницам")},
        {QStringLiteral("view/panels"), tr("Показать или с&крыть")},
        {QStringLiteral("view/navpanes"), tr("Панели на&вигации")},
    };

    for (const Command* c : commands_->all()) {
        if (c->menu.isEmpty()) {
            continue;
        }
        QMenu* target = c->menu == QLatin1String("share") ? a.share : a.menus.value(c->menu);
        if (target == nullptr) {
            QMenu* top = a.menus.value(c->menu.section(QLatin1Char('/'), 0, 0));
            if (top == nullptr) {
                continue;
            }
            target = top->addMenu(subTitles.value(c->menu, c->menu));
            target->setObjectName(QStringLiteral("menu.") + c->menu);
            target->setToolTipsVisible(true);
            a.menus.insert(c->menu, target);
        }
        if (c->separatorBefore && !target->isEmpty()) {
            target->addSeparator();
        }
        auto* act = new QAction(c->title, this);
        act->setObjectName(c->id);
        act->setCheckable(c->checkable);
        const QString id = c->id;
        connect(act, &QAction::triggered, this, [this, id] {
            commands_->trigger(id);
            updateActions();
        });
        // Клавиша контекста "view" — только при фокусе в документе; остальные — во всём окне.
        if (c->when == QLatin1String(CommandRegistry::kViewContext)) {
            act->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            documents_->addAction(act);
        } else {
            act->setShortcutContext(Qt::WindowShortcut);
            addAction(act);
        }
        target->addAction(act);
        a.byId.insert(id, act);
    }

    // «Окно»: список открытых документов при показе меню.
    QMenu* window = a.menus.value(QStringLiteral("window"));
    connect(window, &QMenu::aboutToShow, this, [this, window] {
        Actions& acts = *actions_;
        for (QAction* act : std::as_const(acts.tabActions)) {
            window->removeAction(act);
            act->deleteLater();
        }
        acts.tabActions.clear();
        if (documentCount() > 0) {
            acts.tabActions << window->addSeparator();
        }
        for (int i = 0; i < documentCount(); ++i) {
            QAction* act = window->addAction(tabs().tabText(i));
            act->setCheckable(true);
            act->setChecked(i == currentIndex());
            connect(act, &QAction::triggered, this, [this, i] { showDocument(i); });
            acts.tabActions << act;
        }
    });
    updateActions();
}

void MainWindow::retranslateMenus() {
    if (!actions_) {
        return;
    }
    registerCommands();
    rebuildMenus();
}

void MainWindow::updateActions() {
    if (!actions_) {
        return;
    }
    for (auto it = actions_->byId.constBegin(); it != actions_->byId.constEnd(); ++it) {
        const Command* c = commands_->find(it.key());
        QAction* act = it.value();
        if (c == nullptr) {
            continue;
        }
        act->setEnabled(commands_->isEnabled(c->id));
        if (c->checkable) {
            act->setChecked(commands_->isChecked(c->id));
        }
        const QList<QKeySequence> keys = commands_->effectiveShortcuts(c->id);
        if (act->shortcuts() != keys) {
            act->setShortcuts(keys);
        }
        QString plain = c->title;
        plain.remove(QLatin1Char('&')).remove(QStringLiteral("…"));
        act->setToolTip(c->stage > 0 ? tr("«%1» появится в версии 0.%2").arg(plain).arg(c->stage) : plain);
    }
}

void MainWindow::setSingleKeyAccelerators(bool on) {
    Settings().set(settings::kSingleKeyAccelerators, on);
    commands_->setSingleKeysEnabled(on);
    for (int i = 0; i < documentCount(); ++i) {
        if (auto* quick = qobject_cast<QuickBar*>(document(i)->floatingBar())) {
            quick->setSingleKeyHints(on);
        }
    }
}

bool MainWindow::singleKeyAccelerators() const { return commands_->singleKeysEnabled(); }

// F8: левая панель, правая навигация, строка состояния и плавающие панели документов скрыты или видны;
// состояние — в settings.ini.
void MainWindow::setPanelsHidden(bool hidden) {
    panelsHidden_ = hidden;
    Settings().set(settings::kPanelsHidden, hidden);
    applyPanels();
    updateActions();
}

}  // namespace ot::ui
