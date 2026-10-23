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
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include "services/app_info.h"
#include "services/background_tasks.h"
#include "services/commands.h"
#include "services/convert_ops.h"
#include "services/document_registry.h"
#include "services/recent.h"
#include "services/render_pool.h"
#include "services/shell_launcher.h"
#include "services/thumbnails.h"
#include "services/tile_cache.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/home/home_page.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/quick_bar.h"
#include "ui/shell/status_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/theme/theme.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/toast.h"

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
      registry_(std::make_unique<DocumentRegistry>()) {
    setObjectName(QStringLiteral("mainWindow"));
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
    connect(globalBar_, &GlobalBar::settingsClicked, this, [this] { notifyLater(tr("Установки"), 2); });
    connect(globalBar_, &GlobalBar::searchRequested, this, [this] { notifyLater(tr("Поиск"), 2); });

    commands_ = new CommandRegistry(this);
    buildMenus();
    menuBar()->hide();
    showHome();
}

// Вкладки уходят раньше пула и кеша: вид отменяет свои тайлы, сессия закрывается в своём потоке.
MainWindow::~MainWindow() {
    disconnect(&BackgroundTasks::shared(), nullptr, this, nullptr);  // реестр задач живёт дольше окна
    while (documentCount() > 0) {
        DocumentView* view = document(0);
        const quint64 id = view->id();
        releaseView(view);
        delete view;
        tabs().removeTab(0);
        registry_->close(id);
    }
}

DocumentTabs& MainWindow::tabs() const { return *globalBar_->tabs(); }

bool MainWindow::openFile(const QString& path, int page) {
    error_.clear();
    const QFileInfo info(path);
    if (!info.isFile()) {
        reportOpenError(path, tr("Файл не найден."));
        return false;
    }
    const DocumentRegistry::Opened opened = registry_->open(info.absoluteFilePath());
    if (opened.id == 0) {
        const OpenSummary& s = opened.summary;
        QString reason;
        if (s.needsPassword) {
            reason = tr("Файл защищён паролем. Ввод пароля появится в версии 0.2.");
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
    auto* view = new DocumentView(registry_->session(opened.id), pool_.get(), cache_.get(), documents_);
    documents_->addWidget(view);
    connect(view, &DocumentView::modifiedChanged, this, [this, view](bool modified) {
        tabs().setModified(tabs().indexOf(view->id()), modified);
        updateActions();
    });
    connect(view, &DocumentView::saveAsPdfRequested, this, [this, view] { saveAsPdf(view); });
    connect(&view->session(), &DocumentSession::undoStateChanged, this, &MainWindow::updateActions,
            Qt::QueuedConnection);
    auto* quick = new QuickBar();
    quick->attach(&view->view());
    quick->setSingleKeyHints(commands_->singleKeysEnabled());
    view->setFloatingBar(quick);
    quick->setVisible(!panelsHidden_);
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
    RecentFiles::shared().touch(view->path(), opened.summary.pageCount);
    const int index = tabs().addDocument(opened.id, view->title(), view->path());
    showDocument(index);
    applyInitialView(view, opened.summary);
    emit documentOpened(view);
    return true;
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
    toolPanel_->setVisible(!panelsHidden_);
    navPane_->setDocument(view);
    navPane_->setShown(!panelsHidden_);
    status_->setDocument(view);
    updateStatusVisibility();
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

bool MainWindow::restoreLastViewEnabled() {
    QSettings settings(app::settingsPath(), QSettings::IniFormat);
    return settings.value(QLatin1String(kRestoreViewKey), true).toBool();
}

bool MainWindow::restoreSessionEnabled() {
    QSettings settings(app::settingsPath(), QSettings::IniFormat);
    return settings.value(QLatin1String(session::kRestoreKey), false).toBool();
}

void MainWindow::handleRequest(const SingleInstance::Request& request) {
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
    status_->setVisible(!panelsHidden_ && (!isHomeShown() || !BackgroundTasks::shared().isEmpty()));
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
    for (const auto& [pdfMode, pane] : panes) {
        if (mode == QLatin1String(pdfMode)) {
            navPane_->showPane(QLatin1String(pane));
        }
    }
    Q_UNUSED(view);
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

// PDF-вложение — вкладкой из временной папки (эталон 7.3; ограничения защищённого просмотра — 2.5b).
void MainWindow::openAttachmentPdf(const QString& path, const QString& title) {
    if (!openFile(path)) {
        return;
    }
    if (DocumentView* view = currentDocument()) {
        // Вид полосы — защищённый просмотр: его ограничения для вложений добавит 2.5b.
        MessageBar* bar = view->messageStack().showMessage(
            MessageKind::ProtectedView,
            tr("Вложение «%1» открыто из временной папки: изменения в нём не попадут в исходный документ.")
                .arg(title));
        bar->setObjectName(QStringLiteral("attachmentBar"));
    }
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
        if (!view.save()) {
            Toast::notify(this, tr("Не удалось сохранить «%1».").arg(view.title()), Tone::Danger);
            return false;
        }
        return true;
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
    if (!view->save()) {
        Toast::notify(this, tr("Не удалось сохранить «%1».").arg(view->title()), Tone::Danger);
    }
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
    const QString file =
        QFileDialog::getSaveFileName(this, tr("Сохранить как"), view->path(), tr("Документы PDF (*.pdf)"));
    if (file.isEmpty()) {
        return;
    }
    bool ok = false;
    try {
        ok = view->session().saveAs(file).result().ok();
    } catch (const std::exception&) {
        ok = false;
    }
    if (!ok) {
        Toast::notify(this, tr("Не удалось сохранить «%1».").arg(QFileInfo(file).fileName()), Tone::Danger);
        return;
    }
    const int index = tabs().indexOf(view->id());
    tabs().setTabText(index, view->title());
    tabs().setTabToolTip(index, QDir::toNativeSeparators(view->path()));
    retranslateTitle();
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
