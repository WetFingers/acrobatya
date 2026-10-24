// «Доверенные сертификаты» — вкладка Установок → Подписи (план 4.6, п. 3; эталон 3.8 «Доверие»; sign-30 —
// импорт, экспорт, «доверять для», удаление; sign-31 — списки доверия EUTL и AATL; sign-34 — доверие из файла
// и из подписи): якоря хранилища программы (TrustService) списком — сертификат, источник (свой, список ЕС,
// Adobe, Windows), для чего доверять, срок; фильтр по источнику; «Импорт…» (.cer/.crt/.pem/.p7b/.p7c/.fdf),
// «Экспорт…» (.cer/.p7c/.fdf), «Экспорт всех…» (.p7c), «Изменить доверие…» (TrustDialog), «Удалить»,
// «Импорт из Windows…» (корневые и промежуточные ЦС — только по согласию); корни Windows как доверенные — для
// подписей и (или) сертифицированных документов (по умолчанию нет); списки доверия — ЕС и Adobe, «спрашивать
// перед установкой», «Обновить сейчас» (TrustUpdater в фоне: загрузка, вопрос, установка). Якоря меняются
// сразу; флажки списков — по «ОК» Установок (load/apply).
#pragma once

#include <QWidget>

#include <functional>
#include <optional>

#include "core/sign/trust_store.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace ot {
class Settings;
}

namespace ot::ui {

class TrustDialog;

class TrustedWidget : public QWidget {
    Q_OBJECT
public:
    using FileChooser = std::function<QString(QWidget* parent, bool open)>;
    using TrustPrompt = std::function<std::optional<unsigned>(TrustDialog& dialog)>;
    using Confirm = std::function<bool(const QString& question)>;

    explicit TrustedWidget(QWidget* parent = nullptr);

    void load(const Settings& settings);
    void apply(Settings& settings);
    void reload();

    QTreeWidget* list() const { return list_; }
    QComboBox* filterBox() const { return filter_; }
    QPushButton* importButton() const { return import_; }
    QPushButton* exportButton() const { return export_; }
    QPushButton* exportAllButton() const { return exportAll_; }
    QPushButton* editButton() const { return edit_; }
    QPushButton* removeButton() const { return remove_; }
    QPushButton* windowsImportButton() const { return windowsImport_; }
    QCheckBox* windowsSignaturesBox() const { return winSignatures_; }
    QCheckBox* windowsCertifiedBox() const { return winCertified_; }
    QCheckBox* eutlBox() const { return eutl_; }
    QCheckBox* aatlBox() const { return aatl_; }
    QCheckBox* askBox() const { return ask_; }
    QPushButton* updateButton() const { return update_; }
    QLabel* listsLabel() const { return lists_; }
    QLabel* noteLabel() const { return note_; }

    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }
    void setTrustPrompt(TrustPrompt prompt) { trustPrompt_ = std::move(prompt); }
    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }

    // Выбранный якорь (SHA-256 HEX); выбрать по отпечатку.
    QString selectedThumbprint() const;
    bool select(const QString& thumbprint);
    // Действия (и для тестов).
    int importFile(const QString& path);
    bool exportSelected(const QString& path);
    bool exportAll(const QString& path);
    bool editSelected();
    bool removeSelected();
    int importWindows(bool root, bool ca);
    // «Обновить сейчас»: загрузка в фоне, итог — сигнал updateFinished (и строка под списками).
    void updateNow();
    bool isUpdating() const { return updating_; }

signals:
    void updateFinished(bool ok, const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void syncButtons();
    void fail(const QString& text);
    void done(const QString& text);
    QString chooseFile(bool open, const QString& suggested = {});
    bool confirm(const QString& question);
    void setWindowsRoots();
    void showListsState();

    FileChooser chooser_;
    TrustPrompt trustPrompt_;
    Confirm confirm_;
    bool updating_ = false;
    QComboBox* filter_ = nullptr;
    QLabel* filterLabel_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* import_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* exportAll_ = nullptr;
    QPushButton* edit_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* windowsImport_ = nullptr;
    QLabel* windowsCaption_ = nullptr;
    QCheckBox* winSignatures_ = nullptr;
    QCheckBox* winCertified_ = nullptr;
    QLabel* listsCaption_ = nullptr;
    QCheckBox* eutl_ = nullptr;
    QCheckBox* aatl_ = nullptr;
    QCheckBox* ask_ = nullptr;
    QPushButton* update_ = nullptr;
    QLabel* lists_ = nullptr;
    QLabel* note_ = nullptr;
};

}  // namespace ot::ui
