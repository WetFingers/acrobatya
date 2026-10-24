#include "ui/dialogs/link_confirm_dialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

LinkConfirmDialog::LinkConfirmDialog(Kind kind, const QString& target, const QString& host, QWidget* parent)
    : QDialog(parent), kind_(kind), targetText_(target), host_(kind == Kind::Url ? host : QString()) {
    setObjectName(QStringLiteral("linkConfirmDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("shield-alert"), IconTone::Warn, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    auto* body = new QVBoxLayout();
    body->setSpacing(m.grid * 2);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("linkConfirmText"));
    text_->setWordWrap(true);
    body->addWidget(text_);
    target_ = new QLabel(this);
    target_->setObjectName(QStringLiteral("linkConfirmTarget"));
    target_->setTextFormat(Qt::PlainText);
    target_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont bold = uiFont(m);
    bold.setWeight(QFont::DemiBold);
    target_->setFont(bold);
    // Длинный адрес — с многоточием посередине; целиком — в подсказке.
    target_->setText(QFontMetrics(bold).elidedText(target, Qt::ElideMiddle, m.dialogWidth / 2));
    target_->setToolTip(target);
    body->addWidget(target_);
    hint_ = new QLabel(this);
    hint_->setObjectName(QStringLiteral("linkConfirmHint"));
    hint_->setWordWrap(true);
    body->addWidget(hint_);
    head->addLayout(body, 1);
    column->addLayout(head);

    auto* buttons = new QDialogButtonBox(this);
    open_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    open_->setObjectName(QStringLiteral("linkOpen"));
    open_->setDefault(true);
    if (!host_.isEmpty()) {
        allow_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
        allow_->setObjectName(QStringLiteral("linkAllowSite"));
    }
    block_ = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
    block_->setObjectName(QStringLiteral("linkBlock"));
    connect(open_, &QPushButton::clicked, this, [this] { choose(Choice::Open); });
    if (allow_ != nullptr) {
        connect(allow_, &QPushButton::clicked, this, [this] { choose(Choice::AllowSite); });
    }
    connect(block_, &QPushButton::clicked, this,
            [this] { choose(kind_ == Kind::Url ? Choice::Block : Choice::Cancel); });
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth / 2 + m.grid * 24);
    retranslate();
    open_->setFocus();
}

void LinkConfirmDialog::choose(Choice choice) {
    choice_ = choice;
    if (choice == Choice::Open || choice == Choice::AllowSite) {
        accept();
    } else {
        reject();
    }
}

void LinkConfirmDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void LinkConfirmDialog::retranslate() {
    setWindowTitle(tr("Предупреждение системы безопасности"));
    if (kind_ == Kind::Url) {
        text_->setText(tr("Документ пытается открыть ссылку в браузере:"));
        hint_->setText(host_.isEmpty()
                           ? tr("Открывайте ссылку, только если доверяете документу.")
                           : tr("Открывайте ссылку, только если доверяете документу и сайту %1. «Разрешить "
                                "для сайта» и «Запретить» запоминаются для всех документов (Установки → "
                                "Диспетчер доверия).")
                                 .arg(host_));
        open_->setText(tr("&Открыть"));
        if (allow_ != nullptr) {
            allow_->setText(tr("&Разрешить для сайта"));
        }
        block_->setText(tr("&Запретить"));
        target_->setAccessibleName(tr("Адрес ссылки"));
    } else {
        text_->setText(tr("Документ пытается открыть другой файл:"));
        hint_->setText(tr("Открывайте файл, только если доверяете документу."));
        open_->setText(tr("&Открыть"));
        block_->setText(tr("Отмена"));
        target_->setAccessibleName(tr("Путь к файлу"));
    }
}

}  // namespace ot::ui
