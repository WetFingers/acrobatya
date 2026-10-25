#include "ui/tools/forms/js_alert_dialog.h"

#include <QAbstractButton>
#include <QCheckBox>

namespace ot::ui {

JsAlertDialog::JsAlertDialog(const forms::AlertRequest& request, QWidget* parent)
    : QMessageBox(parent), request_(request) {
    setObjectName(QStringLiteral("jsAlertDialog"));
    const QString title = QString::fromStdString(request.title);
    setWindowTitle(title.isEmpty() || title == QLatin1String("PDF alert")
                       ? tr("Сообщение JavaScript")
                       : tr("Сообщение JavaScript — %1").arg(title));
    setText(QString::fromStdString(request.message));
    switch (request.icon) {
        case forms::AlertIcon::Error:
            setIcon(QMessageBox::Critical);
            break;
        case forms::AlertIcon::Warning:
            setIcon(QMessageBox::Warning);
            break;
        case forms::AlertIcon::Question:
            setIcon(QMessageBox::Question);
            break;
        case forms::AlertIcon::Status:
            setIcon(QMessageBox::Information);
            break;
    }
    switch (request.buttons) {
        case forms::AlertButtons::Ok:
            setStandardButtons(QMessageBox::Ok);
            setEscapeButton(QMessageBox::Ok);
            break;
        case forms::AlertButtons::OkCancel:
            setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
            setEscapeButton(QMessageBox::Cancel);
            break;
        case forms::AlertButtons::YesNo:
            setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            setEscapeButton(QMessageBox::No);
            break;
        case forms::AlertButtons::YesNoCancel:
            setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
            setEscapeButton(QMessageBox::Cancel);
            break;
    }
    if (QAbstractButton* ok = button(QMessageBox::Ok)) {
        ok->setText(tr("ОК"));
    }
    if (QAbstractButton* cancel = button(QMessageBox::Cancel)) {
        cancel->setText(tr("Отмена"));
    }
    if (QAbstractButton* yes = button(QMessageBox::Yes)) {
        yes->setText(tr("Да"));
    }
    if (QAbstractButton* no = button(QMessageBox::No)) {
        no->setText(tr("Нет"));
    }
    if (request.checkBox) {
        auto* box = new QCheckBox(request.checkBoxMessage.empty() ||
                                          request.checkBoxMessage == "Do not show this message again"
                                      ? tr("Больше не показывать это сообщение")
                                      : QString::fromStdString(request.checkBoxMessage));
        box->setChecked(request.checked);
        setCheckBox(box);
    }
}

forms::AlertReply JsAlertDialog::reply() const {
    forms::AlertReply r = forms::defaultReply(request_);
    switch (standardButton(clickedButton())) {
        case QMessageBox::Ok:
            r.button = forms::AlertButton::Ok;
            break;
        case QMessageBox::Cancel:
            r.button = forms::AlertButton::Cancel;
            break;
        case QMessageBox::Yes:
            r.button = forms::AlertButton::Yes;
            break;
        case QMessageBox::No:
            r.button = forms::AlertButton::No;
            break;
        default:
            break;
    }
    if (checkBox() != nullptr) {
        r.checked = checkBox()->isChecked();
    }
    return r;
}

forms::AlertReply JsAlertDialog::ask(QWidget* parent, const forms::AlertRequest& request) {
    JsAlertDialog dialog(request, parent);
    dialog.exec();
    return dialog.reply();
}

}  // namespace ot::ui
