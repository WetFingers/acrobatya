// Подтверждение отправки формы (план 6.1, п. 4; эталон 7.4: «SubmitForm на http(s) — подтверждение», без
// подтверждения ничего не уходит; forms-30): куда (адрес сервера или получатели письма), формат (FDF, XFDF,
// HTML, PDF) и сколько полей; http без TLS — предупреждение. Кнопки — «Отправить» / «Создать письмо» и «Отмена».
#pragma once

#include <QDialog>

#include "core/forms/submit.h"

class QLabel;
class QPushButton;

namespace ot::ui {

class SubmitDialog : public QDialog {
    Q_OBJECT
public:
    explicit SubmitDialog(const forms::SubmitPlan& plan, QWidget* parent = nullptr);

    QPushButton* sendButton() const noexcept { return send_; }
    QLabel* targetLabel() const noexcept { return target_; }
    QLabel* detailsLabel() const noexcept { return details_; }
    // Модально: true — отправить.
    static bool ask(QWidget* parent, const forms::SubmitPlan& plan);
    static QString formatTitle(forms::SubmitFormat format);

private:
    QLabel* target_ = nullptr;
    QLabel* details_ = nullptr;
    QPushButton* send_ = nullptr;
};

}  // namespace ot::ui
