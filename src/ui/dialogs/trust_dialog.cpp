#include "ui/dialogs/trust_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "core/sign/ossl.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

TrustDialog::TrustDialog(const sign::CertSummary& cert, unsigned flags, bool existing, QWidget* parent)
    : QDialog(parent), cert_(cert), existing_(existing) {
    setObjectName(QStringLiteral("trustDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("shield-check"), IconTone::Accent, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    auto* text = new QVBoxLayout();
    intro_ = new QLabel(this);
    intro_->setWordWrap(true);
    who_ = new QLabel(this);
    who_->setObjectName(QStringLiteral("trustWho"));
    who_->setWordWrap(true);
    who_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    text->addWidget(intro_);
    text->addWidget(who_);
    head->addLayout(text, 1);
    column->addLayout(head);
    signatures_ = new QCheckBox(this);
    signatures_->setObjectName(QStringLiteral("trustSignatures"));
    certification_ = new QCheckBox(this);
    certification_->setObjectName(QStringLiteral("trustCertification"));
    dynamic_ = new QCheckBox(this);
    dynamic_->setObjectName(QStringLiteral("trustDynamic"));
    javaScript_ = new QCheckBox(this);
    javaScript_->setObjectName(QStringLiteral("trustJavaScript"));
    signatures_->setChecked((flags & sign::kTrustSignatures) != 0);
    certification_->setChecked((flags & sign::kTrustCertification) != 0);
    dynamic_->setChecked((flags & sign::kTrustDynamicContent) != 0);
    javaScript_->setChecked((flags & sign::kTrustJavaScript) != 0);
    for (QCheckBox* b : {signatures_, certification_, dynamic_, javaScript_}) {
        column->addWidget(b);
    }
    warning_ = new QLabel(this);
    warning_->setObjectName(QStringLiteral("trustWarning"));
    warning_->setWordWrap(true);
    column->addWidget(warning_);
    auto* buttons = new QDialogButtonBox(this);
    ok_ = buttons->addButton(QDialogButtonBox::Ok);
    ok_->setObjectName(QStringLiteral("trustOk"));
    ok_->setDefault(true);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth * 3 / 5);
    retranslate();
}

unsigned TrustDialog::flags() const {
    unsigned f = 0;
    f |= signatures_->isChecked() ? sign::kTrustSignatures : 0u;
    f |= certification_->isChecked() ? sign::kTrustCertification : 0u;
    f |= dynamic_->isChecked() ? sign::kTrustDynamicContent : 0u;
    f |= javaScript_->isChecked() ? sign::kTrustJavaScript : 0u;
    return f;
}

void TrustDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void TrustDialog::retranslate() {
    setWindowTitle(existing_ ? tr("Изменить доверие") : tr("Добавить в доверенные"));
    intro_->setText(existing_
                        ? tr("Для чего доверять этому сертификату:")
                        : tr("Сертификат станет доверенным: подписи, которые до него доводят цепочку, будут "
                             "считаться действительными."));
    QString who = tr("Кому выдан: %1\nКем выдан: %2")
                      .arg(QString::fromStdString(cert_.subject), QString::fromStdString(cert_.issuer));
    if (!cert_.sha256.empty()) {
        who += QLatin1Char('\n') +
               tr("Отпечаток SHA-256: %1").arg(QString::fromStdString(sign::ossl::toHex(cert_.sha256)));
    }
    who_->setText(who);
    signatures_->setText(tr("&Подписанные документы или данные"));
    certification_->setText(tr("&Сертифицированные документы"));
    dynamic_->setText(tr("&Динамическое содержимое"));
    javaScript_->setText(tr("Встроенный &JavaScript с повышенными правами"));
    warning_->setText(
        tr("Доверяйте только сертификатам, в происхождении которых уверены: проверьте отпечаток у "
           "владельца по телефону или лично."));
    SignTexts::setNote(warning_, Tone::Warn);
    ok_->setText(tr("ОК"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
