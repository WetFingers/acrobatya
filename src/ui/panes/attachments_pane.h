// Панель «Вложения» (эталон 3.1 «Навигация», 7.3; план 2.4; view-17, security-43): файлы /EmbeddedFiles и
// аннотаций /FileAttachment — имя, размер, описание и страница в подсказке. «Сохранить…» — любое вложение.
// «Открыть»:
//   * PDF — копия во временной папке открывается вкладкой (сигнал openPdfRequested; защищённый просмотр
//   — 2.5b);
//   * расширение из чёрного списка эталона 7.3 (exe, bat, js, ps1, msi, lnk…) — нельзя: только «Сохранить…»;
//   * прочее — подтверждение «Открыть вложение «x.docx» внешней программой?», копия во временной папке и
//     ShellLauncher::openFile (в тестах — FakeShellLauncher); настройка trust/allowOpenAttachments = false
//     (Acrobat «Разрешить открывать вложения не-PDF внешними программами») запрещает и это.
// Подтверждение и выбор файла подставляют тесты (setConfirm, setSaveChooser).
#pragma once

#include <QPointer>

#include <functional>
#include <optional>
#include <vector>

#include "core/links/attachments.h"
#include "services/settings_keys.h"
#include "ui/panes/nav_page.h"

class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ot::ui {

class EmptyState;

class AttachmentsPane : public NavPage {
    Q_OBJECT
public:
    // Ключ settings.ini (Установки → Диспетчер доверия; settings_keys.h): вкл. по умолчанию.
    static constexpr const char* kAllowOpenKey = settings::kAllowOpenAttachments.name;

    using Confirm = std::function<bool(const QString& fileName)>;
    using SaveChooser = std::function<QString(const QString& suggestedPath)>;

    explicit AttachmentsPane(QWidget* parent = nullptr);

    QString paneId() const override { return QStringLiteral("attachments"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;

    QTreeWidget* tree() const { return tree_; }
    QPushButton* openButton() const { return open_; }
    QPushButton* saveButton() const { return save_; }
    bool isEmptyShown() const;
    const std::vector<attachments::Attachment>& files() const { return files_; }
    int current() const;  // выбранное вложение (номер в files) или −1
    void select(int index);

    // Можно ли открыть (не сохранить) вложение: PDF — да, чёрный список — нет, прочее — по настройке.
    static bool canOpen(const attachments::Attachment& a);
    // То же для документа панели: в защищённом просмотре (2.5b) — ни одно, только «Сохранить…».
    bool canOpenHere(const attachments::Attachment& a) const;
    static bool allowOpenNonPdf();

    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }
    void setSaveChooser(SaveChooser chooser) { chooser_ = std::move(chooser); }

    // Действия выбранного (то же, что кнопки и двойной щелчок; заблокированное — «Сохранить…»).
    void openCurrent();
    void saveCurrent();
    // Вложение комментария /FileAttachment (страница, номер объекта) вкладки view — открыть (5.2, двойной
    // щелчок по значку) или сохранить, с теми же чёрным списком и подтверждением.
    void openAnnotation(DocumentView* view, int page, int objnum, bool save = false);

    // Временная папка вложений процесса (удаляется при выходе).
    static QString tempFolder();

signals:
    void openPdfRequested(const QString& path, const QString& title);
    void listChanged();
    void opened(const QString& path);  // внешней программой (после подтверждения)
    void saved(const QString& path);

protected:
    void changeEvent(QEvent* event) override;

private:
    void reload();
    void fill(const std::vector<attachments::Attachment>& files);
    void updateButtons();
    void retranslate();
    QString extractToTemp(const attachments::Attachment& a);

    QStackedWidget* stack_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    EmptyState* empty_ = nullptr;
    QPushButton* open_ = nullptr;
    QPushButton* save_ = nullptr;
    QPointer<DocumentView> view_;
    std::vector<attachments::Attachment> files_;
    Confirm confirm_;
    SaveChooser chooser_;
    quint64 generation_ = 0;
    bool dirty_ = true;
    struct PendingOpen {
        int page = -1;
        int objnum = 0;
        bool save = false;
    };
    std::optional<PendingOpen> pendingOpen_;
};

}  // namespace ot::ui
