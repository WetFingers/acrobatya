#include "ui/dialogs/submit_dialog.h"

#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

QString SubmitDialog::formatTitle(forms::SubmitFormat format) {
    switch (format) {
        case forms::SubmitFormat::Fdf:
            return tr("данные формы FDF");
        case forms::SubmitFormat::Xfdf:
            return tr("данные формы XFDF");
        case forms::SubmitFormat::Html:
            return tr("поля формы (HTML)");
        case forms::SubmitFormat::Pdf:
            break;
    }
    return tr("документ PDF целиком");
}

SubmitDialog::SubmitDialog(const forms::SubmitPlan& plan, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("submitDialog"));
    const bool mail = plan.target == forms::SubmitTarget::Mail;
    setWindowTitle(tr("Отправка формы"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("send"), IconTone::Accent, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    auto* body = new QVBoxLayout();
    body->setSpacing(m.grid * 2);
    auto* text = new QLabel(mail ? tr("Документ подготовит письмо с данными формы:")
                                 : tr("Документ отправит данные формы на сервер:"),
                            this);
    text->setWordWrap(true);
    body->addWidget(text);
    target_ = new QLabel(this);
    target_->setObjectName(QStringLiteral("submitTarget"));
    target_->setTextFormat(Qt::PlainText);
    target_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont bold = uiFont(m);
    bold.setWeight(QFont::DemiBold);
    target_->setFont(bold);
    QString where;
    if (mail && plan.mail) {
        QStringList to;
        for (const std::string& a : plan.mail->to) {
            to << QString::fromStdString(a);
        }
        where = to.isEmpty() ? tr("получатель не указан") : to.join(QStringLiteral(", "));
    } else {
        where = QString::fromStdString(plan.url);
    }
    target_->setText(QFontMetrics(bold).elidedText(where, Qt::ElideMiddle, m.dialogWidth / 2));
    target_->setToolTip(where);
    body->addWidget(target_);
    details_ = new QLabel(this);
    details_->setObjectName(QStringLiteral("submitDetails"));
    details_->setWordWrap(true);
    const int fields = static_cast<int>(plan.fields.size());
    if (mail) {
        details_->setText(tr("Вложение: %1 — %2, полей: %3. Письмо откроется в почтовой программе — отправите его "
                             "вы.")
                              .arg(QString::fromStdString(plan.attachmentName), formatTitle(plan.format))
                              .arg(fields));
    } else {
        QString d = tr("Что уйдёт: %1, полей: %2.").arg(formatTitle(plan.format)).arg(fields);
        if (plan.target == forms::SubmitTarget::Http) {
            d += QLatin1Char(' ') + tr("Соединение не защищено (http): данные могут увидеть посторонние.");
        }
        d += QLatin1Char(' ') + tr("Отправляйте, только если доверяете документу и сайту.");
        details_->setText(d);
    }
    body->addWidget(details_);
    head->addLayout(body, 1);
    column->addLayout(head);

    auto* buttons = new QDialogButtonBox(this);
    send_ = buttons->addButton(mail ? tr("&Создать письмо") : tr("&Отправить"), QDialogButtonBox::AcceptRole);
    send_->setObjectName(QStringLiteral("submitSend"));
    send_->setDefault(true);
    buttons->addButton(tr("Отмена"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth / 2 + m.grid * 24);
}

bool SubmitDialog::ask(QWidget* parent, const forms::SubmitPlan& plan) {
    SubmitDialog dialog(plan, parent);
    return dialog.exec() == QDialog::Accepted;
}

}  // namespace ot::ui
