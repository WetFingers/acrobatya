#include "ui/dialogs/replace_dialog.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "ui/theme/theme.h"

namespace ot::ui {

ReplaceDialog::ReplaceDialog(const QString& needle, const QString& replacement,
                             std::vector<edit::ReplaceMatch> matches, QWidget* parent)
    : QDialog(parent), matches_(std::move(matches)) {
    setObjectName(QStringLiteral("replaceDialog"));
    setWindowTitle(tr("Заменить все"));
    const Metrics& m = Theme::sizes();
    resize(m.dialogWidth, m.dialogHeight * 3 / 4);
    auto* column = new QVBoxLayout(this);
    column->setSpacing(m.grid * 3);
    summary_ = new QLabel(this);
    summary_->setObjectName(QStringLiteral("replaceSummary"));
    summary_->setWordWrap(true);
    column->addWidget(summary_);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("replaceList"));
    list_->setRootIsDecorated(false);
    list_->setHeaderLabels({tr("Стр."), tr("Было"), tr("Станет")});
    list_->header()->setStretchLastSection(true);
    list_->setAccessibleName(tr("Совпадения"));
    for (const edit::ReplaceMatch& match : matches_) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, QString::number(match.hit.page + 1));
        auto plain = [](std::string s) {
            QString t = QString::fromStdString(s);
            t.remove(QChar(0x02));
            t.remove(QChar(0x03));
            return t;
        };
        item->setText(1, plain(match.context));
        if (match.replaceable()) {
            item->setText(2, plain(match.result));
            item->setToolTip(2, fragmentHtml(match.result));
        } else {
            item->setText(2, skipText(match.skip));
            item->setForeground(2, Theme::colors().textMuted.color());
            item->setForeground(1, Theme::colors().textMuted.color());
        }
        item->setToolTip(1, fragmentHtml(match.context));
    }
    list_->resizeColumnToContents(0);
    list_->setColumnWidth(1, m.dialogWidth / 2 - m.grid * 8);
    column->addWidget(list_, 1);
    auto* buttons = new QDialogButtonBox(this);
    replace_ = buttons->addButton(tr("&Заменить все"), QDialogButtonBox::AcceptRole);
    replace_->setObjectName(QStringLiteral("replaceAllButton"));
    replace_->setDefault(true);
    close_ = buttons->addButton(tr("Отмена"), QDialogButtonBox::RejectRole);
    close_->setObjectName(QStringLiteral("replaceCloseButton"));
    connect(replace_, &QPushButton::clicked, this, [this] {
        replace_->setEnabled(false);
        emit replaceAllRequested();
    });
    connect(close_, &QPushButton::clicked, this, &QDialog::reject);
    column->addWidget(buttons);

    const int total = static_cast<int>(matches_.size());
    const int ok = replaceable();
    if (total == 0) {
        summary_->setText(tr("«%1» не найдено.").arg(needle));
        replace_->setEnabled(false);
    } else {
        summary_->setText(tr("Найдено: %1. Будет заменено на «%2»: %3. Пропущено: %4.")
                              .arg(total)
                              .arg(replacement)
                              .arg(ok)
                              .arg(total - ok));
        replace_->setEnabled(ok > 0);
    }
}

int ReplaceDialog::replaceable() const {
    int n = 0;
    for (const edit::ReplaceMatch& m : matches_) {
        n += m.replaceable() ? 1 : 0;
    }
    return n;
}

QString ReplaceDialog::skipText(edit::ReplaceSkip skip) {
    switch (skip) {
        case edit::ReplaceSkip::CrossBlock:
            return tr("Пропущено: совпадение через границу абзацев или страниц");
        case edit::ReplaceSkip::ReadOnly:
            return tr("Пропущено: абзац только для чтения");
        case edit::ReplaceSkip::NoGlyphs:
            return tr("Пропущено: текст не найден в содержимом страницы");
        case edit::ReplaceSkip::Failed:
            return tr("Пропущено: ошибка записи");
        case edit::ReplaceSkip::None:
            break;
    }
    return {};
}

QString ReplaceDialog::fragmentHtml(const std::string& text) {
    QString t = QString::fromStdString(text).toHtmlEscaped();
    t.replace(QChar(0x02), QStringLiteral("<b>"));
    t.replace(QChar(0x03), QStringLiteral("</b>"));
    return t;
}

void ReplaceDialog::showReport(const edit::ReplaceReport& report) {
    reported_ = true;
    QString text = tr("Заменено: %1. Пропущено: %2.").arg(report.replaced).arg(report.skipped.size());
    if (!report.notices.empty()) {
        text += QLatin1Char(' ') + tr("Для части символов шрифт подменён или дополнен системным.");
    }
    summary_->setText(text);
    replace_->hide();
    close_->setText(tr("Закрыть"));
    close_->setDefault(true);
}

}  // namespace ot::ui
