// «Создать самоподписанное удостоверение» (план 4.6, п. 1; эталон 3.8 «Диалог подписания»; sign-05 — UI):
// имя (CN, обязательно), организация, подразделение, эл. почта, страна (две латинские буквы), алгоритм
// ключа — RSA 3072 или ECDSA P-256, срок — 5 лет, пароль файла .pfx с подтверждением, «Запомнить пароль»
// (DPAPI). «Создать» — IdentityStore::createSelfSigned (services/identities.h): ключ и сертификат в
// <данные>\ids, запись в state.sqlite; ошибка — строкой в диалоге.
#pragma once

#include <QDialog>

#include <optional>

#include "core/sign/identity.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ot::ui {

class PropertySheet;

class NewIdentityDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewIdentityDialog(QWidget* parent = nullptr);

    QLineEdit* nameField() const { return name_; }
    QLineEdit* organizationField() const { return organization_; }
    QLineEdit* unitField() const { return unit_; }
    QLineEdit* emailField() const { return email_; }
    QLineEdit* countryField() const { return country_; }
    QComboBox* algorithmBox() const { return algorithm_; }
    QLineEdit* passwordField() const { return password_; }
    QLineEdit* confirmField() const { return confirm_; }
    QCheckBox* rememberBox() const { return remember_; }
    QLabel* errorLabel() const { return error_; }
    QPushButton* createButton() const { return create_; }

    // Созданное удостоверение (после accept).
    const std::optional<sign::IdentityRecord>& record() const { return record_; }

    // «Создать»: проверить поля и создать; ошибка — в errorLabel, диалог остаётся.
    void submit();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void fail(const QString& text);

    PropertySheet* sheet_ = nullptr;
    QLabel* intro_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* organization_ = nullptr;
    QLineEdit* unit_ = nullptr;
    QLineEdit* email_ = nullptr;
    QLineEdit* country_ = nullptr;
    QComboBox* algorithm_ = nullptr;
    QLineEdit* password_ = nullptr;
    QLineEdit* confirm_ = nullptr;
    QCheckBox* remember_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* create_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QList<QLabel*> rows_;
    QList<QLabel*> sections_;
    std::optional<sign::IdentityRecord> record_;
};

}  // namespace ot::ui
