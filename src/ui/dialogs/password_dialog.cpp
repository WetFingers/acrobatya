#include "ui/dialogs/password_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

PasswordDialog::PasswordDialog(const QString& fileName, Validator validator, QWidget* parent)
    : QDialog(parent), fileName_(fileName), validator_(std::move(validator)) {
    setObjectName(QStringLiteral("passwordDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("lock"), IconTone::Accent, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("passwordText"));
    text_->setWordWrap(true);
    head->addWidget(text_, 1);
    column->addLayout(head);
    field_ = new QLineEdit(this);
    field_->setObjectName(QStringLiteral("passwordField"));
    field_->setEchoMode(QLineEdit::Password);
    column->addWidget(field_);
    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("passwordError"));
    error_->setWordWrap(true);
    error_->hide();
    column->addWidget(error_);
    remember_ = new QCheckBox(this);
    remember_->setObjectName(QStringLiteral("passwordRemember"));
    column->addWidget(remember_);
    auto* buttons = new QDialogButtonBox(this);
    ok_ = buttons->addButton(QDialogButtonBox::Ok);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    ok_->setDefault(true);
    ok_->setEnabled(false);
    connect(field_, &QLineEdit::textChanged, this,
            [this](const QString& t) { ok_->setEnabled(!t.isEmpty()); });
    // Пустой пароль владельца тоже бывает, но пустой пароль пользователя MuPDF пробует сам при открытии.
    connect(buttons, &QDialogButtonBox::accepted, this, &PasswordDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth / 2);
    retranslate();
    field_->setFocus();
}

bool PasswordDialog::remember() const { return remember_->isChecked(); }

QString PasswordDialog::password() const { return accepted_ ? acceptedPassword_ : field_->text(); }

void PasswordDialog::submit() {
    if (attempts_ >= kAttempts) {
        return;
    }
    const QString candidate = field_->text();
    ++attempts_;
    if (validator_ && validator_(candidate)) {
        accepted_ = true;
        acceptedPassword_ = candidate;
        accept();
        return;
    }
    if (attempts_ >= kAttempts) {
        reject();
        return;
    }
    error_->setText(tr("Неверный пароль. Осталось попыток: %1.").arg(attemptsLeft()));
    error_->show();
    field_->clear();
    field_->setFocus();
}

void PasswordDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void PasswordDialog::retranslate() {
    setWindowTitle(tr("Пароль"));
    text_->setText(tr("Документ «%1» защищён паролем. Введите пароль, чтобы открыть его.").arg(fileName_));
    field_->setPlaceholderText(tr("Пароль"));
    field_->setAccessibleName(tr("Пароль"));
    remember_->setText(tr("&Запомнить на время сеанса"));
    remember_->setToolTip(tr("Пароль хранится только в памяти программы до её закрытия"));
    ok_->setText(tr("ОК"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
