#include "ui/tools/certificate/certify_dialog.h"

namespace ot::ui {

namespace {

SignDialog::Context certifyContext(bool visible, QSizeF rect, const QString& documentTitle) {
    SignDialog::Context c;
    c.mode = SignDialog::Mode::Certify;
    c.visible = visible;
    c.rect = rect;
    c.documentTitle = documentTitle;
    return c;
}

}  // namespace

CertifyDialog::CertifyDialog(bool visible, QSizeF rect, const QString& documentTitle, QWidget* parent)
    : SignDialog(certifyContext(visible, rect, documentTitle), parent) {
    setObjectName(QStringLiteral("certifyDialog"));
}

}  // namespace ot::ui
