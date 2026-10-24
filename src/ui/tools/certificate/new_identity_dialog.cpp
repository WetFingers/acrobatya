#include "ui/tools/certificate/new_identity_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

#include <exception>

#include "services/identities.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

NewIdentityDialog::NewIdentityDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("newIdentityDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    intro_ = new QLabel(this);
    intro_->setWordWrap(true);
    column->addWidget(intro_);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);

    name_ = new QLineEdit();
    name_->setObjectName(QStringLiteral("identityName"));
    organization_ = new QLineEdit();
    organization_->setObjectName(QStringLiteral("identityOrganization"));
    unit_ = new QLineEdit();
    unit_->setObjectName(QStringLiteral("identityUnit"));
    email_ = new QLineEdit();
    email_->setObjectName(QStringLiteral("identityEmail"));
    country_ = new QLineEdit();
    country_->setObjectName(QStringLiteral("identityCountry"));
    country_->setMaxLength(2);
    country_->setValidator(
        new QRegularExpressionValidator(QRegularExpression(QStringLiteral("[A-Za-z]{0,2}")), country_));
    country_->setText(QStringLiteral("RU"));
    algorithm_ = new QComboBox();
    algorithm_->setObjectName(QStringLiteral("identityAlgorithm"));
    algorithm_->addItem(QString(), static_cast<int>(sign::KeyAlgorithm::Rsa3072));
    algorithm_->addItem(QString(), static_cast<int>(sign::KeyAlgorithm::EcdsaP256));
    password_ = new QLineEdit();
    password_->setObjectName(QStringLiteral("identityPassword"));
    password_->setEchoMode(QLineEdit::Password);
    confirm_ = new QLineEdit();
    confirm_->setObjectName(QStringLiteral("identityConfirm"));
    confirm_->setEchoMode(QLineEdit::Password);
    remember_ = new QCheckBox();
    remember_->setObjectName(QStringLiteral("identityRemember"));

    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), name_);
    rows_ << sheet_->addRow(QString(), organization_);
    rows_ << sheet_->addRow(QString(), unit_);
    rows_ << sheet_->addRow(QString(), email_);
    rows_ << sheet_->addRow(QString(), country_);
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), algorithm_);
    rows_ << sheet_->addRow(QString(), password_);
    rows_ << sheet_->addRow(QString(), confirm_);
    sheet_->addWidget(remember_);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("identityError"));
    error_->setWordWrap(true);
    error_->hide();
    column->addWidget(error_);
    auto* buttons = new QDialogButtonBox(this);
    create_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    create_->setObjectName(QStringLiteral("identityCreate"));
    create_->setDefault(true);
    create_->setProperty("primary", true);
    repolish(create_);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &NewIdentityDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth * 3 / 5);
    retranslate();
    name_->setFocus();
}

void NewIdentityDialog::fail(const QString& text) {
    error_->setText(text);
    SignTexts::setNote(error_, Tone::Danger);
    error_->show();
}

void NewIdentityDialog::submit() {
    const QString name = name_->text().trimmed();
    if (name.isEmpty()) {
        fail(tr("Укажите имя — оно будет в сертификате и в подписи."));
        name_->setFocus();
        return;
    }
    if (password_->text().isEmpty()) {
        fail(tr("Задайте пароль: он защищает файл удостоверения."));
        password_->setFocus();
        return;
    }
    if (password_->text() != confirm_->text()) {
        fail(tr("Пароль и подтверждение не совпадают."));
        confirm_->clear();
        confirm_->setFocus();
        return;
    }
    const QString country = country_->text().trimmed().toUpper();
    if (!country.isEmpty() && country.size() != 2) {
        fail(tr("Страна — две латинские буквы, например RU."));
        return;
    }
    sign::SelfSignedOptions o;
    o.commonName = name.toStdString();
    o.organization = organization_->text().trimmed().toStdString();
    o.organizationalUnit = unit_->text().trimmed().toStdString();
    o.email = email_->text().trimmed().toStdString();
    o.country = country.toStdString();
    o.algorithm = static_cast<sign::KeyAlgorithm>(algorithm_->currentData().toInt());
    const std::string password = password_->text().toStdString();
    const bool remember = remember_->isChecked();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        record_ = Identities::shared().with(
            [&](sign::IdentityStore& s) { return s.createSelfSigned(o, password, remember); });
    } catch (const sign::IdentityError& e) {
        QApplication::restoreOverrideCursor();
        fail(SignTexts::identityError(e.status()));
        return;
    } catch (const std::exception& e) {
        QApplication::restoreOverrideCursor();
        fail(tr("Не удалось создать удостоверение: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    QApplication::restoreOverrideCursor();
    accept();
}

void NewIdentityDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void NewIdentityDialog::retranslate() {
    setWindowTitle(tr("Создать самоподписанное удостоверение"));
    intro_->setText(
        tr("Самоподписанное удостоверение подходит для подписи внутри семьи и для проверки: другим людям "
           "нужно будет добавить его сертификат в доверенные. Срок действия — 5 лет."));
    const QStringList sections = {tr("Владелец"), tr("Ключ и пароль")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Имя:"),       tr("&Организация:"),  tr("&Подразделение:"),
                              tr("&Эл. почта:"), tr("&Страна:"),       tr("&Алгоритм ключа:"),
                              tr("Па&роль:"),    tr("П&одтверждение:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    name_->setPlaceholderText(tr("Например, Иван Иванов"));
    algorithm_->setItemText(0, tr("RSA 3072 бит — совместимее"));
    algorithm_->setItemText(1, tr("ECDSA P-256 — короче подпись"));
    remember_->setText(tr("&Запомнить пароль (защищён Windows для этого пользователя)"));
    create_->setText(tr("Со&здать"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
