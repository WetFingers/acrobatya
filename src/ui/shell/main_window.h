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

#include <functional>
#include <memory>

#include "services/session_store.h"
#include "services/single_instance.h"

class QSplitter;
class QStackedWidget;

namespace ot {
class CommandRegistry;
class DocumentRegistry;
class DocumentSession;
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class DocumentTabs;
class DocumentView;
class GlobalBar;
class ToolPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    enum class CloseAnswer { Save, Discard, Cancel };
    using CloseConfirm = std::function<CloseAnswer(DocumentView&)>;

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Открыть файл во вкладке (уже открытый — его вкладка становится текущей); page — с нуля, -1 — первая.
    // false — не открылся: окно сообщает причину (неблокирующий диалог), errorString().
    bool openFile(const QString& path, int page = -1);
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
    // «Восстанавливать последний вид при открытии» (Установки → Документы; вкл. по умолчанию, ключ переедет в
    // settings_keys.h в 2.4b).
    static constexpr char kRestoreViewKey[] = "documents/restoreLastView";
    static bool restoreLastViewEnabled();

    // Второй экземпляр передал файлы: открыть и поднять окно.
    void handleRequest(const ot::SingleInstance::Request& request);

    // Ответ на «Сохранить изменения?» (тесты подставляют свой; по умолчанию — QMessageBox).
    void setCloseConfirm(CloseConfirm confirm) { confirm_ = std::move(confirm); }

    // Сообщение «появится в версии 0.N» — тостом над окном (заглушки будущих этапов).
    void notifyLater(const QString& what, int stage);

    // Однобуквенные ускорители H, V, Z, Shift+Z, X… (эталон 6.8; настройка, выкл. по умолчанию).
    void setSingleKeyAccelerators(bool on);
    bool singleKeyAccelerators() const;
    // F8: все панели (левая, плавающие; правая и строка состояния — 2.4) скрыты.
    void setPanelsHidden(bool hidden);
    bool panelsHidden() const { return panelsHidden_; }

signals:
    void documentOpened(ot::ui::DocumentView* view);
    void currentDocumentChanged(ot::ui::DocumentView* view);

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
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

    std::unique_ptr<RenderPool> pool_;
    std::unique_ptr<TileCache> cache_;
    std::unique_ptr<DocumentRegistry> registry_;
    CommandRegistry* commands_ = nullptr;

    GlobalBar* globalBar_ = nullptr;
    QSplitter* body_ = nullptr;
    ToolPanel* toolPanel_ = nullptr;
    QStackedWidget* center_ = nullptr;
    QWidget* home_ = nullptr;
    QStackedWidget* documents_ = nullptr;
    CloseConfirm confirm_;
    QString error_;
    bool closing_ = false;
    bool panelsHidden_ = false;

    struct Actions;
    std::unique_ptr<Actions> actions_;
};

}  // namespace ot::ui
