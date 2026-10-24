// Главное окно (эталон 6.1 «как в новом Acrobat», 3.1 «Открытие», 4.2; view-27, view-31): QMainWindow без
// доков — сверху GlobalBar 48 px (Меню ▾, Главная, вкладки, поиск, Все инструменты, Поделиться ▾, ⚙), ниже
// свои панели в QSplitter: слева ToolPanel (2.3, пункт 2), в центре QStackedWidget — Главная / вкладка
// документа (DocumentView), справа NavPane и внизу StatusBar (2.4). QMenuBar скрыт, F9 показывает его;
// команды живут в окне, поэтому их клавиши работают и без строки меню. Документы — DocumentRegistry (один
// файл — одна вкладка: повторное открытие делает вкладку текущей), пул и кеш тайлов — общие у окна. Открытие:
// аргументы, Ctrl+O, перетаскивание, второй экземпляр (SingleInstance → handleRequest). Закрытие вкладки или
// окна с правками — «Сохранить / Не сохранять / Отмена»; при закрытии окна вкладки пишутся в сеанс
// (state.sqlite), при запуске он открывается снова по настройке general/restoreSession.
#pragma once

#include <QMainWindow>
#include <QPointer>

#include <QHash>

#include <functional>
#include <memory>
#include <vector>

#include "services/document_registry.h"
#include "services/document_session.h"
#include "services/protected_view.h"
#include "services/session_store.h"
#include "services/settings_keys.h"
#include "services/single_instance.h"

class QSplitter;
class QStackedWidget;

namespace ot {
class AutosaveRunner;
class CommandRegistry;
class DocumentRegistry;
class DocumentSession;
class MemoryGuard;
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class CommandPalette;
class DocumentTabs;
class DocumentView;
class GlobalBar;
class FullScreenWindow;
class LinkFlow;
class NavPane;
class PasswordDialog;
class PreferencesDialog;
class PrintDialog;
class PrintRunner;
class PropertiesDialog;
class ReadModeBar;
class RecoverDialog;
class SaveFlow;
class SearchWindow;
class StatusBar;
class ToolPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class CloseAnswer { Save, Discard, Cancel };
    using CloseConfirm = std::function<CloseAnswer(DocumentView&)>;

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Открыть файл во вкладке (уже открытый — его вкладка становится текущей); page — с нуля, -1 — первая;
    // password — для зашифрованного (--script doc.open; диалог пароля — 2.5). false — не открылся: окно
    // сообщает причину (неблокирующий диалог), errorString().
    bool openFile(const QString& path, int page = -1, const QString& password = {});
    int openFiles(const QStringList& paths, int page = -1);  // сколько открылось
    // Закрыть вкладку (с подтверждением, если есть правки); false — «Отмена» или нет вкладки.
    bool closeDocument(int index);
    bool closeAllDocuments();
    void showHome();
    void showDocument(int index);
    bool isHomeShown() const;

    int documentCount() const;
    int currentIndex() const;  // -1 — Главная
    DocumentView* currentDocument() const;
    DocumentView* document(int index) const;
    DocumentTabs& tabs() const;
    GlobalBar& globalBar() const { return *globalBar_; }
    ToolPanel& toolPanel() const { return *toolPanel_; }
    // Правая навигация (2.4): полоса 44 и панель 280 (в сплиттере, ширина 220–480).
    NavPane& navPane() const { return *navPane_; }
    // Строка состояния 32 (2.4): страница, масштаб, раскладка, поворот, индикаторы, фоновые задачи.
    StatusBar& status() const { return *status_; }
    DocumentRegistry& registry() const { return *registry_; }
    // Реестр команд окна (меню, клавиши, палитра 2.4b, --script 2.4b, страж unit.guard.hotkeys).
    CommandRegistry& commands() const { return *commands_; }
    QWidget* homePage() const { return home_; }
    QString errorString() const { return error_; }

    // Совместимость с каркасом этапа 0: сессия и страница текущей вкладки.
    DocumentSession* session() const;
    int currentPage() const;

    // Сеанс: вкладки и текущая; restore открывает существующие файлы.
    SessionState sessionState() const;
    void restoreSession(const SessionState& state);
    static bool restoreSessionEnabled();
    // «Восстанавливать последний вид при открытии» (Установки → Документы; вкл. по умолчанию).
    static constexpr const char* kRestoreViewKey = settings::kRestoreLastView.name;
    static bool restoreLastViewEnabled();
    // Состояние панелей между запусками (2.4): F8 — все скрыты, Shift+F4 — левая свёрнута (F4 —
    // navpane/open).
    static constexpr const char* kPanelsHiddenKey = settings::kPanelsHidden.name;
    static constexpr const char* kToolPanelCollapsedKey = settings::kToolPanelCollapsed.name;
    // Контекст команд режима чтения: Esc — «выход» (view.exitMode).
    static constexpr char kReadModeContext[] = "readMode";

    // Второй экземпляр передал файлы: открыть и поднять окно; запрос печати (`/p`, `/pt`, 2.6) — напечатать
    // без вкладки и без окна поверх (printSilently).
    void handleRequest(const ot::SingleInstance::Request& request);
    // Напечатать файл без диалога и вкладки (Проводник «Печать» при открытой программе): все страницы,
    // масштаб из Установок, принтер printer (пусто — по умолчанию); итог — тост. nullptr — файл не открылся
    // (тост).
    PrintRunner* printSilently(const QString& path, const QString& printer = {});

    // Ответ на «Сохранить изменения?» (тесты подставляют свой; по умолчанию — QMessageBox).
    void setCloseConfirm(CloseConfirm confirm) { confirm_ = std::move(confirm); }

    // Сохранение (2.5, save_flow.h): Ctrl+S, Ctrl+Shift+S, «Сохранить» при закрытии.
    SaveFlow& saveFlow() const { return *saveFlow_; }
    // Ссылки и закладки наружу (2.5b, link_flow.h): подтверждение /URI с памятью по сайту, /GoToR, запрет
    // /Launch, защищённый просмотр.
    LinkFlow& linkFlow() const { return *linkFlow_; }
    // Защищённый просмотр вкладки (2.5b; services/protected_view.h): только чтение и жёлтая полоса
    // «[Включить все функции]» — она делает файл своим (state.sqlite) и снимает ограничения. title — имя
    // вложения (Origin::Attachment). Origin::None — ничего.
    void applyProtectedView(DocumentView* view, protected_view::Origin origin, const QString& title = {});
    // «Включить все функции» вкладки (кнопка полосы).
    void enableAllFeatures(DocumentView* view);
    // Стиль рендера всех вкладок и полного экрана из settings.ini (2.5b; ui/view/view_style.h): замена
    // цветов, сглаживание, тонкие линии, предпросмотр наложения. Зовут Установки («ОК») и команды «Вид».
    void applyViewStyle();
    // «Вид → Ночной режим» / «Высокая контрастность» (reading/recolor): повторный выбор — снять замену.
    void setRecolorMode(const QString& mode);
    QString recolorMode() const;
    // Автосохранение открытых PDF (services/autosave_runner.h; Установки → Документы): «Не сохранять» и
    // закрытие удаляют запись документа.
    AutosaveRunner& autosave() const { return *autosave_; }
    // Пароль при открытии (2.5, password_dialog.h: три попытки, «запомнить на время сеанса» — только в памяти
    // окна); тесты подставляют ответ, по умолчанию — exec(). true — диалог принят.
    using PasswordPrompt = std::function<bool(PasswordDialog&)>;
    void setPasswordPrompt(PasswordPrompt prompt) { passwordPrompt_ = std::move(prompt); }
    bool hasRememberedPassword(const QString& path) const;
    // При запуске (2.5): записи автосохранения → «Восстановить несохранённые изменения?» (open(), не
    // блокирует); nullptr — записей нет или «Предлагать восстановление» выключено.
    RecoverDialog* offerRecovery();
    RecoverDialog* recoverDialog() const;
    // «Мало памяти»: store MuPDF и кеш тайлов окна (тесты — счётчик срабатываний).
    MemoryGuard& memoryGuard() const { return *memory_; }
    // Открыть документы с восстановленными правками (файл не сохранён, история отмены — из журнала).
    int recoverDocuments(const std::vector<AutosaveEntry>& entries);

    // «Просмотреть подписанную версию» (4.6; sign-28): файл подписанной ревизии path (временный, удаляется
    // при закрытии вкладки) — новой вкладкой «только чтение» с полосой о том, что это за версия; защищённый
    // просмотр — как у исходной вкладки (файл — часть её документа). nullptr — не открылся.
    DocumentView* openSignedVersion(DocumentView& source, const QString& path, const QString& label);

    // Сообщение «появится в версии 0.N» — тостом над окном (заглушки будущих этапов).
    void notifyLater(const QString& what, int stage);

    // Однобуквенные ускорители H, V, Z, Shift+Z, X… (эталон 6.8; настройка, выкл. по умолчанию).
    void setSingleKeyAccelerators(bool on);
    bool singleKeyAccelerators() const;
    // F8: все панели (левая, плавающие; правая и строка состояния — 2.4) скрыты.
    void setPanelsHidden(bool hidden);
    bool panelsHidden() const { return panelsHidden_; }

    // Режим чтения (Ctrl+H; read_mode.cpp): только холст и тонкая полоса внизу; Esc — назад, панели как были.
    void setReadMode(bool on);
    bool isReadMode() const { return readMode_; }
    ReadModeBar* readModeBar() const { return readBar_; }
    // Полный экран (Ctrl+L; full_screen.h): окно без рамки на мониторе окна; выход — вид на показанной
    // странице.
    void enterFullScreen();
    FullScreenWindow* fullScreenWindow() const;

    // Установки (Ctrl+K, ⚙; preferences/): диалог создаётся при первом вызове, open() — не блокирует цикл
    // событий; category — id категории («general», «pageDisplay»…), пусто — прежняя. «ОК» → applyPreferences.
    PreferencesDialog* showPreferences(const QString& category = {});
    PreferencesDialog* preferencesDialog() const;
    // Применить settings.ini к работающему окну: тема и «Крупные элементы», язык, однобуквенные ускорители,
    // логические номера страниц (зовёт «ОК» Установок).
    void applyPreferences();
    // «Свойства документа» (Ctrl+D; dialogs/properties_dialog.h) текущей вкладки: новый диалог (open(),
    // закрытие удаляет его); tab — вкладка (PropertiesDialog::Tab), −1 — «Описание». Нет документа — nullptr.
    PropertiesDialog* showProperties(int tab = -1);
    PropertiesDialog* propertiesDialog() const;
    // «Печать» (Ctrl+P; tools/print/print_dialog.h, 2.6) текущей вкладки: новый диалог (open(), закрытие
    // удаляет его); pages — заранее выбранные страницы («Выделенные»: миниатюры, страницы закладки), пусто —
    // по выделению миниатюр; printer — сразу этот принтер. Нет документа или печать запрещена — nullptr.
    PrintDialog* showPrintDialog(const QList<int>& pages = {}, const QString& printer = {});
    PrintDialog* printDialog() const;
    // «Расширенный поиск» (Ctrl+Shift+F; dialogs/search_window.h): окно создаётся при первом вызове, не
    // модальное; text — строка поиска (пусто — прежняя).
    SearchWindow* showSearchWindow(const QString& text = {});
    SearchWindow* searchWindow() const;
    // Палитра команд (Ctrl+Shift+P; command_palette.h) — команды реестра окна.
    CommandPalette* showCommandPalette();
    CommandPalette* commandPalette() const;

signals:
    void readModeChanged(bool on);
    void documentOpened(ot::ui::DocumentView* view);
    void currentDocumentChanged(ot::ui::DocumentView* view);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    // menus.cpp — команды окна в реестре, меню из реестра («Меню ▾», строка меню, «Поделиться ▾»), клавиши.
    void registerCommands();
    void registerToolCommands();
    void buildMenus();
    void rebuildMenus();
    void retranslateMenus();
    void updateActions();

    void retranslateTitle();
    void openDialog();
    void saveCurrent();
    void saveCurrentAs();
    void saveAsPdf(DocumentView* view);
    void copyPath();
    void about();
    bool confirmClose(DocumentView& view);
    void onCurrentTabChanged(int index);
    void reportOpenError(const QString& path, const QString& reason);
    void showAllTools();
    void releaseView(DocumentView* view);
    void applyNavWidth();
    void applyPanels();
    void placeReadBar();
    void updateStatusVisibility();
    void applyInitialView(DocumentView* view, const OpenSummary& summary);
    void openAttachmentPdf(const QString& path, const QString& title);
    // Вкладка для открытого реестром документа: вид, полосы «восстановлен» / PDF/A, автосохранение.
    DocumentView* addDocumentView(const DocumentRegistry::Opened& opened, int page);
    void applyAutosaveSettings();
    void showInExplorer();

    std::unique_ptr<RenderPool> pool_;
    std::unique_ptr<TileCache> cache_;
    // «Мало памяти» (эталон 4.1, план 1.2): фон и простой 60 с — store MuPDF и кеш тайлов окна до 50 %.
    // Объявлен после cache_ — уничтожается раньше него.
    std::unique_ptr<MemoryGuard> memory_;
    std::unique_ptr<DocumentRegistry> registry_;
    std::unique_ptr<AutosaveRunner> autosave_;
    CommandRegistry* commands_ = nullptr;
    SaveFlow* saveFlow_ = nullptr;
    LinkFlow* linkFlow_ = nullptr;
    PasswordPrompt passwordPrompt_;
    QHash<QString, QString> passwords_;  // «запомнить на время сеанса»: ключ пути → пароль (только память)
    QPointer<RecoverDialog> recover_;

    GlobalBar* globalBar_ = nullptr;
    QSplitter* body_ = nullptr;
    ToolPanel* toolPanel_ = nullptr;
    NavPane* navPane_ = nullptr;
    StatusBar* status_ = nullptr;
    QStackedWidget* center_ = nullptr;
    QWidget* home_ = nullptr;
    QStackedWidget* documents_ = nullptr;
    CloseConfirm confirm_;
    QString error_;
    bool closing_ = false;
    bool panelsHidden_ = false;
    bool readMode_ = false;
    bool menuBarShown_ = false;
    ReadModeBar* readBar_ = nullptr;
    QPointer<FullScreenWindow> fullScreen_;
    QPointer<PreferencesDialog> preferences_;
    QPointer<CommandPalette> palette_;
    QPointer<SearchWindow> search_;
    QPointer<PropertiesDialog> properties_;
    QPointer<PrintDialog> print_;
    QHash<quint64, QString> temporary_;  // вкладка → её временный файл (подписанная версия), удаляется с ней

    struct Actions;
    std::unique_ptr<Actions> actions_;
};

}  // namespace ot::ui
