#include "ui/panes/signatures_pane.h"

#include <QAction>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include "core/sign/ltv.h"
#include "services/settings.h"
#include "services/sign_ops.h"
#include "services/trust_service.h"
#include "ui/dialogs/certificate_viewer.h"
#include "ui/dialogs/signature_properties_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/signature_status.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/certificate_tool.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/toast.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace signatures_pane {

constexpr int kFieldRole = Qt::UserRole + 1;

QIcon rowIcon(const QString& name, Tone tone) {
    return QIcon(Icons::get(name, toneIcon(tone), Theme::sizes().iconRow));
}

QString signerName(const sign::SignatureInfo& s) {
    if (s.signer && !s.signer->commonName.empty()) {
        return QString::fromStdString(s.signer->commonName);
    }
    if (!s.name.empty()) {
        return QString::fromStdString(s.name);
    }
    return s.signer ? QString::fromStdString(s.signer->subject) : QString();
}

QString issuerName(const sign::SignatureInfo& s) {
    if (!s.signer) {
        return {};
    }
    const QString issuer = QString::fromStdString(s.signer->issuer);
    const int at = issuer.indexOf(QLatin1String("CN="));
    if (at < 0) {
        return issuer;
    }
    const int end = issuer.indexOf(QLatin1Char(','), at);
    return issuer.mid(at + 3, end < 0 ? -1 : end - at - 3).trimmed();
}

QTreeWidgetItem* child(QTreeWidgetItem* parent, const QString& text, const QIcon& icon = {}) {
    auto* item = new QTreeWidgetItem(parent);
    item->setText(0, text);
    item->setToolTip(0, text);
    if (!icon.isNull()) {
        item->setIcon(0, icon);
    }
    return item;
}

// Перенос строк дерева по ширине столбца: QTreeView сам высоту строки с переносом не считает (ширины в
// sizeHint нет) — длинное «Подпись 1: Иванов И. (действительна)» обрезалось бы многоточием.
class WrapDelegate : public QStyledItemDelegate {
public:
    explicit WrapDelegate(QTreeView* view) : QStyledItemDelegate(view), view_(view) {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize base = QStyledItemDelegate::sizeHint(option, index);
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        int depth = 0;
        for (QModelIndex p = index.parent(); p.isValid(); p = p.parent()) {
            ++depth;
        }
        const int indent = view_->indentation() * (depth + (view_->rootIsDecorated() ? 1 : 0));
        const int icon = opt.icon.isNull() ? 0 : opt.decorationSize.width() + Theme::sizes().grid;
        const int width = view_->viewport()->width() - indent - icon - Theme::sizes().grid * 3;
        if (width <= 0) {
            return base;
        }
        const QRect text =
            opt.fontMetrics.boundingRect(QRect(0, 0, width, 100000), Qt::TextWordWrap, opt.text);
        base.setHeight(std::max(base.height(), text.height() + Theme::sizes().grid));
        return base;
    }

private:
    QTreeView* view_;
};

}  // namespace signatures_pane

SignaturesPane::SignaturesPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("signaturesPane"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto* top = new QWidget(this);
    auto* tl = new QVBoxLayout(top);
    tl->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, m.grid * 2);
    tl->setSpacing(m.grid * 2);
    auto* row = new QHBoxLayout();
    verify_ = new QPushButton(Icons::icon(QStringLiteral("shield-check")), QString(), top);
    verify_->setObjectName(QStringLiteral("signaturesVerifyAll"));
    connect(verify_, &QPushButton::clicked, this, &SignaturesPane::verifyAll);
    options_ = new QToolButton(top);
    options_->setObjectName(QStringLiteral("signaturesOptions"));
    options_->setPopupMode(QToolButton::InstantPopup);
    options_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    optionsMenu_ = new QMenu(options_);
    options_->setMenu(optionsMenu_);
    row->addWidget(verify_);
    row->addStretch(1);
    row->addWidget(options_);
    tl->addLayout(row);
    summary_ = new QLabel(top);
    summary_->setObjectName(QStringLiteral("signaturesSummary"));
    summary_->setWordWrap(true);
    tl->addWidget(summary_);
    column->addWidget(top);

    stack_ = new QStackedWidget(this);
    tree_ = new QTreeWidget(stack_);
    tree_->setObjectName(QStringLiteral("signatureTree"));
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setRootIsDecorated(true);
    // Строки переносятся (панель 280 px): «Подпись 1: Иванов И. (действительна)» целиком, как в Acrobat.
    tree_->setWordWrap(true);
    tree_->setTextElideMode(Qt::ElideNone);
    tree_->header()->setStretchLastSection(true);
    tree_->header()->setSectionResizeMode(QHeaderView::Stretch);
    tree_->setItemDelegate(new signatures_pane::WrapDelegate(tree_));
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const QString field = fieldOf(tree_->itemAt(pos));
        if (field.isEmpty()) {
            return;
        }
        QMenu* menu = menuFor(field);
        menu->exec(tree_->viewport()->mapToGlobal(pos));
        delete menu;
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const QString field = fieldOf(item);
        if (!field.isEmpty() && item->parent() == nullptr) {
            goToField(field);
        }
    });
    empty_ = new EmptyState(QStringLiteral("file-signature"), QString(), QString(), stack_);
    stack_->addWidget(tree_);
    stack_->addWidget(empty_);
    column->addWidget(stack_, 1);
    connect(&Theme::instance(), &Theme::themeChanged, this, &SignaturesPane::fill);
    retranslate();
}

QString SignaturesPane::title() const { return tr("Подписи"); }

bool SignaturesPane::isEmptyShown() const { return stack_->currentWidget() == empty_; }

void SignaturesPane::setDocument(DocumentView* view) {
    for (const QMetaObject::Connection& c : connections_) {
        disconnect(c);
    }
    connections_.clear();
    view_ = view;
    if (view_ != nullptr) {
        SignatureStatus& status = view_->signatures();
        connections_.push_back(connect(&status, &SignatureStatus::started, this, &SignaturesPane::fill));
        connections_.push_back(connect(&status, &SignatureStatus::verified, this, &SignaturesPane::fill));
    }
    dirty_ = true;
    if (isVisible()) {
        activated();
    } else {
        fill();
    }
}

void SignaturesPane::activated() {
    // Панель открыли — подписи проверяются, если ещё не (проверка при открытии могла быть выключена).
    if (view_ != nullptr && !view_->isViewOnly() && !view_->signatures().hasResult() &&
        !view_->signatures().isVerifying()) {
        view_->signatures().verify();
    }
    dirty_ = false;
    fill();
}

QString SignaturesPane::fieldOf(const QTreeWidgetItem* item) const {
    while (item != nullptr && item->parent() != nullptr) {
        item = item->parent();
    }
    return item != nullptr ? item->data(0, signatures_pane::kFieldRole).toString() : QString();
}

QTreeWidgetItem* SignaturesPane::itemFor(const QString& field) const {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        if (tree_->topLevelItem(i)->data(0, signatures_pane::kFieldRole).toString() == field) {
            return tree_->topLevelItem(i);
        }
    }
    return nullptr;
}

QString SignaturesPane::itemText(const QString& field) const {
    QTreeWidgetItem* top = itemFor(field);
    if (top == nullptr) {
        return {};
    }
    QStringList lines{top->text(0)};
    for (QTreeWidgetItemIterator it(top); *it != nullptr; ++it) {
        QTreeWidgetItem* item = *it;
        const QTreeWidgetItem* p = item;
        while (p->parent() != nullptr) {
            p = p->parent();
        }
        if (p != top) {
            break;
        }
        if (item != top) {
            lines << item->text(0);
        }
    }
    return lines.join(QLatin1Char('\n'));
}

void SignaturesPane::fill() {
    tree_->clear();
    if (view_ == nullptr || view_->isViewOnly()) {
        summary_->clear();
        SignTexts::clearNote(summary_);
        summary_->hide();
        verify_->setEnabled(false);
        stack_->setCurrentWidget(empty_);
        emit rowsChanged();
        return;
    }
    const SignatureStatus& status = view_->signatures();
    verify_->setEnabled(!status.isVerifying());
    summary_->show();
    if (status.isVerifying()) {
        summary_->setText(tr("Проверка подписей…"));
        SignTexts::setNote(summary_, Tone::Info);
    } else if (!status.error().isEmpty()) {
        summary_->setText(tr("Подписи проверить не удалось: файл не открылся."));
        SignTexts::setNote(summary_, Tone::Warn);
    } else if (status.hasResult() && status.verification().status != sign::DocumentStatus::NotSigned) {
        const sign::DocumentStatus s = status.verification().status;
        summary_->setText(SignTexts::documentStatus(s));
        SignTexts::setNote(summary_, s == sign::DocumentStatus::Invalid    ? Tone::Danger
                                     : s == sign::DocumentStatus::Problems ? Tone::Warn
                                                                           : Tone::Info);
    } else {
        summary_->hide();
    }
    for (const sign::SignatureInfo& sig : status.verification().signatures) {
        addSignature(sig);
    }
    // Пустые поля подписи.
    for (const sign::SignatureField& f : status.fields()) {
        if (f.isSigned) {
            continue;
        }
        auto* item = new QTreeWidgetItem(tree_);
        const QString name = QString::fromStdString(f.name);
        item->setText(0, f.page >= 0 ? tr("Пустое поле подписи «%1» — стр. %2").arg(name).arg(f.page + 1)
                                     : tr("Пустое поле подписи «%1»").arg(name));
        item->setToolTip(0, item->text(0));
        item->setIcon(0, signatures_pane::rowIcon(QStringLiteral("signature"), Tone::Neutral));
        item->setData(0, signatures_pane::kFieldRole, name);
    }
    const bool none = tree_->topLevelItemCount() == 0;
    stack_->setCurrentWidget(none && !status.isVerifying() ? static_cast<QWidget*>(empty_) : tree_);
    emit rowsChanged();
}

void SignaturesPane::addSignature(const sign::SignatureInfo& sig) {
    using signatures_pane::child;
    using signatures_pane::rowIcon;
    auto* top = new QTreeWidgetItem(tree_);
    const bool cert = sig.kind == sign::SignatureKind::Certification;
    const QString number = QString::number(sig.index + 1);
    QString kind = tr("Подпись %1").arg(number);
    if (cert) {
        kind = tr("Сертифицирующая подпись %1").arg(number);
    } else if (sig.kind == sign::SignatureKind::DocTimestamp) {
        kind = tr("Метка времени %1").arg(number);
    }
    top->setText(
        0, tr("%1: %2 (%3)").arg(kind, signatures_pane::signerName(sig), SignTexts::verdict(sig.verdict)));
    top->setToolTip(0, top->text(0));
    top->setIcon(0, rowIcon(SignTexts::verdictIcon(sig.verdict, cert), SignTexts::verdictTone(sig.verdict)));
    top->setData(0, signatures_pane::kFieldRole, QString::fromStdString(sig.field));

    // Статус и причины.
    QTreeWidgetItem* status = child(top, tr("Статус: %1").arg(SignTexts::verdict(sig.verdict)));
    for (const sign::Finding& f : sig.findings) {
        const std::optional<sign::Verdict> severity = sign::issueSeverity(f.issue);
        const Tone tone = severity ? SignTexts::verdictTone(*severity) : Tone::Info;
        QTreeWidgetItem* line = child(status, SignTexts::issue(f.issue),
                                      rowIcon(tone == Tone::Danger ? QStringLiteral("circle-alert")
                                              : tone == Tone::Warn ? QStringLiteral("triangle-alert")
                                                                   : QStringLiteral("info"),
                                              tone));
        line->setToolTip(0, QString::fromStdString(f.detail));
    }
    if (sig.kind == sign::SignatureKind::DocTimestamp) {
        child(top, tr("Служба времени: %1").arg(signatures_pane::signerName(sig)));
    } else {
        const QString issuer = signatures_pane::issuerName(sig);
        child(top, issuer.isEmpty()
                       ? tr("Подписант: %1").arg(signatures_pane::signerName(sig))
                       : tr("Подписант: %1, выдан: %2").arg(signatures_pane::signerName(sig), issuer));
    }
    // Время и метка.
    const std::string when = sig.timestamp ? sig.timestamp->genTime : sig.claimedTime;
    if (!when.empty()) {
        child(top, tr("Время подписи: %1").arg(SignTexts::dateTime(when)));
    }
    child(top, SignTexts::timeSource(sig.timeSource));
    if (sig.timestamp && sig.kind != sign::SignatureKind::DocTimestamp) {
        const QString tsa = sig.timestamp->signer.commonName.empty()
                                ? QString::fromStdString(sig.timestamp->tsaName)
                                : QString::fromStdString(sig.timestamp->signer.commonName);
        child(top, tr("Метка времени: служба «%1» — %2")
                       .arg(tsa, sig.timestampTrusted ? tr("доверена") : tr("не доверена")));
    }
    if (!sig.reason.empty()) {
        child(top, tr("Причина: %1").arg(QString::fromStdString(sig.reason)));
    }
    if (!sig.location.empty()) {
        child(top, tr("Место: %1").arg(QString::fromStdString(sig.location)));
    }
    if (!sig.contact.empty()) {
        child(top, tr("Контакт: %1").arg(QString::fromStdString(sig.contact)));
    }
    const QString level = SignTexts::padesLevel(sig.level);
    child(top, level.isEmpty() ? tr("Формат: %1").arg(QString::fromStdString(sig.subFilter))
                               : tr("Формат: %1 · %2").arg(QString::fromStdString(sig.subFilter), level));
    child(top, sig.ltv ? tr("LTV включено") : tr("LTV не включено"),
          rowIcon(sig.ltv ? QStringLiteral("circle-check") : QStringLiteral("info"),
                  sig.ltv ? Tone::Ok : Tone::Neutral));
    if (cert || sig.docMdp != sign::MdpPermission::None) {
        child(top, tr("Разрешённые изменения: %1").arg(SignTexts::permission(sig.docMdp)));
    }
    // Изменения после подписи.
    if (!sig.changes.modified) {
        child(top, tr("Изменений после подписи нет"));
    } else {
        const int count = static_cast<int>(sig.changes.changes.size());
        QTreeWidgetItem* changes =
            sig.changes.allowed
                ? child(top, tr("После подписи внесены разрешённые изменения (%n)", nullptr, count),
                        rowIcon(QStringLiteral("circle-check"), Tone::Ok))
                : child(top, tr("Изменения после подписи не разрешены (%n)", nullptr, count),
                        rowIcon(QStringLiteral("circle-alert"), Tone::Danger));
        for (const sign::Change& c : sig.changes.changes) {
            QString text = SignTexts::changeKind(c.kind);
            if (c.page >= 0) {
                text += tr(" (стр. %1)").arg(c.page + 1);
            }
            text += c.allowed ? tr(" — разрешено") : tr(" — не разрешено");
            QTreeWidgetItem* line = child(changes, text);
            line->setToolTip(0, QString::fromStdString(c.detail));
        }
    }
    const QString field = QString::fromStdString(sig.field);
    child(top, sig.visible ? tr("Поле «%1», стр. %2").arg(field).arg(sig.page + 1)
                           : tr("Невидимая подпись, поле «%1»").arg(field));
    top->setExpanded(false);
}

// ---------------------------------------------------------------- меню и действия

QMenu* SignaturesPane::menuFor(const QString& field) {
    auto* menu = new QMenu(this);
    const auto add = [&](const char* name, const QString& icon, const QString& text, auto slot,
                         bool enabled = true, const QString& tip = {}) {
        QAction* a = menu->addAction(icon.isEmpty() ? QIcon() : Icons::icon(icon), text);
        a->setObjectName(QLatin1String(name));
        a->setEnabled(enabled);
        if (!tip.isEmpty()) {
            a->setToolTip(tip);
        }
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    const sign::SignatureInfo* sig = view_ != nullptr ? view_->signatures().signature(field) : nullptr;
    if (sig == nullptr) {
        // Пустое поле.
        add("sigSignField", QStringLiteral("signature"), tr("&Подписать поле…"),
            [this, field] { signField(field); });
        add("sigGoTo", QStringLiteral("locate"), tr("Перейти к &полю подписи"),
            [this, field] { goToField(field); });
        return menu;
    }
    add(
        "sigGoTo", QStringLiteral("locate"), tr("Перейти к &полю подписи"),
        [this, field] { goToField(field); }, sig->page >= 0);
    add("sigVerify", QStringLiteral("shield-check"), tr("Проверить &подпись"), [this] { verifyAll(); });
    add("sigVerifyAll", {}, tr("Проверить &все подписи"), [this] { verifyAll(); });
    menu->addSeparator();
    add("sigSignedVersion", QStringLiteral("history"), tr("Просмотреть подписанную &версию"),
        [this, field] { viewSignedVersion(field); });
    add(
        "sigAddLtv", QStringLiteral("file-check"), tr("&Добавить сведения для проверки"),
        [this] { addValidationInfo(); }, view_->canSave());
    QString why;
    const bool clear = canClear(field, &why);
    add(
        "sigClear", QStringLiteral("eraser"), tr("&Очистить подпись"),
        [this, field] { clearSignature(field); }, clear, why);
    menu->addSeparator();
    add("sigProperties", {}, tr("&Свойства подписи…"), [this, field] { showProperties(field); });
    add(
        "sigCertificate", QStringLiteral("id-card"), tr("Показать с&ертификат подписанта…"),
        [this, field] { showCertificate(field); }, sig->signer.has_value());
    return menu;
}

void SignaturesPane::goToField(const QString& field) {
    if (view_ == nullptr) {
        return;
    }
    const std::string name = field.toStdString();
    for (const sign::SignatureField& f : view_->signatures().fields()) {
        if (f.name == name && f.page >= 0) {
            view_->view().goToPage(f.page);
            if (f.visible) {
                view_->view().ensureVisible(f.page, f.rect, 40);
            }
            return;
        }
    }
    if (const sign::SignatureInfo* s = view_->signatures().signature(field); s != nullptr && s->page >= 0) {
        view_->view().goToPage(s->page);
    }
}

void SignaturesPane::verifyAll() {
    if (view_ != nullptr) {
        view_->signatures().verify();
    }
}

SignaturePropertiesDialog* SignaturesPane::showProperties(const QString& field) {
    const sign::SignatureInfo* sig = view_ != nullptr ? view_->signatures().signature(field) : nullptr;
    if (sig == nullptr) {
        return nullptr;
    }
    auto* d = new SignaturePropertiesDialog(*sig, view_->signatures().verification().versions, window());
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &SignaturePropertiesDialog::certificateRequested, this,
            [this, field] { showCertificate(field); });
    connect(d, &SignaturePropertiesDialog::verifyRequested, this, &SignaturesPane::verifyAll);
    connect(d, &SignaturePropertiesDialog::signedVersionRequested, this,
            [this, field] { viewSignedVersion(field); });
    d->open();
    return d;
}

CertificateViewer* SignaturesPane::showCertificate(const QString& field) {
    const sign::SignatureInfo* sig = view_ != nullptr ? view_->signatures().signature(field) : nullptr;
    if (sig == nullptr) {
        return nullptr;
    }
    std::vector<sign::CertSummary> chain = sig->chain;
    if (chain.empty() && sig->signer) {
        // Цепочка не построена — подписант и сертификаты из подписи.
        chain.push_back(*sig->signer);
        for (const sign::CertSummary& c : sig->certificates) {
            if (c.sha256 != sig->signer->sha256) {
                chain.push_back(c);
            }
        }
    }
    auto* v = new CertificateViewer(std::move(chain), sig->revocationDetails, window());
    v->setAttribute(Qt::WA_DeleteOnClose);
    v->open();
    return v;
}

void SignaturesPane::viewSignedVersion(const QString& field) {
    if (view_ == nullptr) {
        return;
    }
    QPointer<DocumentView> source = view_;
    const sign::SignatureInfo* sig = view_->signatures().signature(field);
    const QString label =
        sig != nullptr ? tr("подпись %1 — %2").arg(sig->index + 1).arg(signatures_pane::signerName(*sig))
                       : field;
    signing::signedVersion(view_->session(), field).then(this, [this, source, label](QFuture<QString> f) {
        QString path;
        try {
            path = f.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет файла — сообщение ниже
        }
        auto* w = qobject_cast<MainWindow*>(window());
        if (source == nullptr || w == nullptr) {
            return;
        }
        if (path.isEmpty() || w->openSignedVersion(*source, path, label) == nullptr) {
            notify(false, tr("Подписанную версию открыть не удалось."));
        }
    });
}

void SignaturesPane::addValidationInfo() {
    if (view_ == nullptr || !view_->canSave()) {
        return;
    }
    const Settings settings;
    view_->session()
        .addValidationInfo(TrustService::shared().snapshot(), settings.get(settings::kLtvVri))
        .then(this, [this](QFuture<sign::LtvResult> f) {
            try {
                const sign::LtvResult r = f.result();
                if (!r.save.ok()) {
                    notify(false, tr("Сведения для проверки не записаны: %1")
                                      .arg(QString::fromStdString(r.save.message)));
                } else if (!r.dss.written) {
                    notify(true, tr("Новых сведений для проверки нет: всё нужное уже в документе."));
                } else {
                    notify(true,
                           tr("Сведения для проверки добавлены: сертификатов %1, ответов OCSP %2, списков "
                              "отзыва %3.")
                               .arg(r.dss.certs)
                               .arg(r.dss.ocsps)
                               .arg(r.dss.crls));
                }
            } catch (const std::exception& e) {
                notify(false, tr("Сведения для проверки не добавлены: %1").arg(QString::fromUtf8(e.what())));
            }
        });
}

bool SignaturesPane::canClear(const QString& field, QString* why) const {
    const auto fail = [why](const QString& text) {
        if (why != nullptr) {
            *why = text;
        }
        return false;
    };
    if (view_ == nullptr || !view_->canSave() || !view_->canEdit()) {
        return fail(tr("Документ нельзя изменить."));
    }
    const sign::SignatureInfo* sig = view_->signatures().signature(field);
    if (sig == nullptr) {
        return fail(tr("Поле не подписано."));
    }
    for (const sign::SignatureInfo& other : view_->signatures().verification().signatures) {
        if (other.field != sig->field && other.rangeEnd > sig->rangeEnd) {
            return fail(tr("После этой подписи есть другие — очистка сделала бы их недействительными."));
        }
    }
    for (const sign::SignatureField& f : view_->signatures().fields()) {
        if (f.name == sig->field && f.readOnly) {
            return fail(tr("Поле подписи заблокировано — очистить его нельзя."));
        }
    }
    return true;
}

void SignaturesPane::clearSignature(const QString& field) {
    if (view_ == nullptr || !canClear(field)) {
        return;
    }
    const bool yes =
        confirm_ ? confirm_(field)
                 : QMessageBox::question(window(), tr("Очистить подпись"),
                                         tr("Убрать подпись из поля «%1»? Файл сохранится; прежняя "
                                            "версия с подписью останется в нём и будет видна как ревизия.")
                                             .arg(field)) == QMessageBox::Yes;
    if (!yes) {
        return;
    }
    QPointer<DocumentView> target = view_;
    signing::clearSignature(view_->session(), field).then(this, [this, target](QFuture<void> f) {
        try {
            f.waitForFinished();
        } catch (const std::exception& e) {
            notify(false, tr("Подпись очистить не удалось: %1").arg(QString::fromUtf8(e.what())));
            return;
        }
        if (target == nullptr) {
            return;
        }
        target->session().save().then(this, [this](QFuture<SaveReport> saved) {
            try {
                const SaveReport r = saved.result();
                notify(r.ok(), r.ok() ? tr("Подпись очищена, документ сохранён.")
                                      : tr("Подпись очищена, но документ не сохранён: %1")
                                            .arg(QString::fromStdString(r.message)));
            } catch (const std::exception& e) {
                notify(false, tr("Документ не сохранён: %1").arg(QString::fromUtf8(e.what())));
            }
        });
    });
}

void SignaturesPane::signField(const QString& field) {
    if (view_ == nullptr) {
        return;
    }
    const std::string name = field.toStdString();
    for (const sign::SignatureField& f : view_->signatures().fields()) {
        if (f.name == name && !f.isSigned) {
            ToolRegistry& registry = ToolRegistry::instance();
            registry.activate(QStringLiteral("certificate"), view_);
            if (auto* tool = dynamic_cast<CertificateTool*>(registry.find(QStringLiteral("certificate")))) {
                tool->placeSignature(f.page, f.rect, field);
            }
            return;
        }
    }
}

void SignaturesPane::notify(bool ok, const QString& message) {
    Toast::notify(window(), message, ok ? Tone::Ok : Tone::Danger);
    emit actionFinished(ok, message);
}

void SignaturesPane::retranslate() {
    verify_->setText(tr("Проверить все"));
    options_->setText(tr("Параметры"));
    optionsMenu_->clear();
    QAction* expand = optionsMenu_->addAction(tr("Развернуть все"));
    connect(expand, &QAction::triggered, tree_, &QTreeWidget::expandAll);
    QAction* collapse = optionsMenu_->addAction(tr("Свернуть все"));
    connect(collapse, &QAction::triggered, tree_, &QTreeWidget::collapseAll);
    optionsMenu_->addSeparator();
    QAction* ltv = optionsMenu_->addAction(tr("Добавить сведения для проверки"));
    ltv->setObjectName(QStringLiteral("signaturesAddLtv"));
    connect(ltv, &QAction::triggered, this, &SignaturesPane::addValidationInfo);
    empty_->setText(tr("В документе нет подписей."));
    tree_->setAccessibleName(tr("Подписи"));
    fill();
}

// Ширина панели сменилась — высоты строк с переносом заново.
void SignaturesPane::resizeEvent(QResizeEvent* event) {
    NavPage::resizeEvent(event);
    tree_->doItemsLayout();
}

void SignaturesPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
