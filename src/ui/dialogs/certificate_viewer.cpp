#include "ui/dialogs/certificate_viewer.h"

#include <QDateTime>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <exception>

#include "core/sign/ossl.h"
#include "core/sign/trust_store.h"
#include "services/trust_service.h"
#include "ui/dialogs/trust_dialog.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace certificate_viewer {

constexpr int kChainIndexRole = Qt::UserRole + 1;

QString display(const sign::CertSummary& c) {
    return QString::fromStdString(c.commonName.empty() ? c.subject : c.commonName);
}

QString join(const std::vector<std::string>& items, QString (*text)(const std::string&)) {
    QStringList out;
    for (const std::string& i : items) {
        out << text(i);
    }
    return out.join(QStringLiteral(", "));
}

QWidget* page(QWidget* content) {
    auto* w = new QWidget();
    auto* l = new QVBoxLayout(w);
    const Metrics& m = Theme::sizes();
    l->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    l->addWidget(content);
    l->addStretch(1);
    return w;
}

}  // namespace certificate_viewer

CertificateViewer::CertificateViewer(std::vector<sign::CertSummary> chain,
                                     std::vector<sign::RevocationResult> revocation, QWidget* parent)
    : QDialog(parent), certs_(std::move(chain)), revocations_(std::move(revocation)) {
    setObjectName(QStringLiteral("certificateViewer"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);
    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    chainCaption_ = new QLabel(left);
    chainCaption_->setProperty("caption", true);
    ll->addWidget(chainCaption_);
    chain_ = new QTreeWidget(left);
    chain_->setObjectName(QStringLiteral("certificateChain"));
    chain_->setHeaderHidden(true);
    chain_->setColumnCount(1);
    chain_->setRootIsDecorated(true);
    ll->addWidget(chain_, 1);
    tabs_ = new QTabWidget(split);
    tabs_->setObjectName(QStringLiteral("certificateTabs"));

    summary_ = new QLabel();
    summary_->setObjectName(QStringLiteral("certificateSummary"));
    summary_->setWordWrap(true);
    summary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summary_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    status_ = new QLabel();
    status_->setObjectName(QStringLiteral("certificateStatus"));
    status_->setWordWrap(true);
    auto* summaryBox = new QWidget();
    auto* sl = new QVBoxLayout(summaryBox);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->setSpacing(m.grid * 3);
    sl->addWidget(status_);
    sl->addWidget(summary_);
    tabs_->addTab(certificate_viewer::page(summaryBox), QString());

    auto* detailsBox = new QWidget();
    auto* dl = new QVBoxLayout(detailsBox);
    dl->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    details_ = new QTreeWidget(detailsBox);
    details_->setObjectName(QStringLiteral("certificateDetails"));
    details_->setColumnCount(2);
    details_->setRootIsDecorated(false);
    details_->header()->setStretchLastSection(true);
    detailValue_ = new QPlainTextEdit(detailsBox);
    detailValue_->setObjectName(QStringLiteral("certificateDetailValue"));
    detailValue_->setReadOnly(true);
    detailValue_->setMaximumHeight(m.cardRow * 2);
    dl->addWidget(details_, 1);
    dl->addWidget(detailValue_);
    tabs_->addTab(detailsBox, QString());
    connect(details_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        detailValue_->setPlainText(item != nullptr ? item->text(1) : QString());
    });

    revocation_ = new QLabel();
    revocation_->setObjectName(QStringLiteral("certificateRevocation"));
    revocation_->setWordWrap(true);
    revocation_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tabs_->addTab(certificate_viewer::page(revocation_), QString());

    auto* trustBox = new QWidget();
    auto* tl = new QVBoxLayout(trustBox);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->setSpacing(m.grid * 3);
    trust_ = new QLabel();
    trust_->setObjectName(QStringLiteral("certificateTrust"));
    trust_->setWordWrap(true);
    trustButton_ = new QPushButton(Icons::icon(QStringLiteral("shield-check")), QString());
    trustButton_->setObjectName(QStringLiteral("certificateTrustButton"));
    connect(trustButton_, &QPushButton::clicked, this, &CertificateViewer::editTrust);
    tl->addWidget(trust_);
    auto* row = new QHBoxLayout();
    row->addWidget(trustButton_);
    row->addStretch(1);
    tl->addLayout(row);
    tabs_->addTab(certificate_viewer::page(trustBox), QString());

    policies_ = new QLabel();
    policies_->setObjectName(QStringLiteral("certificatePolicies"));
    policies_->setWordWrap(true);
    policies_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    tabs_->addTab(certificate_viewer::page(policies_), QString());

    split->addWidget(left);
    split->addWidget(tabs_);
    split->setStretchFactor(1, 1);
    split->setSizes({m.rightPanel, m.dialogWidth - m.rightPanel});
    column->addWidget(split, 1);
    auto* buttons = new QDialogButtonBox(this);
    close_ = buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    column->addWidget(buttons);

    // Цепочка деревом: корень наверху, подписант — самый вложенный.
    QTreeWidgetItem* parentItem = nullptr;
    for (int i = static_cast<int>(certs_.size()) - 1; i >= 0; --i) {
        auto* item = parentItem != nullptr ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(chain_);
        item->setText(0, certificate_viewer::display(certs_[static_cast<std::size_t>(i)]));
        item->setData(0, certificate_viewer::kChainIndexRole, i);
        item->setIcon(0, Icons::icon(certs_[static_cast<std::size_t>(i)].ca ? QStringLiteral("award")
                                                                            : QStringLiteral("id-card")));
        parentItem = item;
    }
    chain_->expandAll();
    connect(chain_, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item != nullptr) {
            selected_ = item->data(0, certificate_viewer::kChainIndexRole).toInt();
            fill();
        }
    });
    connect(&TrustService::shared(), &TrustService::changed, this, &CertificateViewer::fill,
            Qt::QueuedConnection);
    resize(m.dialogWidth, m.dialogHeight);
    retranslate();
    select(0);
}

void CertificateViewer::select(int index) {
    if (index < 0 || index >= chainLength()) {
        return;
    }
    selected_ = index;
    for (QTreeWidgetItem* item = chain_->topLevelItem(0); item != nullptr;
         item = item->childCount() > 0 ? item->child(0) : nullptr) {
        if (item->data(0, certificate_viewer::kChainIndexRole).toInt() == index) {
            const QSignalBlocker block(chain_);
            chain_->setCurrentItem(item);
        }
    }
    fill();
}

void CertificateViewer::showTab(Tab tab) { tabs_->setCurrentIndex(static_cast<int>(tab)); }

void CertificateViewer::fill() {
    details_->clear();
    if (certs_.empty()) {
        summary_->setText(tr("Сертификатов нет."));
        return;
    }
    const sign::CertSummary& c = certs_[static_cast<std::size_t>(selected_)];
    sign::CertDetails d;
    try {
        d = sign::certDetails(c.der);
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): детали не читаются — только сводка
    }
    // Сводка.
    const QDateTime from = QDateTime::fromString(QString::fromStdString(c.notBefore), Qt::ISODate);
    const QDateTime to = QDateTime::fromString(QString::fromStdString(c.notAfter), Qt::ISODate);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (to.isValid() && to < now) {
        status_->setText(tr("Срок действия сертификата истёк %1.").arg(SignTexts::dateTime(c.notAfter)));
        SignTexts::setNote(status_, Tone::Warn);
    } else if (from.isValid() && from > now) {
        status_->setText(tr("Сертификат ещё не действует."));
        SignTexts::setNote(status_, Tone::Warn);
    } else {
        status_->setText(c.ca ? tr("Сертификат центра сертификации действителен.")
                              : tr("Сертификат действителен."));
        SignTexts::setNote(status_, Tone::Ok);
    }
    QStringList lines;
    lines << tr("<b>%1</b>").arg(certificate_viewer::display(c).toHtmlEscaped());
    lines << tr("Кому выдан: %1").arg(QString::fromStdString(c.subject).toHtmlEscaped());
    lines << tr("Кем выдан: %1").arg(QString::fromStdString(c.issuer).toHtmlEscaped());
    lines
        << tr("Действует с %1 по %2").arg(SignTexts::dateTime(c.notBefore), SignTexts::dateTime(c.notAfter));
    if (!d.keyUsage.empty()) {
        lines << tr("Назначение ключа: %1").arg(certificate_viewer::join(d.keyUsage, &SignTexts::keyUsage));
    }
    if (!d.extendedKeyUsage.empty()) {
        lines << tr("Расширенное назначение: %1")
                     .arg(certificate_viewer::join(d.extendedKeyUsage, &SignTexts::extendedKeyUsage));
    }
    lines << tr("Серийный номер: %1").arg(QString::fromStdString(c.serial));
    if (!d.publicKey.empty()) {
        lines << tr("Открытый ключ: %1").arg(QString::fromStdString(d.publicKey));
    }
    summary_->setText(lines.join(QStringLiteral("<br>")));

    // Детали.
    const auto add = [this](const QString& name, const QString& value) {
        auto* item = new QTreeWidgetItem(details_);
        item->setText(0, name);
        item->setText(1, value);
        item->setToolTip(1, value);
    };
    add(tr("Версия"), QString::number(d.version));
    add(tr("Серийный номер"), QString::fromStdString(c.serial));
    add(tr("Алгоритм подписи"), QString::fromStdString(d.signatureAlgorithm));
    add(tr("Издатель"), QString::fromStdString(c.issuer));
    add(tr("Действителен с"), SignTexts::dateTime(c.notBefore));
    add(tr("Действителен по"), SignTexts::dateTime(c.notAfter));
    add(tr("Субъект"), QString::fromStdString(c.subject));
    add(tr("Открытый ключ"), QString::fromStdString(d.publicKey));
    for (const auto& [name, value] : d.extensions) {
        add(QString::fromStdString(name), QString::fromStdString(value).trimmed());
    }
    add(tr("Отпечаток SHA-1"), QString::fromStdString(d.sha1));
    add(tr("Отпечаток SHA-256"), QString::fromStdString(d.sha256));
    details_->resizeColumnToContents(0);
    if (details_->topLevelItemCount() > 0) {
        details_->setCurrentItem(details_->topLevelItem(0));
    }

    // Отзыв.
    if (selected_ < static_cast<int>(revocations_.size())) {
        const sign::RevocationResult& r = revocations_[static_cast<std::size_t>(selected_)];
        QStringList rev;
        rev << tr("Состояние: %1").arg(SignTexts::revocation(r.status));
        if (r.source == sign::RevocationSource::Ocsp) {
            rev << tr("Проверено по ответу OCSP%1")
                       .arg(r.responder.empty() ? QString()
                                                : tr(" от «%1»").arg(QString::fromStdString(r.responder)));
        } else if (r.source == sign::RevocationSource::Crl) {
            rev << tr("Проверено по списку отзыва (CRL)");
        }
        if (!r.thisUpdate.empty()) {
            rev << tr("Сведения от %1, следующие — %2")
                       .arg(SignTexts::dateTime(r.thisUpdate), SignTexts::dateTime(r.nextUpdate));
        }
        if (!r.revocationTime.empty()) {
            rev << tr("Отозван %1").arg(SignTexts::dateTime(r.revocationTime));
        }
        if (!r.detail.empty()) {
            rev << tr("Подробнее: %1").arg(QString::fromStdString(r.detail));
        }
        revocation_->setText(rev.join(QLatin1Char('\n')));
    } else {
        revocation_->setText(
            tr("Отзыв этого сертификата не проверялся: это доверенный якорь, или подпись "
               "проверялась без доверия, или проверка отзыва выключена."));
    }

    // Доверие.
    const std::shared_ptr<const sign::TrustStore> store = TrustService::shared().snapshot();
    const sign::TrustAnchor* anchor = store->find(sign::ossl::toHex(c.sha256));
    if (anchor != nullptr) {
        trust_->setText(
            tr("Сертификат — доверенный якорь (%1). Доверять для: %2.")
                .arg(SignTexts::trustSource(anchor->source), SignTexts::trustFlags(anchor->flags)));
        SignTexts::setNote(trust_, Tone::Ok);
        trustButton_->setText(tr("&Изменить доверие…"));
    } else {
        trust_->setText(
            tr("Сертификата нет среди доверенных. Подписи, цепочка которых доходит до доверенного "
               "сертификата, считаются действительными."));
        SignTexts::setNote(trust_, Tone::Info);
        trustButton_->setText(tr("&Добавить в доверенные…"));
    }

    // Политики.
    if (d.policies.empty()) {
        policies_->setText(tr("Политики сертификата не указаны."));
    } else {
        QStringList p;
        for (const std::string& oid : d.policies) {
            p << QString::fromStdString(oid);
        }
        policies_->setText(tr("Политики сертификата (OID):\n%1").arg(p.join(QLatin1Char('\n'))));
    }
}

void CertificateViewer::editTrust() {
    if (certs_.empty()) {
        return;
    }
    const sign::CertSummary& c = certs_[static_cast<std::size_t>(selected_)];
    const std::string sha = sign::ossl::toHex(c.sha256);
    const std::shared_ptr<const sign::TrustStore> store = TrustService::shared().snapshot();
    const sign::TrustAnchor* anchor = store->find(sha);
    TrustDialog dialog(c, anchor != nullptr ? anchor->flags : sign::kTrustSignatures, anchor != nullptr,
                       this);
    std::optional<unsigned> flags;
    if (trustPrompt_) {
        flags = trustPrompt_(dialog);
    } else if (dialog.exec() == QDialog::Accepted) {
        flags = dialog.flags();
    }
    if (!flags) {
        return;
    }
    const sign::Bytes der = c.der;
    const bool existing = anchor != nullptr;
    const unsigned value = *flags;
    TrustService::shared().edit([&](sign::TrustStore& s) {
        if (existing) {
            s.setFlags(sha, value);
        } else {
            s.add(der, value);
        }
    });
    fill();
    emit trustChanged();
}

void CertificateViewer::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void CertificateViewer::retranslate() {
    setWindowTitle(tr("Просмотр сертификата"));
    chainCaption_->setText(tr("Цепочка сертификатов"));
    chain_->setAccessibleName(tr("Цепочка сертификатов"));
    const QStringList tabs = {tr("Сводка"), tr("Детали"), tr("Отзыв"), tr("Доверие"), tr("Политики")};
    for (int i = 0; i < tabs_->count(); ++i) {
        tabs_->setTabText(i, tabs.value(i));
    }
    details_->setHeaderLabels({tr("Поле"), tr("Значение")});
    close_->setText(tr("Закрыть"));
    fill();
}

}  // namespace ot::ui
