#include "ui/tools/certificate/warnings_dialog.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"

namespace ot::ui {

namespace {

constexpr int kWarningPageRole = Qt::UserRole + 1;

}  // namespace

WarningsDialog::WarningsDialog(sign::WarningScan scan, QWidget* parent)
    : QDialog(parent), scan_(std::move(scan)) {
    setObjectName(QStringLiteral("warningsDialog"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("warningsSummary"));
    summary_->setWordWrap(true);
    column->addWidget(summary_);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("warningsList"));
    list_->setColumnCount(3);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(true);
    column->addWidget(list_, 1);
    auto* buttons = new QDialogButtonBox(this);
    review_ = buttons->addButton(QString(), QDialogButtonBox::ActionRole);
    review_->setObjectName(QStringLiteral("warningsReview"));
    review_->setEnabled(false);
    close_ = buttons->addButton(QDialogButtonBox::Close);
    close_->setDefault(true);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    connect(review_, &QPushButton::clicked, this, [this] { emit reviewRequested(selectedPage()); });
    connect(list_, &QTreeWidget::currentItemChanged, this,
            [this] { review_->setEnabled(selectedPage() >= 0); });
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (selectedPage() >= 0) {
            emit reviewRequested(selectedPage());
        }
    });
    column->addWidget(buttons);
    resize(m.dialogWidth, m.dialogHeight * 2 / 3);
    retranslate();
}

int WarningsDialog::selectedPage() const {
    const QTreeWidgetItem* item = list_->currentItem();
    return item != nullptr ? item->data(0, kWarningPageRole).toInt() : -1;
}

void WarningsDialog::fill() {
    list_->clear();
    const int icon = Theme::sizes().iconRow;
    for (const sign::DocumentWarning& w : scan_.warnings) {
        auto* item = new QTreeWidgetItem(list_);
        IconTone tone = IconTone::Info;
        QString iconName = QStringLiteral("info");
        if (w.level == sign::WarningLevel::Danger) {
            tone = IconTone::Danger;
            iconName = QStringLiteral("circle-alert");
        } else if (w.level == sign::WarningLevel::Warning) {
            tone = IconTone::Warn;
            iconName = QStringLiteral("triangle-alert");
        }
        item->setIcon(0, QIcon(Icons::get(iconName, tone, icon)));
        item->setText(0, SignTexts::warningKind(w.kind));
        item->setText(1, SignTexts::warningPlace(w));
        item->setText(2, QString::fromStdString(w.detail));
        item->setToolTip(2, QString::fromStdString(w.detail));
        item->setData(0, kWarningPageRole, w.page);
    }
    list_->resizeColumnToContents(0);
    list_->resizeColumnToContents(1);
    if (list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
}

void WarningsDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void WarningsDialog::retranslate() {
    setWindowTitle(tr("Предупреждения документа"));
    if (scan_.clean()) {
        summary_->setText(
            tr("Предупреждений нет: в документе не найдено ничего, что меняло бы его вид или "
               "поведение после подписи."));
        SignTexts::setNote(summary_, Tone::Ok);
    } else {
        summary_->setText(
            tr("Документ содержит то, что может изменить его вид или поведение после подписи "
               "(%n шт.). Просмотрите это, прежде чем подписывать.",
               nullptr, static_cast<int>(scan_.warnings.size())));
        SignTexts::setNote(summary_, scan_.worst() == sign::WarningLevel::Info ? Tone::Info : Tone::Warn);
    }
    list_->setHeaderLabels({tr("Что найдено"), tr("Где"), tr("Подробно")});
    list_->setAccessibleName(tr("Предупреждения"));
    review_->setText(tr("&Просмотреть"));
    close_->setText(tr("Закрыть"));
    fill();
}

}  // namespace ot::ui
