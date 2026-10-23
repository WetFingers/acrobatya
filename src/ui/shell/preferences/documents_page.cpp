#include "ui/shell/preferences/documents_page.h"

#include <QCheckBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "services/settings.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

DocumentsPage::DocumentsPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("documentsPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    restoreView_ = new QCheckBox();
    restoreView_->setObjectName(QStringLiteral("restoreViewBox"));
    autosave_ = new QCheckBox();
    autosave_->setObjectName(QStringLiteral("autosaveBox"));
    minutes_ = new NumberField();
    minutes_->setObjectName(QStringLiteral("autosaveMinutesField"));
    minutes_->setDecimals(0);
    minutes_->setRange(settings::kAutosaveMinutes.min, settings::kAutosaveMinutes.max);
    connect(autosave_, &QCheckBox::toggled, minutes_, &QWidget::setEnabled);
    recovery_ = new QCheckBox();
    recovery_->setObjectName(QStringLiteral("recoveryBox"));
    pdfa_ = new QCheckBox();
    pdfa_->setObjectName(QStringLiteral("pdfaReadOnlyBox"));

    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(restoreView_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(autosave_);
    rows_ << sheet_->addRow(QString(), minutes_);
    sheet_->addWidget(recovery_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(pdfa_);
    retranslate();
}

QString DocumentsPage::title() const { return tr("Документы"); }

void DocumentsPage::load(const Settings& settings) {
    restoreView_->setChecked(settings.get(settings::kRestoreLastView));
    autosave_->setChecked(settings.get(settings::kAutosave));
    minutes_->setValue(settings.get(settings::kAutosaveMinutes));
    minutes_->setEnabled(autosave_->isChecked());
    recovery_->setChecked(settings.get(settings::kOfferRecovery));
    pdfa_->setChecked(settings.get(settings::kPdfaReadOnly));
}

void DocumentsPage::apply(Settings& settings) {
    settings.set(settings::kRestoreLastView, restoreView_->isChecked());
    settings.set(settings::kAutosave, autosave_->isChecked());
    settings.set(settings::kAutosaveMinutes, static_cast<int>(minutes_->value()));
    settings.set(settings::kOfferRecovery, recovery_->isChecked());
    settings.set(settings::kPdfaReadOnly, pdfa_->isChecked());
}

void DocumentsPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void DocumentsPage::retranslate() {
    const QStringList sections = {tr("Открытие"), tr("Автосохранение"), tr("Документы PDF/A")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    if (!rows_.isEmpty()) {
        rows_[0]->setText(tr("&Интервал:"));
    }
    minutes_->setAccessibleName(tr("Интервал автосохранения"));
    minutes_->setUnit(tr("мин"));
    restoreView_->setText(tr("&Восстанавливать последний вид при повторном открытии документов"));
    autosave_->setText(tr("&Автоматически сохранять изменения документов"));
    recovery_->setText(tr("&Предлагать восстановить несохранённые документы при запуске"));
    pdfa_->setText(tr("Открывать документы PDF/A в режиме &только для чтения"));
}

}  // namespace ot::ui
