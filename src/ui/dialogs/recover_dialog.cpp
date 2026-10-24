#include "ui/dialogs/recover_dialog.h"

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

RecoverDialog::RecoverDialog(std::vector<AutosaveEntry> entries, QWidget* parent)
    : QDialog(parent), entries_(std::move(entries)) {
    setObjectName(QStringLiteral("recoverDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("file-text"), IconTone::Accent, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    text_ = new QLabel(this);
    text_->setObjectName(QStringLiteral("recoverText"));
    text_->setWordWrap(true);
    head->addWidget(text_, 1);
    column->addLayout(head);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("recoverList"));
    const QLocale locale;
    for (const AutosaveEntry& e : entries_) {
        const QString path = QString::fromStdWString(e.path.wstring());
        const QString when =
            e.savedAt > 0 ? locale.toString(QDateTime::fromSecsSinceEpoch(e.savedAt), QLocale::ShortFormat)
                          : QString();
        auto* item = new QListWidgetItem(list_);
        item->setText(when.isEmpty() ? QFileInfo(path).fileName()
                                     : tr("%1 — изменения от %2").arg(QFileInfo(path).fileName(), when));
        item->setToolTip(QDir::toNativeSeparators(path));
        item->setIcon(Icons::icon(QStringLiteral("file-text")));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
    }
    list_->setMinimumHeight(m.row *
                            std::min<int>(6, std::max<int>(2, static_cast<int>(entries_.size()) + 1)));
    column->addWidget(list_, 1);
    auto* buttons = new QHBoxLayout();
    later_ = new QPushButton(this);
    discard_ = new QPushButton(this);
    recover_ = new QPushButton(this);
    recover_->setDefault(true);
    buttons->addWidget(later_);
    buttons->addStretch(1);
    buttons->addWidget(discard_);
    buttons->addWidget(recover_);
    column->addLayout(buttons);
    connect(recover_, &QPushButton::clicked, this, [this] { finish(Choice::Recover); });
    connect(discard_, &QPushButton::clicked, this, [this] { finish(Choice::Discard); });
    connect(later_, &QPushButton::clicked, this, [this] { finish(Choice::Later); });
    connect(list_, &QListWidget::itemChanged, this,
            [this] { recover_->setEnabled(!selectedEntries().empty()); });
    setMinimumWidth(m.dialogWidth * 2 / 3);
    retranslate();
}

std::vector<AutosaveEntry> RecoverDialog::selectedEntries() const {
    std::vector<AutosaveEntry> out;
    for (int i = 0; i < list_->count() && i < static_cast<int>(entries_.size()); ++i) {
        if (list_->item(i)->checkState() == Qt::Checked) {
            out.push_back(entries_[static_cast<std::size_t>(i)]);
        }
    }
    return out;
}

void RecoverDialog::finish(Choice choice) {
    choice_ = choice;
    if (choice == Choice::Recover) {
        emit recoverRequested(selectedEntries());
        accept();
    } else if (choice == Choice::Discard) {
        emit discardRequested(entries_);
        accept();
    } else {
        reject();
    }
}

void RecoverDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void RecoverDialog::retranslate() {
    setWindowTitle(tr("Восстановление документов"));
    text_->setText(
        tr("<b>Восстановить несохранённые изменения?</b><br>Программа закрылась, не сохранив "
           "изменения в этих документах. Отмеченные откроются с изменениями — их можно будет "
           "сохранить или отменить."));
    recover_->setText(tr("&Восстановить"));
    discard_->setText(tr("&Не восстанавливать"));
    discard_->setToolTip(tr("Удалить сохранённые изменения всех документов из списка"));
    later_->setText(tr("&Позже"));
    later_->setToolTip(tr("Спросить при следующем запуске"));
}

}  // namespace ot::ui
