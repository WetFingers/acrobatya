// Меню окна (эталон 6.1, 6.8): строка меню (скрыта, F9) и «Меню ▾» глобальной панели — одни и те же QMenu;
// действия добавлены в само окно, поэтому их клавиши работают и без строки меню. «Поделиться ▾» — путь в
// буфер (Проводник — ShellLauncher 2.4, почта — 9.1). Клавиши — из таблицы эталона 6.8.
#include <QAction>
#include <QActionGroup>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>

#include "services/document_session.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/tool_panel.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

struct MainWindow::Actions {
    QMenu* file = nullptr;
    QMenu* edit = nullptr;
    QMenu* view = nullptr;
    QMenu* zoom = nullptr;
    QMenu* layout = nullptr;
    QMenu* rotate = nullptr;
    QMenu* navigate = nullptr;
    QMenu* window = nullptr;
    QMenu* help = nullptr;
    QMenu* main = nullptr;   // «Меню ▾»
    QMenu* share = nullptr;  // «Поделиться ▾»

    QAction* open = nullptr;
    QAction* save = nullptr;
    QAction* saveAs = nullptr;
    QAction* close = nullptr;
    QAction* print = nullptr;
    QAction* properties = nullptr;
    QAction* quit = nullptr;
    QAction* undo = nullptr;
    QAction* redo = nullptr;
    QAction* copy = nullptr;
    QAction* selectAll = nullptr;
    QAction* home = nullptr;
    QAction* zoomIn = nullptr;
    QAction* zoomOut = nullptr;
    QAction* actualSize = nullptr;
    QAction* fitPage = nullptr;
    QAction* fitWidth = nullptr;
    QAction* fitVisible = nullptr;
    QAction* single = nullptr;
    QAction* continuous = nullptr;
    QAction* facing = nullptr;
    QAction* facingContinuous = nullptr;
    QAction* cover = nullptr;
    QAction* rotateCw = nullptr;
    QAction* rotateCcw = nullptr;
    QAction* first = nullptr;
    QAction* previous = nullptr;
    QAction* next = nullptr;
    QAction* last = nullptr;
    QAction* back = nullptr;
    QAction* forward = nullptr;
    QAction* menuBar = nullptr;
    QAction* toolPanel = nullptr;
    QAction* nextTab = nullptr;
    QAction* previousTab = nullptr;
    QAction* about = nullptr;
    QAction* copyPath = nullptr;
    QAction* explorer = nullptr;
    QAction* mail = nullptr;
    QList<QAction*> documentOnly;  // недоступны на Главной
};

namespace {

QAction* make(QWidget* owner, QMenu* menu, const QString& name,
              std::initializer_list<QKeySequence> keys = {}) {
    auto* a = new QAction(owner);
    a->setObjectName(name);
    a->setShortcuts(QList<QKeySequence>(keys));
    a->setShortcutContext(Qt::WindowShortcut);
    owner->addAction(a);  // клавиша работает и при скрытой строке меню
    if (menu != nullptr) {
        menu->addAction(a);
    }
    return a;
}

}  // namespace

void MainWindow::buildMenus() {
    actions_ = std::make_unique<Actions>();
    Actions& a = *actions_;
    QMenuBar* bar = menuBar();
    a.file = bar->addMenu(QString());
    a.edit = bar->addMenu(QString());
    a.view = bar->addMenu(QString());
    a.window = bar->addMenu(QString());
    a.help = bar->addMenu(QString());

    auto view = [this]() -> PageView* {
        DocumentView* d = currentDocument();
        return d != nullptr ? &d->view() : nullptr;
    };
    auto onView = [view](auto fn) {
        return [view, fn] {
            if (PageView* v = view()) {
                fn(*v);
            }
        };
    };

    // Файл
    a.open = make(this, a.file, QStringLiteral("doc.open"), {QKeySequence(Qt::CTRL | Qt::Key_O)});
    connect(a.open, &QAction::triggered, this, &MainWindow::openDialog);
    a.file->addSeparator();
    a.save = make(this, a.file, QStringLiteral("doc.save"), {QKeySequence(Qt::CTRL | Qt::Key_S)});
    connect(a.save, &QAction::triggered, this, &MainWindow::saveCurrent);
    a.saveAs =
        make(this, a.file, QStringLiteral("doc.saveAs"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)});
    connect(a.saveAs, &QAction::triggered, this, &MainWindow::saveCurrentAs);
    a.file->addSeparator();
    a.close = make(this, a.file, QStringLiteral("doc.close"),
                   {QKeySequence(Qt::CTRL | Qt::Key_W), QKeySequence(Qt::CTRL | Qt::Key_F4)});
    connect(a.close, &QAction::triggered, this, [this] { closeDocument(currentIndex()); });
    a.file->addSeparator();
    a.print = make(this, a.file, QStringLiteral("doc.print"), {QKeySequence(Qt::CTRL | Qt::Key_P)});
    connect(a.print, &QAction::triggered, this, [this] { notifyLater(tr("Печать"), 2); });
    a.properties = make(this, a.file, QStringLiteral("doc.properties"), {QKeySequence(Qt::CTRL | Qt::Key_D)});
    connect(a.properties, &QAction::triggered, this, [this] { notifyLater(tr("Свойства документа"), 2); });
    a.file->addSeparator();
    a.quit = make(this, a.file, QStringLiteral("app.quit"), {QKeySequence(Qt::CTRL | Qt::Key_Q)});
    connect(a.quit, &QAction::triggered, this, &QWidget::close);

    // Правка
    a.undo = make(this, a.edit, QStringLiteral("edit.undo"), {QKeySequence(Qt::CTRL | Qt::Key_Z)});
    connect(a.undo, &QAction::triggered, this, [this] {
        if (DocumentView* d = currentDocument()) {
            d->session().undo();
        }
    });
    a.redo = make(this, a.edit, QStringLiteral("edit.redo"), {QKeySequence(Qt::CTRL | Qt::Key_Y)});
    connect(a.redo, &QAction::triggered, this, [this] {
        if (DocumentView* d = currentDocument()) {
            d->session().redo();
        }
    });
    a.edit->addSeparator();
    // Копирование и «выделить всё» — у «Выделения» вида; клавиши Ctrl+C/Ctrl+A вид обрабатывает сам.
    a.copy = make(this, a.edit, QStringLiteral("edit.copy"));
    connect(a.copy, &QAction::triggered, this,
            onView([](PageView& v) { v.tools().select().copySelection(); }));
    a.selectAll = make(this, a.edit, QStringLiteral("edit.selectAll"));
    connect(a.selectAll, &QAction::triggered, this, onView([](PageView& v) {
                v.tools().setTool(ViewToolId::Select);
                v.tools().select().selectAll();
            }));

    // Вид
    a.home = make(this, a.view, QStringLiteral("view.home"));
    connect(a.home, &QAction::triggered, this, &MainWindow::showHome);
    a.view->addSeparator();
    a.zoom = a.view->addMenu(QString());
    a.zoomIn = make(this, a.zoom, QStringLiteral("view.zoomIn"),
                    {QKeySequence(Qt::CTRL | Qt::Key_Equal), QKeySequence(Qt::CTRL | Qt::Key_Plus)});
    connect(a.zoomIn, &QAction::triggered, this, onView([](PageView& v) { v.zoomIn(); }));
    a.zoomOut = make(this, a.zoom, QStringLiteral("view.zoomOut"), {QKeySequence(Qt::CTRL | Qt::Key_Minus)});
    connect(a.zoomOut, &QAction::triggered, this, onView([](PageView& v) { v.zoomOut(); }));
    a.zoom->addSeparator();
    a.actualSize =
        make(this, a.zoom, QStringLiteral("view.actualSize"), {QKeySequence(Qt::CTRL | Qt::Key_1)});
    connect(a.actualSize, &QAction::triggered, this, onView([](PageView& v) { v.fit(FitMode::ActualSize); }));
    a.fitPage = make(this, a.zoom, QStringLiteral("view.fitPage"), {QKeySequence(Qt::CTRL | Qt::Key_0)});
    connect(a.fitPage, &QAction::triggered, this, onView([](PageView& v) { v.fit(FitMode::Page); }));
    a.fitWidth = make(this, a.zoom, QStringLiteral("view.fitWidth"), {QKeySequence(Qt::CTRL | Qt::Key_2)});
    connect(a.fitWidth, &QAction::triggered, this, onView([](PageView& v) { v.fit(FitMode::Width); }));
    a.fitVisible =
        make(this, a.zoom, QStringLiteral("view.fitVisible"), {QKeySequence(Qt::CTRL | Qt::Key_3)});
    connect(a.fitVisible, &QAction::triggered, this, onView([](PageView& v) { v.fit(FitMode::Visible); }));

    a.layout = a.view->addMenu(QString());
    auto* modes = new QActionGroup(this);
    const std::pair<QAction**, PageMode> modeActions[] = {
        {&a.single, PageMode::SinglePage},
        {&a.continuous, PageMode::Continuous},
        {&a.facing, PageMode::Facing},
        {&a.facingContinuous, PageMode::FacingContinuous},
    };
    const char* modeNames[] = {"view.singlePage", "view.continuous", "view.facing", "view.facingContinuous"};
    int n = 0;
    for (const auto& [slot, mode] : modeActions) {
        QAction* act = make(this, a.layout, QLatin1String(modeNames[n++]));
        act->setCheckable(true);
        modes->addAction(act);
        connect(act, &QAction::triggered, this, onView([mode](PageView& v) { v.setPageMode(mode); }));
        *slot = act;
    }
    a.layout->addSeparator();
    a.cover = make(this, a.layout, QStringLiteral("view.coverPage"));
    a.cover->setCheckable(true);
    connect(a.cover, &QAction::toggled, this,
            [onView](bool on) { onView([on](PageView& v) { v.setCoverPage(on); })(); });

    a.rotate = a.view->addMenu(QString());
    a.rotateCw = make(this, a.rotate, QStringLiteral("view.rotateCw"),
                      {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Equal),
                       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Plus)});
    connect(a.rotateCw, &QAction::triggered, this, onView([](PageView& v) { v.rotateView(90); }));
    a.rotateCcw = make(this, a.rotate, QStringLiteral("view.rotateCcw"),
                       {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Minus)});
    connect(a.rotateCcw, &QAction::triggered, this, onView([](PageView& v) { v.rotateView(-90); }));

    a.navigate = a.view->addMenu(QString());
    a.first = make(this, a.navigate, QStringLiteral("view.firstPage"));
    connect(a.first, &QAction::triggered, this, onView([](PageView& v) { v.firstPage(); }));
    a.previous = make(this, a.navigate, QStringLiteral("view.previousPage"));
    connect(a.previous, &QAction::triggered, this, onView([](PageView& v) { v.previousPage(); }));
    a.next = make(this, a.navigate, QStringLiteral("view.nextPage"));
    connect(a.next, &QAction::triggered, this, onView([](PageView& v) { v.nextPage(); }));
    a.last = make(this, a.navigate, QStringLiteral("view.lastPage"));
    connect(a.last, &QAction::triggered, this, onView([](PageView& v) { v.lastPage(); }));
    a.navigate->addSeparator();
    a.back = make(this, a.navigate, QStringLiteral("view.back"), {QKeySequence(Qt::ALT | Qt::Key_Left)});
    connect(a.back, &QAction::triggered, this, onView([](PageView& v) { v.goBack(); }));
    a.forward =
        make(this, a.navigate, QStringLiteral("view.forward"), {QKeySequence(Qt::ALT | Qt::Key_Right)});
    connect(a.forward, &QAction::triggered, this, onView([](PageView& v) { v.goForward(); }));
    a.view->addSeparator();
    a.toolPanel =
        make(this, a.view, QStringLiteral("view.toolPanel"), {QKeySequence(Qt::SHIFT | Qt::Key_F4)});
    a.toolPanel->setCheckable(true);
    a.toolPanel->setChecked(true);
    connect(a.toolPanel, &QAction::triggered, this, [this](bool on) { toolPanel_->setCollapsed(!on); });
    connect(toolPanel_, &ToolPanel::collapsedChanged, a.toolPanel,
            [&a](bool collapsed) { a.toolPanel->setChecked(!collapsed); });
    a.menuBar = make(this, a.view, QStringLiteral("view.menuBar"), {QKeySequence(Qt::Key_F9)});
    a.menuBar->setCheckable(true);
    connect(a.menuBar, &QAction::toggled, menuBar(), &QWidget::setVisible);

    // Окно: переключение вкладок и список документов.
    a.nextTab =
        make(this, a.window, QStringLiteral("window.nextTab"), {QKeySequence(Qt::CTRL | Qt::Key_Tab)});
    connect(a.nextTab, &QAction::triggered, this, [this] {
        tabs().selectNext();
        showDocument(tabs().currentIndex());
    });
    a.previousTab =
        make(this, a.window, QStringLiteral("window.previousTab"),
             {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Tab), QKeySequence(Qt::CTRL | Qt::Key_Backtab)});
    connect(a.previousTab, &QAction::triggered, this, [this] {
        tabs().selectPrevious();
        showDocument(tabs().currentIndex());
    });
    connect(a.window, &QMenu::aboutToShow, this, [this] {
        Actions& acts = *actions_;
        for (QAction* act : acts.window->actions()) {
            if (act->property("otTab").isValid()) {
                acts.window->removeAction(act);
                act->deleteLater();
            }
        }
        if (documentCount() > 0) {
            acts.window->addSeparator()->setProperty("otTab", true);
        }
        for (int i = 0; i < documentCount(); ++i) {
            QAction* act = acts.window->addAction(tabs().tabText(i));
            act->setProperty("otTab", i);
            act->setCheckable(true);
            act->setChecked(i == currentIndex());
            connect(act, &QAction::triggered, this, [this, i] { showDocument(i); });
        }
    });

    // Справка
    a.about = make(this, a.help, QStringLiteral("help.about"));
    connect(a.about, &QAction::triggered, this, &MainWindow::about);

    // «Меню ▾» — те же меню выпадающим списком.
    a.main = new QMenu(this);
    a.main->setObjectName(QStringLiteral("mainMenu"));
    for (QMenu* m : {a.file, a.edit, a.view, a.window, a.help}) {
        a.main->addMenu(m);
    }
    globalBar_->setMainMenu(a.main);

    // «Поделиться ▾»
    a.share = new QMenu(this);
    a.share->setObjectName(QStringLiteral("shareMenu"));
    a.mail = make(this, a.share, QStringLiteral("share.mail"));
    connect(a.mail, &QAction::triggered, this, [this] { notifyLater(tr("Отправить по почте"), 9); });
    a.copyPath = make(this, a.share, QStringLiteral("share.copyPath"));
    connect(a.copyPath, &QAction::triggered, this, &MainWindow::copyPath);
    a.explorer = make(this, a.share, QStringLiteral("share.showInExplorer"));
    connect(a.explorer, &QAction::triggered, this, [this] { notifyLater(tr("Показать в Проводнике"), 2); });
    globalBar_->setShareMenu(a.share);

    a.documentOnly = {a.save,
                      a.saveAs,
                      a.close,
                      a.print,
                      a.properties,
                      a.undo,
                      a.redo,
                      a.copy,
                      a.selectAll,
                      a.zoomIn,
                      a.zoomOut,
                      a.actualSize,
                      a.fitPage,
                      a.fitWidth,
                      a.fitVisible,
                      a.single,
                      a.continuous,
                      a.facing,
                      a.facingContinuous,
                      a.cover,
                      a.rotateCw,
                      a.rotateCcw,
                      a.first,
                      a.previous,
                      a.next,
                      a.last,
                      a.back,
                      a.forward,
                      a.copyPath,
                      a.explorer,
                      a.mail};
    retranslateMenus();
    updateActions();
}

void MainWindow::retranslateMenus() {
    if (!actions_) {
        return;
    }
    Actions& a = *actions_;
    a.file->setTitle(tr("&Файл"));
    a.edit->setTitle(tr("&Правка"));
    a.view->setTitle(tr("&Вид"));
    a.zoom->setTitle(tr("&Масштаб"));
    a.layout->setTitle(tr("&Отображение страниц"));
    a.rotate->setTitle(tr("&Повернуть вид"));
    a.navigate->setTitle(tr("Пере&ход по страницам"));
    a.window->setTitle(tr("&Окно"));
    a.help->setTitle(tr("&Справка"));

    a.open->setText(tr("&Открыть…"));
    a.save->setText(tr("&Сохранить"));
    a.saveAs->setText(tr("Сохранить &как…"));
    a.close->setText(tr("&Закрыть"));
    a.print->setText(tr("&Печать…"));
    a.properties->setText(tr("Сво&йства документа…"));
    a.quit->setText(tr("В&ыход"));
    a.undo->setText(tr("&Отменить"));
    a.redo->setText(tr("&Вернуть"));
    a.copy->setText(tr("&Копировать"));
    a.selectAll->setText(tr("Выделить &всё"));
    a.home->setText(tr("&Главная"));
    a.zoomIn->setText(tr("&Увеличить"));
    a.zoomOut->setText(tr("У&меньшить"));
    a.actualSize->setText(tr("&Реальный размер"));
    a.fitPage->setText(tr("Уместить &страницу"));
    a.fitWidth->setText(tr("По &ширине"));
    a.fitVisible->setText(tr("По &видимому"));
    a.single->setText(tr("&Одна страница"));
    a.continuous->setText(tr("&Непрерывно"));
    a.facing->setText(tr("&Разворот"));
    a.facingContinuous->setText(tr("Разворот &непрерывно"));
    a.cover->setText(tr("&Титульная страница отдельно"));
    a.rotateCw->setText(tr("По &часовой стрелке"));
    a.rotateCcw->setText(tr("&Против часовой стрелки"));
    a.first->setText(tr("&Первая страница"));
    a.previous->setText(tr("П&редыдущая страница"));
    a.next->setText(tr("&Следующая страница"));
    a.last->setText(tr("П&оследняя страница"));
    a.back->setText(tr("Предыдущий &вид"));
    a.forward->setText(tr("Следующий в&ид"));
    a.toolPanel->setText(tr("Панель &инструментов"));
    a.menuBar->setText(tr("&Строка меню"));
    a.nextTab->setText(tr("&Следующая вкладка"));
    a.previousTab->setText(tr("&Предыдущая вкладка"));
    a.about->setText(tr("&О программе"));
    a.mail->setText(tr("Отправить по &почте…"));
    a.copyPath->setText(tr("&Копировать путь"));
    a.explorer->setText(tr("Показать в &Проводнике"));
    // Клавиши, которые вид обрабатывает сам (без конфликта с полями ввода), — подписью в меню.
    const std::pair<QAction*, QKeySequence> viewKeys[] = {
        {a.copy, QKeySequence(Qt::CTRL | Qt::Key_C)}, {a.selectAll, QKeySequence(Qt::CTRL | Qt::Key_A)},
        {a.first, QKeySequence(Qt::Key_Home)},        {a.previous, QKeySequence(Qt::Key_PageUp)},
        {a.next, QKeySequence(Qt::Key_PageDown)},     {a.last, QKeySequence(Qt::Key_End)},
    };
    for (const auto& [act, key] : viewKeys) {
        act->setText(act->text() + QLatin1Char('\t') + key.toString(QKeySequence::NativeText));
    }
}

void MainWindow::updateActions() {
    if (!actions_) {
        return;
    }
    Actions& a = *actions_;
    DocumentView* d = currentDocument();
    for (QAction* act : a.documentOnly) {
        act->setEnabled(d != nullptr);
    }
    if (d == nullptr) {
        return;
    }
    const UndoState undo = d->session().undoState();
    a.undo->setEnabled(undo.canUndo && !d->isViewOnly());
    a.redo->setEnabled(undo.canRedo && !d->isViewOnly());
    const PageView& v = d->view();
    QAction* mode = v.pageMode() == PageMode::SinglePage   ? a.single
                    : v.pageMode() == PageMode::Continuous ? a.continuous
                    : v.pageMode() == PageMode::Facing     ? a.facing
                                                           : a.facingContinuous;
    mode->setChecked(true);
    const QSignalBlocker block(a.cover);
    a.cover->setChecked(v.coverPage());
}

}  // namespace ot::ui
