// Диалог пароля (план 2.5; эталон 3.1 «Открытие», 7.1; security-09): «Документ защищён паролем» — поле,
// «Запомнить на время сеанса» (только в памяти окна, не на диске), три попытки: неверный пароль — «Неверный
// пароль. Осталось попыток: N», поле очищается; после третьей — отказ (reject, exhausted()). Проверку пароля
// делает вызывающий (validator: попробовать открыть файл).
#pragma once

#include <QDialog>

#include <functional>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ot::ui {

class PasswordDialog : public QDialog {
    Q_OBJECT
public:
    static constexpr int kAttempts = 3;
    using Validator = std::function<bool(const QString& password)>;

    PasswordDialog(const QString& fileName, Validator validator, QWidget* parent = nullptr);

    QLineEdit* field() const { return field_; }
    QCheckBox* rememberBox() const { return remember_; }
    QLabel* errorLabel() const { return error_; }
    QPushButton* okButton() const { return ok_; }

    int attemptsUsed() const { return attempts_; }
    int attemptsLeft() const { return kAttempts - attempts_; }
    bool exhausted() const { return attempts_ >= kAttempts && !accepted_; }
    bool remember() const;
    QString password() const;

    // «ОК»: проверить пароль; верный — accept, неверный — попытка (третья — reject).
    void submit();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    QString fileName_;
    Validator validator_;
    QLabel* text_ = nullptr;
    QLineEdit* field_ = nullptr;
    QCheckBox* remember_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QString acceptedPassword_;
    int attempts_ = 0;
    bool accepted_ = false;
};

}  // namespace ot::ui
