#include "ui/shell/preferences/editing_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

#include "services/settings.h"
#include "services/system_fonts_dwrite.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

namespace {

QStringList familyList() {
    QStringList out;
    for (const std::string& f : ot::systemFonts().families()) {
        out << QString::fromStdString(f);
    }
    out.removeDuplicates();
    std::sort(out.begin(), out.end(),
              [](const QString& a, const QString& b) { return a.compare(b, Qt::CaseInsensitive) < 0; });
    return out;
}

// Выбрать семейство (нет в системе — добавить строкой: настройка остаётся как есть).
void selectFamily(QComboBox* box, const QString& family) {
    int index = box->findData(family, Qt::UserRole, Qt::MatchFixedString);
    if (index < 0) {
        box->addItem(family, family);
        index = box->count() - 1;
    }
    box->setCurrentIndex(index);
}

}  // namespace

EditingPage::EditingPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("editingPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    const QStringList families = familyList();
    font_ = new QComboBox();
    font_->setObjectName(QStringLiteral("editDefaultFontBox"));
    font_->setMaxVisibleItems(20);
    for (const QString& f : families) {
        font_->addItem(f, f);
    }
    size_ = new QSpinBox();
    size_->setObjectName(QStringLiteral("editDefaultSizeBox"));
    size_->setRange(settings::kEditDefaultFontSize.min, settings::kEditDefaultFontSize.max);
    fallback_ = new QComboBox();
    fallback_->setObjectName(QStringLiteral("editFallbackBox"));
    fallback_->setMaxVisibleItems(20);
    fallback_->addItem(QString(), QString());  // «Выбрать автоматически»
    for (const QString& f : families) {
        fallback_->addItem(f, f);
    }
    hint_ = new QLabel();
    hint_->setObjectName(QStringLiteral("editFallbackHint"));
    hint_->setWordWrap(true);
    system_ = new QCheckBox();
    system_->setObjectName(QStringLiteral("editSystemFontsBox"));

    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), font_);
    rows_ << sheet_->addRow(QString(), size_);
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), fallback_);
    sheet_->addWidget(hint_);
    sheet_->addWidget(system_);
    retranslate();
}

QString EditingPage::title() const { return tr("Редактирование содержимого"); }

void EditingPage::load(const Settings& settings) {
    selectFamily(font_, settings.get(settings::kEditDefaultFont));
    size_->setValue(settings.get(settings::kEditDefaultFontSize));
    const QString fallback = settings.get(settings::kEditFallbackFont);
    if (fallback.isEmpty()) {
        fallback_->setCurrentIndex(0);
    } else {
        selectFamily(fallback_, fallback);
    }
    system_->setChecked(settings.get(settings::kEditSystemFonts));
}

void EditingPage::apply(Settings& settings) {
    const QString font = font_->currentData().toString();
    settings.set(settings::kEditDefaultFont,
                 font.isEmpty() ? QString::fromLatin1(settings::kEditDefaultFont.def) : font);
    settings.set(settings::kEditDefaultFontSize, size_->value());
    settings.set(settings::kEditFallbackFont, fallback_->currentData().toString());
    settings.set(settings::kEditSystemFonts, system_->isChecked());
}

void EditingPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void EditingPage::retranslate() {
    const QStringList sections = {tr("Новый текст («Добавить текст»)"), tr("Шрифты")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Шрифт по умолчанию:"), tr("&Кегль, пт:"), tr("Шрифт &подстановки:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    fallback_->setItemText(0, tr("Выбрать автоматически"));
    hint_->setText(
        tr("Подстановка — когда шрифта документа нет ни в файле, ни в системе: «Выбрать "
           "автоматически» берёт Segoe UI или Times New Roman по засечкам исходного шрифта."));
    system_->setText(tr("Показывать &системные шрифты в списке шрифтов"));
}

}  // namespace ot::ui
