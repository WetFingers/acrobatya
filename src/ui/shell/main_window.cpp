#include "ui/shell/main_window.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include <spdlog/spdlog.h>

#include "services/app_info.h"
#include "services/autosave_runner.h"
#include "services/background_tasks.h"
#include "services/commands.h"
#include "services/convert_ops.h"
#include "services/document_registry.h"
#include "services/i18n.h"
#include "services/memory_guard.h"
#include "services/print_job.h"
#include "services/recent.h"
#include "services/render_pool.h"
#include "services/settings.h"
#include "services/shell_launcher.h"
#include "services/thumbnails.h"
#include "services/tile_cache.h"
#include "ui/dialogs/password_dialog.h"
#include "ui/dialogs/properties_dialog.h"
#include "ui/dialogs/recover_dialog.h"
#include "ui/dialogs/search_window.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/command_palette.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/full_screen.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/home/home_page.h"
#include "ui/shell/link_flow.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/shell/quick_bar.h"
#include "ui/shell/read_mode.h"
#include "ui/shell/save_flow.h"
#include "ui/shell/status_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/theme/theme.h"
#include "ui/tools/print/print_dialog.h"
#include "ui/tools/print/print_runner.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

// Фильтр «Открыть»: всё, что открывает fz_open_document (не-PDF — только просмотр).
QString openFilter() {
    return MainWindow::tr(
        "Все поддерживаемые файлы (*.pdf *.xps *.oxps *.epub *.cbz *.fb2 *.png *.jpg *.jpeg "
        "*.tif *.tiff *.bmp *.txt *.htm *.html);;Документы PDF (*.pdf);;Все файлы (*)");
}

QString startDir(const QString& current) {
    if (!current.isEmpty()) {
        return QFileInfo(current).absolutePath();
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent),
      pool_(std::make_unique<RenderPool>()),
      cache_(std::make_unique<TileCache>()),
      memory_(std::make_unique<MemoryGuard>()),
      registry_(std::make_unique<DocumentRegistry>()),
      autosave_(std::make_unique<AutosaveRunner>(AutosaveRunner::defaultDir())) {
    setObjectName(QStringLiteral("mainWindow"));
    memory_->setTileCache(cache_.get());
    setAcceptDrops(true);
    resize(1440, 900);
    setMinimumSize(1024, 640);

    auto* shell = new QWidget(this);
    shell->setObjectName(QStringLiteral("shell"));
    auto* column = new QVBoxLayout(shell);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    globalBar_ = new GlobalBar(shell);
    column->addWidget(globalBar_);
    // Левая панель фиксированной ширины (320/48) — рядом со сплиттером; в сплиттере — центр и правая
    // навигация (2.4: её ширина тянется 220–480).
    auto* row = new QWidget(shell);
    auto* rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(0);
    toolPanel_ = new ToolPanel(ToolRegistry::instance(), row);
    rowLayout->addWidget(toolPanel_);
    body_ = new QSplitter(Qt::Horizontal, row);
    body_->setObjectName(QStringLiteral("shellBody"));
    body_->setChildrenCollapsible(false);
    body_->setHandleWidth(1);
    rowLayout->addWidget(body_, 1);
    center_ = new QStackedWidget(body_);
    center_->setObjectName(QStringLiteral("shellCenter"));
    // Минимум центра — свой, а не Главной (у неё широкая раскладка): иначе панель навигации не шире 220.
    center_->setMinimumWidth(Theme::sizes().centerMin);
    auto* home = new HomePage(RecentFiles::shared(), ThumbnailCache::shared(), center_);
    connect(home, &HomePage::openRequested, this, [this](const QString& path) { openFile(path); });
    connect(home, &HomePage::openDialogRequested, this, &MainWindow::openDialog);
    connect(home, &HomePage::notifyLater, this, &MainWindow::notifyLater);
    home_ = home;
    center_->addWidget(home_);
    documents_ = new QStackedWidget(center_);
    documents_->setObjectName(QStringLiteral("documents"));
    center_->addWidget(documents_);
    body_->addWidget(center_);
    body_->setStretchFactor(body_->indexOf(center_), 1);
    // Правая навигация: панель 280 — в сплиттере (тянется 220–480), полоса 44 — у правого края.
    navPane_ = new NavPane(row);
    body_->addWidget(navPane_->panel());
    body_->setStretchFactor(body_->indexOf(navPane_->panel()), 0);
    body_->setCollapsible(body_->indexOf(navPane_->panel()), false);
    rowLayout->addWidget(navPane_);
    connect(navPane_, &NavPane::panelOpenChanged, this, [this](bool open) {
        if (open) {
            // После того как сплиттер примет показ панели (он восстанавливает прежний размер при показе).
            QTimer::singleShot(0, this, &MainWindow::applyNavWidth);
        }
        updateActions();
    });
    connect(navPane_, &NavPane::currentPaneChanged, this, &MainWindow::updateActions);
    connect(navPane_, &NavPane::openPdfRequested, this, &MainWindow::openAttachmentPdf);
    connect(body_, &QSplitter::splitterMoved, this, [this] {
        if (navPane_->panel()->isVisible()) {
            navPane_->setPanelWidth(navPane_->panel()->width());
        }
    });
    column->addWidget(row, 1);
    // Строка состояния — во всю ширину окна под панелями (эталон 6.1).
    status_ = new StatusBar(shell);
    column->addWidget(status_);
    connect(&BackgroundTasks::shared(), &BackgroundTasks::changed, this, &MainWindow::updateStatusVisibility);
    setCentralWidget(shell);

    DocumentTabs* bar = globalBar_->tabs();
    connect(bar, &QTabBar::tabBarClicked, this, [this](int index) {
        if (index >= 0) {
            showDocument(index);
        }
    });
    connect(bar, &QTabBar::currentChanged, this, &MainWindow::onCurrentTabChanged);
    connect(bar, &QTabBar::tabCloseRequested, this, &MainWindow::closeDocument);
    connect(globalBar_, &GlobalBar::homeClicked, this, &MainWindow::showHome);
    connect(globalBar_, &GlobalBar::allToolsClicked, this, &MainWindow::showAllTools);
    connect(toolPanel_, &ToolPanel::toolRequested, this,
            [this](const QString& id) { ToolRegistry::instance().activate(id, currentDocument()); });
    connect(globalBar_, &GlobalBar::settingsClicked, this, [this] { showPreferences(); });
    // Поле поиска глобальной панели: в документе — панель «Найти» с этой строкой, на Главной — «Расширенный
    // поиск».
    connect(globalBar_, &GlobalBar::searchRequested, this, [this](const QString& text) {
        if (DocumentView* view = currentDocument()) {
            view->findBar().find(text);
        } else {
            showSearchWindow(text);
        }
    });

    // Полоса режима чтения — над документом (не страница стека центра).
    readBar_ = new ReadModeBar(center_);
    readBar_->hide();
    center_->installEventFilter(this);
    readBar_->installEventFilter(this);
    connect(readBar_, &ReadModeBar::exitRequested, this, [this] { setReadMode(false); });

    saveFlow_ = new SaveFlow(*this);
    linkFlow_ = new LinkFlow(*this);
    connect(saveFlow_, &SaveFlow::saved, this, [this](DocumentView* view) {
        const int index = tabs().indexOf(view->id());
        if (index >= 0) {
            tabs().setTabText(index, view->title());
            tabs().setTabToolTip(index, QDir::toNativeSeparators(view->path()));
        }
        retranslateTitle();
        updateActions();
    });
    applyAutosaveSettings();
    commands_ = new CommandRegistry(this);
    buildMenus();
    menuBar()->hide();
    // Панели — как при прошлом выходе (Shift+F4, F8).
    {
        const Settings ini;
        toolPanel_->setCollapsed(ini.get(settings::kToolPanelCollapsed));
        panelsHidden_ = ini.get(settings::kPanelsHidden);
    }
    connect(toolPanel_, &ToolPanel::collapsedChanged, this,
            [](bool collapsed) { Settings().set(settings::kToolPanelCollapsed, collapsed); });
    showHome();
}

// Вкладки уходят раньше пула и кеша: вид отменяет свои тайлы, сессия закрывается в своём потоке.
MainWindow::~MainWindow() {
    disconnect(&BackgroundTasks::shared(), nullptr, this, nullptr);  // реестр задач живёт дольше окна
    delete fullScreen_;  // его вид рисует в пуле и кеше окна — раньше них
    while (documentCount() > 0) {
        DocumentView* view = document(0);
        const quint64 id = view->id();
        autosave_->unwatch(&view->session());
        releaseView(view);
        delete view;
        tabs().removeTab(0);
        registry_->close(id);
    }
}

DocumentTabs& MainWindow::tabs() const { return *globalBar_->tabs(); }

bool MainWindow::openFile(const QString& path, int page, const QString& password) {
    error_.clear();
    const QFileInfo info(path);
    if (!info.isFile()) {
        reportOpenError(path, tr("Файл не найден."));
        return false;
    }
    const QString absolute = info.absoluteFilePath();
    const QString key = DocumentRegistry::pathKey(absolute);
    QString pass = password;
    if (pass.isEmpty()) {
        pass = passwords_.value(key);  // «запомнить на время сеанса»
    }
    DocumentRegistry::Opened opened = registry_->open(absolute, pass);
    if (opened.id == 0 && opened.summary.needsPassword && password.isEmpty()) {
        // Диалог пароля: три попытки, каждая — открыть файл с этим паролем.
        passwords_.remove(key);
        PasswordDialog dialog(
            info.fileName(),
            [this, &opened, &absolute](const QString& candidate) {
                opened = registry_->open(absolute, candidate);
                return opened.id != 0;
            },
            this);
        const bool accepted = passwordPrompt_ ? passwordPrompt_(dialog) : dialog.exec() == QDialog::Accepted;
        if (!accepted || opened.id == 0) {
            if (dialog.exhausted()) {
                reportOpenError(path, tr("Неверный пароль: три попытки исчерпаны."));
            } else {
                error_ = tr("Открытие отменено.");
            }
            return false;
        }
        if (dialog.remember()) {
            passwords_.insert(key, dialog.password());
        }
    }
    if (opened.id == 0) {
        const OpenSummary& s = opened.summary;
        QString reason;
        if (s.needsPassword) {
            reason = tr("Неверный пароль.");
        } else if (s.error.hint == QLatin1String("unsupported")) {
            reason = tr("Формат файла не поддерживается.");
        } else if (s.error.hint == QLatin1String("close-document")) {
            reason = tr("Не хватило памяти или файл недоступен для чтения.");
        } else {
            reason = tr("Файл повреждён или не является документом.");
        }
        reportOpenError(path, reason);
        return false;
    }
    if (opened.existing) {
        const int index = tabs().indexOf(opened.id);
        showDocument(index);
        if (page >= 0) {
            document(index)->view().goToPage(page);
        }
        return true;
    }
    addDocumentView(opened, page);
    return true;
}

DocumentView* MainWindow::addDocumentView(const DocumentRegistry::Opened& opened, int page) {
    auto* view = new DocumentView(registry_->session(opened.id), pool_.get(), cache_.get(), documents_);
    documents_->addWidget(view);
    connect(view, &DocumentView::modifiedChanged, this, [this, view](bool modified) {
        tabs().setModified(tabs().indexOf(view->id()), modified);
        updateActions();
    });
    connect(view, &DocumentView::readOnlyChanged, this, &MainWindow::updateActions);
    connect(view, &DocumentView::protectedViewChanged, this, &MainWindow::updateActions);
    connect(&view->view(), &PageView::linkActivated, this,
            [this, view](const links::LinkAction& action) { linkFlow_->follow(*view, action); });
    connect(view, &DocumentView::saveAsPdfRequested, this, [this, view] { saveAsPdf(view); });
    connect(&view->findBar(), &FindBar::advancedSearchRequested, this,
            [this](const QString& text) { showSearchWindow(text); });
    connect(&view->session(), &DocumentSession::undoStateChanged, this, &MainWindow::updateActions,
            Qt::QueuedConnection);
    auto* quick = new QuickBar();
    quick->attach(&view->view());
    quick->setSingleKeyHints(commands_->singleKeysEnabled());
    view->setFloatingBar(quick);
    quick->setVisible(!panelsHidden_ && !readMode_);
    connect(&view->view().tools(), &ToolController::toolChanged, this, &MainWindow::updateActions);
    bool placed = page >= 0;
    if (page >= 0) {
        view->setInitialPage(page);
    } else if (restoreLastViewEnabled()) {
        if (const std::optional<LastView> last = RecentFiles::shared().lastView(view->path())) {
            view->setInitialView(*last);  // «Восстанавливать последний вид при открытии» (как в Acrobat)
            placed = true;
        }
    }
    if (!placed && opened.summary.initial.openAction) {
        view->setInitialAction(*opened.summary.initial.openAction);  // /OpenAction файла
    }
    // Восстановленный файл (pdf_was_repaired): инкремент невозможен — запись будет полной (эталон 3.1, 4.4).
    if (opened.summary.repaired) {
        MessageBar* bar = view->messageStack().showMessage(
            MessageKind::Repaired, tr("Файл был повреждён и восстановлен при открытии. Сохранение будет "
                                      "полным: файл запишется заново."));
        bar->setObjectName(QStringLiteral("repairedBar"));
    }
    // PDF/A (pro-45): только чтение, пока не «Включить редактирование» (Установки → Документы).
    if (opened.summary.pdfa && opened.summary.isPdf && Settings().get(settings::kPdfaReadOnly)) {
        view->setReadOnly(true);
        MessageBar* bar = view->messageStack().showMessage(
            MessageKind::PdfA,
            tr("Этот файл заявляет соответствие стандарту PDF/A и открыт только для чтения, "
               "чтобы не нарушить соответствие."));
        bar->setObjectName(QStringLiteral("pdfaBar"));
        bar->addAction(tr("Включить редактирование"), QStringLiteral("enableEditing"), true);
        connect(bar, &MessageBar::actionTriggered, view, [view](const QString& id) {
            if (id == QLatin1String("enableEditing")) {
                view->setReadOnly(false);
                view->messageStack().remove(MessageKind::PdfA);
            }
        });
    }
    view->setViewStyle(ViewStyle::fromSettings(Settings()));
    // Чужой файл (Zone.Identifier, временная папка; эталон 7.5) — защищённый просмотр.
    applyProtectedView(view, protected_view::check(view->path()));
    if (opened.summary.isPdf) {
        autosave_->watch(&view->session());
    }
    RecentFiles::shared().touch(view->path(), opened.summary.pageCount);
    const int index = tabs().addDocument(opened.id, view->title(), view->path());
    showDocument(index);
    applyInitialView(view, opened.summary);
    emit documentOpened(view);
    return view;
}

bool MainWindow::hasRememberedPassword(const QString& path) const {
    return passwords_.contains(DocumentRegistry::pathKey(QFileInfo(path).absoluteFilePath()));
}

void MainWindow::applyAutosaveSettings() {
    const Settings s;
    autosave_->setIntervalMinutes(s.get(settings::kAutosaveMinutes));
    if (s.get(settings::kAutosave)) {
        autosave_->start();
    } else {
        autosave_->stop();
    }
}

RecoverDialog* MainWindow::offerRecovery() {
    if (!Settings().get(settings::kOfferRecovery)) {
        return nullptr;
    }
    std::vector<AutosaveEntry> entries = autosave_->recoverable();
    if (entries.empty()) {
        return nullptr;
    }
    auto* dialog = new RecoverDialog(std::move(entries), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &RecoverDialog::recoverRequested, this,
            [this](const std::vector<AutosaveEntry>& chosen) { recoverDocuments(chosen); });
    connect(dialog, &RecoverDialog::discardRequested, this, [this](const std::vector<AutosaveEntry>& all) {
        for (const AutosaveEntry& e : all) {
            autosave_->discard(QString::fromStdWString(e.path.wstring()));
        }
    });
    recover_ = dialog;
    dialog->open();
    return dialog;
}

RecoverDialog* MainWindow::recoverDialog() const { return recover_.data(); }

int MainWindow::recoverDocuments(const std::vector<AutosaveEntry>& entries) {
    int count = 0;
    for (const AutosaveEntry& e : entries) {
        const QString path = QString::fromStdWString(e.path.wstring());
        const DocumentRegistry::Opened opened =
            registry_->recover(QString::fromStdWString(autosave_->store().dir().wstring()), e,
                               passwords_.value(DocumentRegistry::pathKey(path)));
        if (opened.id == 0) {
            reportOpenError(path,
                            tr("Не удалось восстановить изменения: файл изменился или запись повреждена."));
            continue;
        }
        if (opened.existing) {
            showDocument(tabs().indexOf(opened.id));
            continue;
        }
        addDocumentView(opened, -1);
        ++count;
    }
    return count;
}

int MainWindow::openFiles(const QStringList& paths, int page) {
    int opened = 0;
    for (const QString& path : paths) {
        opened += openFile(path, page) ? 1 : 0;
    }
    return opened;
}

bool MainWindow::closeDocument(int index) {
    DocumentView* view = document(index);
    if (view == nullptr) {
        return false;
    }
    if (view->isModified() && !confirmClose(*view)) {
        return false;
    }
    const quint64 id = view->id();
    const bool wasHome = isHomeShown();
    RecentFiles::shared().saveView(view->path(), view->currentView());
    // Сохранено или «Не сохранять» — запись автосохранения больше не нужна.
    autosave_->discard(&view->session());
    autosave_->unwatch(&view->session());
    releaseView(view);
    documents_->removeWidget(view);
    delete view;
    tabs().removeTab(tabs().indexOf(id));
    registry_->close(id);
    if (tabs().count() == 0 || wasHome) {
        showHome();
    } else {
        showDocument(tabs().currentIndex());
    }
    return true;
}

bool MainWindow::closeAllDocuments() {
    while (documentCount() > 0) {
        if (!closeDocument(documentCount() - 1)) {
            return false;
        }
    }
    return true;
}

void MainWindow::showHome() {
    setReadMode(false);
    center_->setCurrentWidget(home_);
    home_->setFocus();
    toolPanel_->hide();
    navPane_->setShown(false);
    status_->setDocument(nullptr);
    updateStatusVisibility();
    ToolRegistry::instance().setView(nullptr);
    globalBar_->setHomeActive(true);
    retranslateTitle();
    updateActions();
    emit currentDocumentChanged(nullptr);
}

void MainWindow::showDocument(int index) {
    DocumentView* view = document(index);
    if (view == nullptr) {
        showHome();
        return;
    }
    const QSignalBlocker block(globalBar_->tabs());
    tabs().setCurrentIndex(index);
    documents_->setCurrentWidget(view);
    center_->setCurrentWidget(documents_);
    navPane_->setDocument(view);
    status_->setDocument(view);
    applyPanels();
    if (readMode_) {
        readBar_->attach(view);
        placeReadBar();
    }
    QTimer::singleShot(0, this, &MainWindow::applyNavWidth);
    ToolRegistry::instance().setView(view);
    globalBar_->setHomeActive(false);
    view->setFocus();
    retranslateTitle();
    updateActions();
    emit currentDocumentChanged(view);
}

bool MainWindow::isHomeShown() const { return center_->currentWidget() == home_; }

// Смена вкладки клавишами, перетаскиванием или после закрытия; на Главной она Главную не закрывает.
void MainWindow::onCurrentTabChanged(int index) {
    if (index < 0) {
        showHome();
    } else if (!isHomeShown()) {
        showDocument(index);
    }
}

int MainWindow::documentCount() const { return tabs().count(); }

int MainWindow::currentIndex() const { return isHomeShown() ? -1 : tabs().currentIndex(); }

DocumentView* MainWindow::currentDocument() const { return document(currentIndex()); }

DocumentView* MainWindow::document(int index) const {
    if (index < 0 || index >= tabs().count()) {
        return nullptr;
    }
    const quint64 id = tabs().idAt(index);
    for (int i = 0; i < documents_->count(); ++i) {
        auto* view = qobject_cast<DocumentView*>(documents_->widget(i));
        if (view != nullptr && view->id() == id) {
            return view;
        }
    }
    return nullptr;
}

DocumentSession* MainWindow::session() const {
    DocumentView* view = currentDocument();
    return view != nullptr ? &view->session() : nullptr;
}

int MainWindow::currentPage() const {
    DocumentView* view = currentDocument();
    return view != nullptr ? view->view().currentPage() : 0;
}

SessionState MainWindow::sessionState() const {
    SessionState state;
    for (int i = 0; i < documentCount(); ++i) {
        DocumentView* view = document(i);
        state.tabs << SessionTab{view->path(), view->view().currentPage()};
    }
    state.current = currentIndex();
    return state;
}

void MainWindow::restoreSession(const SessionState& state) {
    QString current;
    for (int i = 0; i < state.tabs.size(); ++i) {
        const SessionTab& tab = state.tabs[i];
        if (!QFileInfo::exists(tab.path)) {
            continue;  // файл удалили или переименовали — вкладки нет
        }
        if (openFile(tab.path, tab.page) && i == state.current) {
            current = tab.path;
        }
    }
    if (!current.isEmpty()) {
        showDocument(tabs().indexOf(registry_->findByPath(current)));
    } else {
        showHome();
    }
}

bool MainWindow::restoreLastViewEnabled() { return Settings().get(settings::kRestoreLastView); }

bool MainWindow::restoreSessionEnabled() { return Settings().get(settings::kRestoreSession); }

void MainWindow::handleRequest(const SingleInstance::Request& request) {
    if (request.print) {
        for (const QString& file : request.files) {
            printSilently(file, request.printer);
        }
        return;
    }
    if (isMinimized()) {
        showNormal();
    }
    raise();
    activateWindow();
    openFiles(request.files, request.page > 0 ? request.page - 1 : -1);
}

// «Все инструменты»: левая панель развёрнута и показывает список (активный инструмент выключается); с Главной
// — к текущей вкладке.
void MainWindow::showAllTools() {
    if (isHomeShown() && documentCount() > 0) {
        showDocument(tabs().currentIndex());
    }
    if (panelsHidden_) {
        setPanelsHidden(false);
    }
    toolPanel_->setCollapsed(false);
    toolPanel_->showList();
    toolPanel_->setVisible(!isHomeShown() || documentCount() == 0);
}

// Вкладка уходит: инструмент, включённый на ней, выключается до удаления вида.
void MainWindow::releaseView(DocumentView* view) {
    if (ToolRegistry::instance().view() == view) {
        ToolRegistry::instance().setView(nullptr);
    }
    if (navPane_ != nullptr && navPane_->document() == view) {
        navPane_->setDocument(nullptr);
    }
    if (status_ != nullptr && status_->document() == view) {
        status_->setDocument(nullptr);
    }
}

// Строка состояния: с документом — всегда (кроме F8); на Главной — только пока идёт фоновая задача.
void MainWindow::updateStatusVisibility() {
    status_->setVisible(!panelsHidden_ && !readMode_ &&
                        (!isHomeShown() || !BackgroundTasks::shared().isEmpty()));
}

// Начальный вид файла (view-47): /OpenAction — если страницу не задали и последний вид не восстановлен;
// /PageMode — панель навигации (закладки, миниатюры, слои, вложения); /PageLayout применяет PageView;
// DisplayDocTitle — заголовок окна (retranslateTitle). Hide* из /ViewerPreferences не применяются (окно
// программы — не встроенный просмотр).
void MainWindow::applyInitialView(DocumentView* view, const OpenSummary& summary) {
    const InitialView& v = summary.initial;
    const QString mode = QString::fromStdString(v.pageMode);
    const std::pair<const char*, const char*> panes[] = {{"UseOutlines", "bookmarks"},
                                                         {"UseThumbs", "thumbnails"},
                                                         {"UseOC", "layers"},
                                                         {"UseAttachments", "attachments"}};
    // /PageMode /FullScreen — панель после выхода из полного экрана — /NonFullScreenPageMode.
    const QString panelMode =
        mode == QLatin1String("FullScreen") ? QString::fromStdString(v.nonFullScreenPageMode) : mode;
    for (const auto& [pdfMode, pane] : panes) {
        if (panelMode == QLatin1String(pdfMode)) {
            navPane_->showPane(QLatin1String(pane));
        }
    }
    // Документ просит полный экран — не сразу, а полосой с кнопкой (как «Предупреждать, когда документ
    // запрашивает полноэкранный режим» в Acrobat).
    if (mode == QLatin1String("FullScreen")) {
        MessageBar* bar =
            view->messageStack().add(Tone::Info, tr("Документ просит открыться в полноэкранном режиме."));
        bar->setObjectName(QStringLiteral("fullScreenRequestBar"));
        bar->addAction(tr("Полноэкранный режим"), QStringLiteral("fullScreen"), true);
        connect(bar, &MessageBar::actionTriggered, this, [this, bar] {
            bar->closeButton()->click();
            enterFullScreen();
        });
    }
}

FullScreenWindow* MainWindow::fullScreenWindow() const { return fullScreen_.data(); }

PreferencesDialog* MainWindow::showPreferences(const QString& category) {
    if (preferences_ == nullptr) {
        preferences_ = new PreferencesDialog(this);
        connect(preferences_, &PreferencesDialog::applied, this, &MainWindow::applyPreferences);
    }
    if (!category.isEmpty()) {
        preferences_->showCategory(category);
    }
    preferences_->open();  // окно-модальный, без своего цикла событий
    preferences_->raise();
    return preferences_;
}

PreferencesDialog* MainWindow::preferencesDialog() const { return preferences_.data(); }

void MainWindow::applyPreferences() {
    const Settings s;
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        const ThemeChoice choice = parseThemeChoice(s.get(settings::kTheme)).value_or(ThemeChoice::System);
        const double scale = s.get(settings::kLargeElements) ? kLargeElementsScale : 1.0;
        if (choice != Theme::instance().choice() || !qFuzzyCompare(scale, Theme::instance().scale())) {
            Theme::apply(*app, choice, scale);
        }
    }
    const QString language = i18n::resolve(s.get(settings::kLanguage));
    if (language != i18n::current()) {
        i18n::install(language);  // окна получат LanguageChange
    }
    setSingleKeyAccelerators(s.get(settings::kSingleKeyAccelerators));
    applyAutosaveSettings();
    applyViewStyle();
    status_->reloadSettings();
    for (Tool* tool : ToolRegistry::instance().tools()) {
        tool->settingsChanged();  // 3.4: «Редактирование» — шрифт нового текста, запасной, системные
    }
    updateActions();
}

void MainWindow::applyViewStyle() {
    const ViewStyle style = ViewStyle::fromSettings(Settings());
    for (int i = 0; i < documentCount(); ++i) {
        document(i)->setViewStyle(style);
    }
    if (fullScreen_ != nullptr && currentDocument() != nullptr) {
        fullScreen_->view().setRenderStyle(currentDocument()->view().renderStyle());
    }
}

QString MainWindow::recolorMode() const { return Settings().get(settings::kRecolor); }

void MainWindow::setRecolorMode(const QString& mode) {
    Settings s;
    s.set(settings::kRecolor, s.get(settings::kRecolor) == mode ? QStringLiteral("none") : mode);
    applyViewStyle();
    updateActions();
}

PropertiesDialog* MainWindow::showProperties(int tab) {
    DocumentView* view = currentDocument();
    if (view == nullptr) {
        return nullptr;
    }
    if (properties_ != nullptr) {
        properties_->close();
    }
    auto* dialog = new PropertiesDialog(*view, !view->canEdit() || !view->permissions().edit, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (tab >= 0) {
        dialog->showTab(static_cast<PropertiesDialog::Tab>(tab));
    }
    properties_ = dialog;
    dialog->open();  // окно-модальный, без своего цикла событий
    return dialog;
}

PropertiesDialog* MainWindow::propertiesDialog() const { return properties_.data(); }

PrintDialog* MainWindow::showPrintDialog(const QList<int>& pages, const QString& printer) {
    DocumentView* view = currentDocument();
    if (view == nullptr || !view->permissions().print) {
        return nullptr;
    }
    if (print_ != nullptr) {
        print_->close();
    }
    auto* dialog = new PrintDialog(*view, pages, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    if (!printer.isEmpty()) {
        dialog->setPrinter(printer);
    }
    print_ = dialog;
    dialog->open();  // окно-модальный, без своего цикла событий
    return dialog;
}

PrintDialog* MainWindow::printDialog() const { return print_.data(); }

PrintRunner* MainWindow::printSilently(const QString& path, const QString& printer) {
    auto session = std::make_shared<DocumentSession>();
    if (!session->open(path)) {
        Toast::notify(this,
                      tr("Не удалось напечатать «%1»: файл не открылся.").arg(QFileInfo(path).fileName()),
                      Tone::Danger);
        return nullptr;
    }
    const Settings st;
    PrintSettings s;
    s.title = QFileInfo(path).fileName();
    s.maxDpi = st.get(settings::kPrintQuality);
    s.layout.size = printScaleFromName(st.get(settings::kPrintScale));
    std::unique_ptr<QPrinter> qprinter;
    std::unique_ptr<PrintDevice> device = PrintDevices::open(printer);
    if (device == nullptr) {
        const QPrinterInfo info =
            printer.isEmpty() ? QPrinterInfo::defaultPrinter() : QPrinterInfo::printerInfo(printer);
        if (info.isNull()) {
            Toast::notify(this, tr("Принтер «%1» не найден.").arg(printer), Tone::Danger);
            return nullptr;
        }
        qprinter = std::make_unique<QPrinter>(info, QPrinter::HighResolution);
        device = makePrinterDevice(*qprinter);
    }
    auto* runner = new PrintRunner(std::move(session), s, std::move(qprinter), std::move(device), this, this);
    runner->start();
    return runner;
}

SearchWindow* MainWindow::showSearchWindow(const QString& text) {
    if (search_ == nullptr) {
        search_ = new SearchWindow(*this);
    }
    if (!text.isEmpty()) {
        search_->setQuery(text);
    }
    search_->show();
    search_->raise();
    search_->activateWindow();
    search_->queryField()->setFocus();
    return search_;
}

SearchWindow* MainWindow::searchWindow() const { return search_.data(); }

CommandPalette* MainWindow::showCommandPalette() {
    if (palette_ == nullptr) {
        palette_ = new CommandPalette(*commands_, this);
        connect(palette_, &CommandPalette::commandTriggered, this, &MainWindow::updateActions);
    }
    palette_->reset();
    palette_->open();
    palette_->raise();
    palette_->activateWindow();
    return palette_;
}

CommandPalette* MainWindow::commandPalette() const { return palette_.data(); }

void MainWindow::enterFullScreen() {
    DocumentView* view = currentDocument();
    if (view == nullptr) {
        return;
    }
    if (fullScreen_ != nullptr) {
        fullScreen_->raise();
        fullScreen_->activateWindow();
        return;
    }
    auto* fs = new FullScreenWindow(view->sessionPtr(), view->pool(), cache_.get(),
                                    view->view().currentPage(), this);
    const quint64 id = view->id();
    connect(fs, &FullScreenWindow::closed, this, [this, id](int page) {
        // Вид документа — на показанной странице; окно — снова активное.
        for (int i = 0; i < documentCount(); ++i) {
            if (document(i)->id() == id) {
                document(i)->view().goToPage(page);
            }
        }
        activateWindow();
    });
    fullScreen_ = fs;
    fs->view().setRenderStyle(view->view().renderStyle());  // ночной режим и overprint — как во вкладке
    fs->start(screen());
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    // Центр сменил размер или полоса — ширину (другая подпись страницы, масштаба) — снова по центру.
    if ((watched == center_ || watched == readBar_) && event->type() == QEvent::Resize) {
        placeReadBar();
    }
    // Режим чтения: Esc — выход, раньше инструментов вида (вид забирает Esc себе через ShortcutOverride).
    if (readMode_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape &&
        static_cast<QKeyEvent*>(event)->modifiers() == Qt::NoModifier &&
        QApplication::activePopupWidget() == nullptr && QApplication::activeModalWidget() == nullptr &&
        QApplication::activeWindow() == this) {
        setReadMode(false);
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

// Ширина панели навигации — из settings.ini (её пишет перетаскивание разделителя).
void MainWindow::applyNavWidth() {
    // До показа окна сплиттер не разложен: ширину даёт sizeHint панели (тот же navpane/width).
    if (!isVisible() || !navPane_->panel()->isVisibleTo(this)) {
        return;
    }
    const QList<int> sizes = body_->sizes();
    if (sizes.size() < 2) {
        return;
    }
    const int total = sizes[0] + sizes[1];
    if (total <= 0) {
        return;  // окно ещё не разложено — ширину даст sizeHint панели
    }
    const int w = navPane_->panelWidth();
    body_->setSizes({std::max(0, total - w), w});
}

// PDF-вложение — вкладкой из временной папки в защищённом просмотре (эталон 7.3, 7.5).
void MainWindow::openAttachmentPdf(const QString& path, const QString& title) {
    if (!openFile(path)) {
        return;
    }
    DocumentView* view = currentDocument();
    if (view == nullptr) {
        return;
    }
    const protected_view::Origin origin = protected_view::check(path, true);
    if (origin != protected_view::Origin::None) {
        applyProtectedView(view, origin, title);
        return;
    }
    // Защищённый просмотр выключен или вложение уже своё — только напоминание о временной папке.
    MessageBar* bar = view->addMessage(
        Tone::Info,
        tr("Вложение «%1» открыто из временной папки: изменения в нём не попадут в исходный документ.")
            .arg(title));
    bar->setObjectName(QStringLiteral("attachmentBar"));
}

void MainWindow::applyProtectedView(DocumentView* view, protected_view::Origin origin, const QString& title) {
    using protected_view::Origin;
    if (view == nullptr || origin == Origin::None) {
        return;
    }
    view->setProtectedView(origin);
    QString text;
    switch (origin) {
        case Origin::Internet:
            text =
                tr("Защищённый просмотр: файл получен из Интернета. Правка и сохранение отключены, ссылки и "
                   "вложения не открываются.");
            break;
        case Origin::TempFolder:
            text = tr(
                "Защищённый просмотр: файл открыт из временной папки (например, вложение письма). Правка и "
                "сохранение отключены, ссылки и вложения не открываются.");
            break;
        case Origin::Attachment:
            text = tr("Защищённый просмотр: вложение «%1» открыто из временной папки. Правка и сохранение "
                      "отключены, ссылки и вложения не открываются.")
                       .arg(title);
            break;
        case Origin::AllFiles:
        case Origin::None:
            text =
                tr("Защищённый просмотр: правка и сохранение отключены, ссылки и вложения не открываются.");
            break;
    }
    MessageBar* bar = view->messageStack().showMessage(MessageKind::ProtectedView, text);
    bar->setObjectName(origin == Origin::Attachment ? QStringLiteral("attachmentBar")
                                                    : QStringLiteral("protectedViewBar"));
    bar->addAction(tr("Включить все функции"), QStringLiteral("enableAll"), true);
    connect(bar, &MessageBar::actionTriggered, view, [this, view](const QString& id) {
        if (id == QLatin1String("enableAll")) {
            enableAllFeatures(view);
        }
    });
    updateActions();
}

void MainWindow::enableAllFeatures(DocumentView* view) {
    if (view == nullptr || !view->isProtectedView()) {
        return;
    }
    // Файл становится своим (отпечаток содержимого в state.sqlite); Zone.Identifier не трогаем.
    if (!protected_view::trust(view->path())) {
        spdlog::warn("protected view: could not remember {}", view->path().toStdString());
    }
    view->setProtectedView(protected_view::Origin::None);
    view->messageStack().remove(MessageKind::ProtectedView);
    updateActions();
}

// «Поделиться → Показать в Проводнике» — только через ShellLauncher (эталон 7.3, 8.3).
void MainWindow::showInExplorer() {
    if (DocumentView* view = currentDocument()) {
        if (!ShellLauncher::instance().showInExplorer(view->path())) {
            Toast::notify(this, tr("Не удалось открыть Проводник."), Tone::Danger);
        }
    }
}

void MainWindow::notifyLater(const QString& what, int stage) {
    Toast::notify(this, tr("«%1» появится в версии 0.%2.").arg(what).arg(stage), Tone::Info);
}

bool MainWindow::confirmClose(DocumentView& view) {
    CloseAnswer answer = CloseAnswer::Cancel;
    if (confirm_) {
        answer = confirm_(view);
    } else {
        QMessageBox box(QMessageBox::Question, tr("АкроБатя"),
                        tr("Сохранить изменения в «%1» перед закрытием?").arg(view.title()),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.button(QMessageBox::Save)->setText(tr("&Сохранить"));
        box.button(QMessageBox::Discard)->setText(tr("&Не сохранять"));
        box.button(QMessageBox::Cancel)->setText(tr("Отмена"));
        box.setDefaultButton(QMessageBox::Save);
        box.setEscapeButton(QMessageBox::Cancel);
        const int r = box.exec();
        answer = r == QMessageBox::Save      ? CloseAnswer::Save
                 : r == QMessageBox::Discard ? CloseAnswer::Discard
                                             : CloseAnswer::Cancel;
    }
    if (answer == CloseAnswer::Save) {
        return saveFlow_->save(view);  // ошибку и подписи объясняет SaveFlow
    }
    return answer == CloseAnswer::Discard;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!closing_) {
        const SessionState state = sessionState();
        for (int i = 0; i < documentCount(); ++i) {
            DocumentView* view = document(i);
            if (view->isModified()) {
                showDocument(i);
                if (!confirmClose(*view)) {
                    event->ignore();
                    return;
                }
            }
        }
        session::save(state);
        for (int i = 0; i < documentCount(); ++i) {
            RecentFiles::shared().saveView(document(i)->path(), document(i)->currentView());
            autosave_->discard(&document(i)->session());  // сохранено или «Не сохранять»
        }
        closing_ = true;
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslateTitle();
        retranslateMenus();
    }
    QMainWindow::changeEvent(event);
}

// Первый показ: сплиттер разложен — панели навигации её ширина из settings.ini.
void MainWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    QTimer::singleShot(0, this, &MainWindow::applyNavWidth);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                event->acceptProposedAction();
                return;
            }
        }
    }
    QMainWindow::dragEnterEvent(event);
}

void MainWindow::dropEvent(QDropEvent* event) {
    QStringList files;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            files << url.toLocalFile();
        }
    }
    if (files.isEmpty()) {
        QMainWindow::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    openFiles(files);
}

void MainWindow::retranslateTitle() {
    DocumentView* view = currentDocument();
    if (view == nullptr) {
        setWindowTitle(tr("АкроБатя"));
        return;
    }
    // /ViewerPreferences /DisplayDocTitle — заголовок документа вместо имени файла.
    const OpenSummary s = view->session().summary();
    const QString title =
        s.initial.displayDocTitle ? QString::fromStdString(s.initial.title).trimmed() : QString();
    setWindowTitle(tr("%1 — АкроБатя").arg(title.isEmpty() ? view->title() : title));
}

void MainWindow::openDialog() {
    DocumentView* view = currentDocument();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Открыть"), startDir(view != nullptr ? view->path() : QString()), openFilter());
    openFiles(files);
}

void MainWindow::saveCurrent() {
    DocumentView* view = currentDocument();
    if (view == nullptr) {
        return;
    }
    if (view->isViewOnly()) {
        saveAsPdf(view);
        return;
    }
    saveFlow_->save(*view);
}

void MainWindow::saveCurrentAs() {
    DocumentView* view = currentDocument();
    if (view == nullptr) {
        return;
    }
    if (view->isViewOnly()) {
        saveAsPdf(view);
        return;
    }
    saveFlow_->saveAs(*view);
}

// Не-PDF → новый PDF рядом (страницы как есть), затем он открывается во вкладке (view-01).
void MainWindow::saveAsPdf(DocumentView* view) {
    const QFileInfo source(view->path());
    const QString suggested =
        source.absoluteDir().filePath(source.completeBaseName() + QStringLiteral(".pdf"));
    const QString file =
        QFileDialog::getSaveFileName(this, tr("Сохранить как PDF"), suggested, tr("Документы PDF (*.pdf)"));
    if (file.isEmpty()) {
        return;
    }
    QPointer<MainWindow> self(this);
    // Фоновая задача строки состояния: отмена прерывает запись между страницами.
    std::shared_ptr<DocumentSession> session = view->sessionPtr();
    const quint64 task = BackgroundTasks::shared().start(
        tr("Сохранение «%1» как PDF").arg(QFileInfo(file).fileName()), [session] { session->cancelAll(); });
    convert::saveAsPdf(view->session(), file)
        .then(this,
              [self, file, task] {
                  BackgroundTasks::shared().finish(task);
                  if (self != nullptr) {
                      self->openFile(file);
                  }
              })
        .onFailed(this, [self, file, task] {
            BackgroundTasks::shared().finish(task);
            if (self != nullptr) {
                Toast::notify(self, tr("Не удалось сохранить «%1».").arg(QFileInfo(file).fileName()),
                              Tone::Danger);
            }
        });
}

void MainWindow::copyPath() {
    if (DocumentView* view = currentDocument()) {
        QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(view->path()));
        Toast::notify(this, tr("Путь скопирован."), Tone::Ok);
    }
}

void MainWindow::about() {
    QMessageBox::about(this, tr("О программе"),
                       tr("АкроБатя %1\nPDF-движок MuPDF %2 (AGPL-3.0)\nQt %3 (LGPL-3.0)")
                           .arg(ot::app::version(), ot::app::mupdfVersion(), ot::app::qtVersion()));
}

void MainWindow::reportOpenError(const QString& path, const QString& reason) {
    error_ = reason;
    auto* box = new QMessageBox(QMessageBox::Warning, tr("АкроБатя"),
                                tr("Не удалось открыть «%1».").arg(QFileInfo(path).fileName()),
                                QMessageBox::Ok, this);
    box->setObjectName(QStringLiteral("openErrorBox"));
    box->setInformativeText(reason);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();  // не блокирует: ошибка одного файла не останавливает остальные
}

}  // namespace ot::ui
