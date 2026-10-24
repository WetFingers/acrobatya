#include "ui/dialogs/signature_properties_dialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace signature_properties {

QLabel* textLabel(const char* name) {
    auto* l = new QLabel();
    l->setObjectName(QLatin1String(name));
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    return l;
}

QString cn(const std::optional<sign::CertSummary>& c) {
    if (!c) {
        return {};
    }
    return QString::fromStdString(c->commonName.empty() ? c->subject : c->commonName);
}

}  // namespace signature_properties

SignaturePropertiesDialog::SignaturePropertiesDialog(sign::SignatureInfo signature, int versions,
                                                     QWidget* parent)
    : QDialog(parent), sig_(std::move(signature)), versions_(versions) {
    setObjectName(QStringLiteral("signaturePropertiesDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    icon_ = new QLabel(this);
    icon_->setAlignment(Qt::AlignTop);
    head->addWidget(icon_);
    auto* headText = new QVBoxLayout();
    verdict_ = signature_properties::textLabel("signatureVerdict");
    who_ = signature_properties::textLabel("signatureWho");
    headText->addWidget(verdict_);
    headText->addWidget(who_);
    head->addLayout(headText, 1);
    column->addLayout(head);

    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("signatureTabs"));
    // Сводка: причины проверки и кнопки.
    auto* summary = new QWidget();
    auto* sl = new QVBoxLayout(summary);
    findings_ = new QTreeWidget(summary);
    findings_->setObjectName(QStringLiteral("signatureFindings"));
    findings_->setHeaderHidden(true);
    findings_->setRootIsDecorated(false);
    findings_->setColumnCount(1);
    sl->addWidget(findings_, 1);
    auto* buttonsRow = new QHBoxLayout();
    certificate_ = new QPushButton(Icons::icon(QStringLiteral("id-card")), QString(), summary);
    certificate_->setObjectName(QStringLiteral("signatureCertificate"));
    verify_ = new QPushButton(Icons::icon(QStringLiteral("shield-check")), QString(), summary);
    verify_->setObjectName(QStringLiteral("signatureVerify"));
    buttonsRow->addWidget(certificate_);
    buttonsRow->addWidget(verify_);
    buttonsRow->addStretch(1);
    sl->addLayout(buttonsRow);
    tabs_->addTab(summary, QString());
    // Документ: ревизия, права, изменения.
    auto* doc = new QWidget();
    auto* dl = new QVBoxLayout(doc);
    document_ = signature_properties::textLabel("signatureDocument");
    changes_ = new QTreeWidget(doc);
    changes_->setObjectName(QStringLiteral("signatureChanges"));
    changes_->setHeaderHidden(true);
    changes_->setRootIsDecorated(false);
    signedVersion_ = new QPushButton(Icons::icon(QStringLiteral("history")), QString(), doc);
    signedVersion_->setObjectName(QStringLiteral("signatureSignedVersion"));
    dl->addWidget(document_);
    dl->addWidget(changes_, 1);
    auto* docRow = new QHBoxLayout();
    docRow->addWidget(signedVersion_);
    docRow->addStretch(1);
    dl->addLayout(docRow);
    tabs_->addTab(doc, QString());
    // Подписант, дата и время.
    signer_ = signature_properties::textLabel("signatureSigner");
    time_ = signature_properties::textLabel("signatureTime");
    for (QLabel* l : {signer_, time_}) {
        auto* page = new QWidget();
        auto* pl = new QVBoxLayout(page);
        pl->addWidget(l);
        pl->addStretch(1);
        tabs_->addTab(page, QString());
    }
    column->addWidget(tabs_, 1);
    auto* buttons = new QDialogButtonBox(this);
    close_ = buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    column->addWidget(buttons);
    connect(certificate_, &QPushButton::clicked, this, &SignaturePropertiesDialog::certificateRequested);
    connect(verify_, &QPushButton::clicked, this, &SignaturePropertiesDialog::verifyRequested);
    connect(signedVersion_, &QPushButton::clicked, this, &SignaturePropertiesDialog::signedVersionRequested);
    resize(m.dialogWidth * 4 / 5, m.dialogHeight);
    retranslate();
}

void SignaturePropertiesDialog::showTab(Tab tab) { tabs_->setCurrentIndex(static_cast<int>(tab)); }

void SignaturePropertiesDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void SignaturePropertiesDialog::retranslate() {
    const Metrics& m = Theme::sizes();
    const bool cert = sig_.kind == sign::SignatureKind::Certification;
    setWindowTitle(tr("Свойства подписи"));
    icon_->setPixmap(Icons::get(SignTexts::verdictIcon(sig_.verdict, cert),
                                toneIcon(SignTexts::verdictTone(sig_.verdict)), m.iconEmpty));
    const QString signer = signature_properties::cn(sig_.signer);
    verdict_->setText(tr("%1 — %2.").arg(SignTexts::kind(sig_.kind), SignTexts::verdict(sig_.verdict)));
    SignTexts::setNote(verdict_, SignTexts::verdictTone(sig_.verdict));
    QString who =
        sig_.kind == sign::SignatureKind::DocTimestamp
            ? tr("Служба времени: %1").arg(signer)
            : tr("Подписано: %1").arg(signer.isEmpty() ? QString::fromStdString(sig_.name) : signer);
    if (!sig_.claimedTime.empty() || sig_.timestamp) {
        const std::string when = sig_.timestamp ? sig_.timestamp->genTime : sig_.claimedTime;
        who += QLatin1Char('\n') + tr("Время: %1").arg(SignTexts::dateTime(when));
    }
    if (!sig_.reason.empty()) {
        who += QLatin1Char('\n') + tr("Причина: %1").arg(QString::fromStdString(sig_.reason));
    }
    if (!sig_.location.empty()) {
        who += QLatin1Char('\n') + tr("Место: %1").arg(QString::fromStdString(sig_.location));
    }
    who_->setText(who);

    const QStringList tabs = {tr("Сводка"), tr("Документ"), tr("Подписант"), tr("Дата и время")};
    for (int i = 0; i < tabs_->count(); ++i) {
        tabs_->setTabText(i, tabs.value(i));
    }
    // Сводка: что проверено.
    findings_->clear();
    const int size = m.iconRow;
    const auto addLine = [&](QTreeWidget* tree, const QString& text, Tone tone, const QString& icon,
                             const QString& tip) {
        auto* item = new QTreeWidgetItem(tree);
        item->setText(0, text);
        item->setIcon(0, QIcon(Icons::get(icon, toneIcon(tone), size)));
        item->setToolTip(0, tip.isEmpty() ? text : tip);
    };
    if (sig_.intact) {
        addLine(findings_, tr("Документ не изменялся с момента подписи (или изменения разрешены)."), Tone::Ok,
                QStringLiteral("circle-check"), {});
    }
    if (sig_.trusted) {
        addLine(findings_, tr("Удостоверение подписанта доверено."), Tone::Ok, QStringLiteral("circle-check"),
                {});
    }
    if (sig_.timestamp && sig_.timestampTrusted) {
        addLine(findings_, tr("Подпись содержит встроенную метку времени."), Tone::Ok,
                QStringLiteral("circle-check"), {});
    }
    if (sig_.ltv) {
        addLine(findings_, tr("Подпись поддерживает долгосрочную проверку (LTV включено)."), Tone::Ok,
                QStringLiteral("circle-check"), {});
    }
    for (const sign::Finding& f : sig_.findings) {
        const std::optional<sign::Verdict> severity = sign::issueSeverity(f.issue);
        const Tone tone = severity ? SignTexts::verdictTone(*severity) : Tone::Info;
        const QString icon = tone == Tone::Danger ? QStringLiteral("circle-alert")
                             : tone == Tone::Warn ? QStringLiteral("triangle-alert")
                                                  : QStringLiteral("info");
        addLine(findings_, SignTexts::issue(f.issue), tone, icon, QString::fromStdString(f.detail));
    }
    certificate_->setText(tr("Показать &сертификат подписанта…"));
    certificate_->setEnabled(sig_.signer.has_value() || !sig_.chain.empty());
    verify_->setText(tr("&Проверить подпись"));

    // Документ.
    QStringList doc;
    doc << tr("Подписанная ревизия: %1 из %2").arg(sig_.revision).arg(std::max(versions_, sig_.revision));
    doc << (sig_.coversWholeFile ? tr("Подпись покрывает весь документ.")
                                 : tr("После подписи в документ вносились изменения."));
    if (sig_.docMdp != sign::MdpPermission::None) {
        doc << tr("Разрешённые изменения: %1").arg(SignTexts::permission(sig_.docMdp));
    }
    if (!sig_.locks.empty()) {
        doc << tr("Подпись блокирует поля формы.");
    }
    doc << tr("Формат: %1 %2").arg(QString::fromStdString(sig_.subFilter), SignTexts::padesLevel(sig_.level));
    document_->setText(doc.join(QLatin1Char('\n')));
    changes_->clear();
    if (sig_.changes.changes.empty()) {
        addLine(changes_, tr("Изменений после подписи нет."), Tone::Ok, QStringLiteral("circle-check"), {});
    }
    for (const sign::Change& c : sig_.changes.changes) {
        QString text = SignTexts::changeKind(c.kind);
        if (c.page >= 0) {
            text += tr(" (стр. %1)").arg(c.page + 1);
        }
        if (!c.field.empty()) {
            text += QStringLiteral(" — ") + QString::fromStdString(c.field);
        }
        text += c.allowed ? tr(" — разрешено") : tr(" — не разрешено");
        addLine(changes_, text, c.allowed ? Tone::Ok : Tone::Danger,
                c.allowed ? QStringLiteral("circle-check") : QStringLiteral("circle-alert"),
                QString::fromStdString(c.detail));
    }
    signedVersion_->setText(tr("Просмотреть подписанную &версию"));

    // Подписант.
    QStringList s;
    if (sig_.signer) {
        s << tr("Кому выдан: %1").arg(QString::fromStdString(sig_.signer->subject));
        s << tr("Кем выдан: %1").arg(QString::fromStdString(sig_.signer->issuer));
        s << tr("Действует с %1 по %2")
                 .arg(SignTexts::dateTime(sig_.signer->notBefore),
                      SignTexts::dateTime(sig_.signer->notAfter));
    }
    s << (sig_.trusted ? tr("Цепочка до доверенного сертификата построена (%n серт.).", nullptr,
                            static_cast<int>(sig_.chain.size()))
                       : tr("Цепочка до доверенного сертификата не построена: подписант не доверен."));
    if (sig_.revocationChecked) {
        s << tr("Отзыв: %1").arg(SignTexts::revocation(sig_.revocation));
    } else {
        s << tr("Отзыв не проверялся.");
    }
    s << (sig_.ltv ? tr("LTV включено: сведения для проверки встроены в документ.") : tr("LTV не включено."));
    signer_->setText(s.join(QLatin1Char('\n')));

    // Дата и время.
    QStringList t;
    if (!sig_.claimedTime.empty()) {
        t << tr("Время подписи на компьютере подписавшего: %1").arg(SignTexts::dateTime(sig_.claimedTime));
    }
    if (sig_.timestamp) {
        const sign::TimestampInfo& ts = *sig_.timestamp;
        t << tr("Метка времени: %1").arg(SignTexts::dateTime(ts.genTime));
        const QString tsa = ts.signer.commonName.empty() ? QString::fromStdString(ts.tsaName)
                                                         : QString::fromStdString(ts.signer.commonName);
        t << tr("Служба времени: %1 — %2")
                 .arg(tsa, sig_.timestampTrusted ? tr("доверена") : tr("не доверена"));
        if (!ts.policy.empty()) {
            t << tr("Политика службы: %1").arg(QString::fromStdString(ts.policy));
        }
        if (!ts.serial.empty()) {
            t << tr("Серийный номер метки: %1").arg(QString::fromStdString(ts.serial));
        }
    } else {
        t << tr("Метки времени нет.");
    }
    t << tr("Подпись проверена на %1. %2")
             .arg(SignTexts::dateTime(sig_.validationTime), SignTexts::timeSource(sig_.timeSource));
    time_->setText(t.join(QLatin1Char('\n')));
    close_->setText(tr("Закрыть"));
}

}  // namespace ot::ui
