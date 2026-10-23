#include "ui/shell/preferences/general_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "services/recent.h"
#include "services/settings.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

namespace {

// Значения ключей в порядке пунктов списков (подписи — retranslate).
const char* const kLanguages[] = {"system", "ru", "en"};
const char* const kThemes[] = {"system", "light", "dark", "contrast"};

}  // namespace

GeneralPage::GeneralPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("generalPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    language_ = new QComboBox();
    language_->setObjectName(QStringLiteral("languageBox"));
    fillData(language_, kLanguages);
    theme_ = new QComboBox();
    theme_->setObjectName(QStringLiteral("themeBox"));
    fillData(theme_, kThemes);
    large_ = new QCheckBox();
    large_->setObjectName(QStringLiteral("largeElementsBox"));
    startup_ = new QComboBox();
    startup_->setObjectName(QStringLiteral("startupBox"));
    startup_->addItem(QString(), false);  // Главная
    startup_->addItem(QString(), true);   // документы прошлого сеанса
    singleKey_ = new QCheckBox();
    singleKey_->setObjectName(QStringLiteral("singleKeyBox"));
    clearRecent_ = new QPushButton();
    clearRecent_->setObjectName(QStringLiteral("clearRecentButton"));
    connect(clearRecent_, &QPushButton::clicked, this, [this] {
        RecentFiles::shared().clear();
        clearRecent_->setEnabled(false);
    });

    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), language_);
    rows_ << sheet_->addRow(QString(), theme_);
    sheet_->addWidget(large_);
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), startup_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(singleKey_);
    sections_ << sheet_->addSection(QString());
    auto* recentRow = new QWidget();
    auto* recentLayout = new QHBoxLayout(recentRow);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->addWidget(clearRecent_);
    recentLayout->addStretch(1);
    sheet_->addWidget(recentRow);
    retranslate();
}

QString GeneralPage::title() const { return tr("Общие"); }

void GeneralPage::load(const Settings& settings) {
    selectData(language_, settings.get(settings::kLanguage));
    selectData(theme_, settings.get(settings::kTheme));
    large_->setChecked(settings.get(settings::kLargeElements));
    selectData(startup_, settings.get(settings::kRestoreSession));
    singleKey_->setChecked(settings.get(settings::kSingleKeyAccelerators));
    clearRecent_->setEnabled(!RecentFiles::shared().recent().empty());
}

void GeneralPage::apply(Settings& settings) {
    settings.set(settings::kLanguage, language_->currentData().toString());
    settings.set(settings::kTheme, theme_->currentData().toString());
    settings.set(settings::kLargeElements, large_->isChecked());
    settings.set(settings::kRestoreSession, startup_->currentData().toBool());
    settings.set(settings::kSingleKeyAccelerators, singleKey_->isChecked());
}

void GeneralPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void GeneralPage::retranslate() {
    const QStringList sections = {tr("Оформление"), tr("Запуск"), tr("Инструменты"), tr("Недавние файлы")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Язык интерфейса:"), tr("&Тема оформления:"),
                              tr("При &запуске открывать:")};
    const QList<QWidget*> editors = {language_, theme_, startup_};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
        editors.value(i)->setAccessibleName(
            QString(rows.value(i)).remove(QLatin1Char('&')).remove(QLatin1Char(':')));
    }
    language_->setItemText(0, tr("Как в системе"));
    //: Название языка — на нём самом (в английском переводе тоже «Русский»).
    language_->setItemText(1, tr("Русский"));
    language_->setItemText(2, QStringLiteral("English"));
    theme_->setItemText(0, tr("Системная"));
    theme_->setItemText(1, tr("Светлая"));
    theme_->setItemText(2, tr("Тёмная"));
    theme_->setItemText(3, tr("Высокая контрастность"));
    large_->setText(tr("&Крупные элементы управления"));
    startup_->setItemText(0, tr("Главную"));
    startup_->setItemText(1, tr("Документы прошлого сеанса"));
    singleKey_->setText(tr("Использовать &однобуквенные ускорители для выбора инструментов"));
    clearRecent_->setText(tr("О&чистить список недавних файлов"));
}

}  // namespace ot::ui
