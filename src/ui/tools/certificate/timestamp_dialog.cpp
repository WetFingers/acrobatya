#include "ui/tools/certificate/timestamp_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "services/settings.h"
#include "services/tsa_settings.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

TimestampDialog::TimestampDialog(QWidget* parent) : QDialog(parent), list_(loadTsaServers(Settings())) {
    setObjectName(QStringLiteral("timestampDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("clock-check"), IconTone::Accent, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    intro_ = new QLabel(this);
    intro_->setWordWrap(true);
    head->addWidget(intro_, 1);
    column->addLayout(head);
    auto* row = new QHBoxLayout();
    serverLabel_ = new QLabel(this);
    server_ = new QComboBox(this);
    server_->setObjectName(QStringLiteral("timestampServer"));
    serverLabel_->setBuddy(server_);
    for (const sign::TsaServer& s : list_.servers()) {
        server_->addItem(QString::fromStdString(s.name), QString::fromStdString(s.name));
        server_->setItemData(server_->count() - 1, QString::fromStdString(s.url), Qt::ToolTipRole);
    }
    const int def = server_->findData(QString::fromStdString(list_.defaultName()));
    server_->setCurrentIndex(def >= 0 ? def : 0);
    servers_ = new QPushButton(this);
    servers_->setObjectName(QStringLiteral("timestampServers"));
    connect(servers_, &QPushButton::clicked, this, [this] {
        emit serversRequested();
        reject();
    });
    row->addWidget(serverLabel_);
    row->addWidget(server_, 1);
    row->addWidget(servers_);
    column->addLayout(row);
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("timestampNote"));
    note_->setWordWrap(true);
    column->addWidget(note_);
    column->addStretch(1);
    auto* buttons = new QDialogButtonBox(this);
    add_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    add_->setObjectName(QStringLiteral("timestampAdd"));
    add_->setDefault(true);
    add_->setProperty("primary", true);
    repolish(add_);
    add_->setEnabled(server_->count() > 0);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth * 3 / 5);
    retranslate();
}

std::optional<sign::TsaServer> TimestampDialog::server() const {
    if (const sign::TsaServer* s = list_.find(server_->currentData().toString().toStdString())) {
        return *s;
    }
    return std::nullopt;
}

void TimestampDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void TimestampDialog::retranslate() {
    setWindowTitle(tr("Метка времени документа"));
    intro_->setText(
        tr("Метка времени доказывает, что документ в таком виде существовал в момент метки. Удостоверение не "
           "нужно; подписи документа она не задевает. Метка дописывается в файл отдельной частью."));
    serverLabel_->setText(tr("&Служба времени:"));
    servers_->setText(tr("Серверы &времени…"));
    if (server_->count() == 0) {
        note_->setText(tr("Список серверов времени пуст: добавьте сервер в Установках → Подписи."));
        SignTexts::setNote(note_, Tone::Warn);
        note_->show();
    } else {
        note_->setText(tr("Нужна сеть: служба времени выдаёт метку по запросу."));
        SignTexts::setNote(note_, Tone::Info);
    }
    add_->setText(tr("&Добавить метку"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
