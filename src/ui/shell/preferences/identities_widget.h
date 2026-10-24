// «Удостоверения» — вкладка Установок → Подписи (план 4.6, п. 3; эталон 3.8 «Доверие» — «Удостоверения»;
// sign-05 — создать, sign-10 — по умолчанию, тайм-аут и смена пароля, удаление, sign-11 — экспорт
// сертификата): список своих удостоверений (имя, кем выдан, срок, вид — файл .pfx или хранилище Windows; по
// умолчанию — звёздочкой), «Добавить файл…», «Выбрать в Windows…» (только на экране), «Создать…»,
// «По умолчанию», «Экспорт сертификата…» (.cer, .p7c, .fdf — чтобы другие добавили его в доверенные),
// «Сменить пароль…», «Спрашивать пароль» (всегда / через N минут / до выхода), «Забыть пароль», «Удалить».
// Как в Acrobat, правки применяются сразу (services/identities.h), а не по «ОК» Установок.
#pragma once

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

#include "core/sign/identity.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace ot::ui {

class IdentitiesWidget : public QWidget {
    Q_OBJECT
public:
    // Выбор файла: open — открыть (.pfx), иначе сохранить (экспорт); пусто — отмена.
    using FileChooser = std::function<QString(QWidget* parent, bool open)>;
    // Пароль (prompt — подпись поля); nullopt — отмена.
    using PasswordPrompt = std::function<std::optional<QString>(QWidget* parent, const QString& prompt)>;
    using Confirm = std::function<bool(const QString& question)>;

    explicit IdentitiesWidget(QWidget* parent = nullptr);

    void reload(const QString& select = {});
    QTreeWidget* list() const { return list_; }
    QString selectedId() const;
    bool select(const QString& id);
    std::optional<sign::IdentityRecord> selected() const;

    QPushButton* addFileButton() const { return addFile_; }
    QPushButton* windowsButton() const { return windows_; }
    QPushButton* createButton() const { return create_; }
    QPushButton* defaultButton() const { return default_; }
    QPushButton* exportButton() const { return export_; }
    QPushButton* passwordButton() const { return password_; }
    QComboBox* timeoutBox() const { return timeout_; }
    QPushButton* forgetButton() const { return forget_; }
    QPushButton* removeButton() const { return remove_; }
    QLabel* noteLabel() const { return note_; }

    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }
    void setPasswordPrompt(PasswordPrompt prompt) { prompt_ = std::move(prompt); }
    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }

    // Действия (и для тестов): false — не вышло (причина — строкой под списком).
    bool addPfx(const QString& path, const QString& password);
    bool setDefaultSelected();
    bool exportSelected(const QString& path);
    bool changePassword(const QString& oldPassword, const QString& newPassword);
    bool setTimeoutSelected(int seconds);
    bool forgetPasswordSelected();
    bool removeSelected(bool deleteFile);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void syncButtons();
    void fail(const QString& text);
    void done(const QString& text);
    QString chooseFile(bool open);
    std::optional<QString> askPassword(const QString& prompt);

    std::vector<sign::IdentityRecord> records_;
    FileChooser chooser_;
    PasswordPrompt prompt_;
    Confirm confirm_;
    QTreeWidget* list_ = nullptr;
    QPushButton* addFile_ = nullptr;
    QPushButton* windows_ = nullptr;
    QPushButton* create_ = nullptr;
    QPushButton* default_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* password_ = nullptr;
    QLabel* timeoutLabel_ = nullptr;
    QComboBox* timeout_ = nullptr;
    QPushButton* forget_ = nullptr;
    QPushButton* remove_ = nullptr;
    QLabel* note_ = nullptr;
};

}  // namespace ot::ui
