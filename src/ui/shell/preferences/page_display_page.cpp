#include "ui/shell/preferences/page_display_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

#include "services/settings.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

namespace {

// Значения ключей pageDisplay/defaultLayout и defaultZoom в порядке пунктов (подписи — retranslate).
const char* const kLayouts[] = {"auto", "single", "continuous", "facing", "facingContinuous"};
const char* const kZooms[] = {"auto", "fitPage", "fitWidth", "fitVisible", "actual", "50",
                              "75",   "100",     "125",      "150",        "200",    "400"};
constexpr int kNamedZooms = 5;                                  // дальше — проценты
const char* const kOverprints[] = {"always", "pdfx", "never"};  // pageDisplay/overprintPreview

}  // namespace

PageDisplayPage::PageDisplayPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("pageDisplayPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    layout_ = new QComboBox();
    layout_->setObjectName(QStringLiteral("defaultLayoutBox"));
    fillData(layout_, kLayouts);
    zoom_ = new QComboBox();
    zoom_->setObjectName(QStringLiteral("defaultZoomBox"));
    fillData(zoom_, kZooms);
    overprint_ = new QComboBox();
    overprint_->setObjectName(QStringLiteral("overprintBox"));
    fillData(overprint_, kOverprints);
    auto check = [](const char* name) {
        auto* box = new QCheckBox();
        box->setObjectName(QLatin1String(name));
        return box;
    };
    smoothText_ = check("smoothTextBox");
    smoothImages_ = check("smoothImagesBox");
    smoothLineArt_ = check("smoothLineArtBox");
    thinLines_ = check("thinLinesBox");
    logical_ = check("logicalNumbersBox");
    boxes_ = check("pageBoxesBox");

    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), layout_);
    rows_ << sheet_->addRow(QString(), zoom_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(smoothText_);
    sheet_->addWidget(smoothImages_);
    sheet_->addWidget(smoothLineArt_);
    sheet_->addWidget(thinLines_);
    rows_ << sheet_->addRow(QString(), overprint_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(logical_);
    sheet_->addWidget(boxes_);
    retranslate();
}

QString PageDisplayPage::title() const { return tr("Просмотр страницы"); }

void PageDisplayPage::load(const Settings& settings) {
    selectData(layout_, settings.get(settings::kDefaultLayout));
    selectData(zoom_, settings.get(settings::kDefaultZoom));
    smoothText_->setChecked(settings.get(settings::kSmoothText));
    smoothImages_->setChecked(settings.get(settings::kSmoothImages));
    smoothLineArt_->setChecked(settings.get(settings::kSmoothLineArt));
    thinLines_->setChecked(settings.get(settings::kEnhanceThinLines));
    selectData(overprint_, settings.get(settings::kOverprintPreview));
    logical_->setChecked(settings.get(settings::kUseLogicalPageNumbers));
    boxes_->setChecked(settings.get(settings::kShowPageBoxes));
}

void PageDisplayPage::apply(Settings& settings) {
    settings.set(settings::kDefaultLayout, layout_->currentData().toString());
    settings.set(settings::kDefaultZoom, zoom_->currentData().toString());
    settings.set(settings::kSmoothText, smoothText_->isChecked());
    settings.set(settings::kSmoothImages, smoothImages_->isChecked());
    settings.set(settings::kSmoothLineArt, smoothLineArt_->isChecked());
    settings.set(settings::kEnhanceThinLines, thinLines_->isChecked());
    settings.set(settings::kOverprintPreview, overprint_->currentData().toString());
    settings.set(settings::kUseLogicalPageNumbers, logical_->isChecked());
    settings.set(settings::kShowPageBoxes, boxes_->isChecked());
}

void PageDisplayPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void PageDisplayPage::retranslate() {
    const QStringList sections = {tr("Раскладка и масштаб по умолчанию"), tr("Отрисовка"),
                                  tr("Номера страниц и рамки")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Раскладка страниц:"), tr("&Масштаб:"), tr("Предпросмотр &наложения:")};
    const QList<QWidget*> editors = {layout_, zoom_, overprint_};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
        editors.value(i)->setAccessibleName(
            QString(rows.value(i)).remove(QLatin1Char('&')).remove(QLatin1Char(':')));
    }
    const QStringList layouts = {tr("Автоматически"), tr("Одна страница"), tr("Непрерывно"), tr("Разворот"),
                                 tr("Разворот непрерывно")};
    for (int i = 0; i < layout_->count(); ++i) {
        layout_->setItemText(i, layouts.value(i));
    }
    const QStringList zooms = {tr("Автоматически"), tr("Уместить страницу"), tr("По ширине"),
                               tr("По видимому"), tr("Реальный размер")};
    const QLocale locale;
    for (int i = 0; i < zoom_->count(); ++i) {
        zoom_->setItemText(i, i < kNamedZooms ? zooms.value(i)
                                              : locale.toString(zoom_->itemData(i).toString().toInt()) +
                                                    QStringLiteral(" %"));
    }
    const QStringList overprints = {tr("Всегда"), tr("Только для PDF/X"), tr("Никогда")};
    for (int i = 0; i < overprint_->count(); ++i) {
        overprint_->setItemText(i, overprints.value(i));
    }
    overprint_->setToolTip(tr("Показывать наложение красок (overprint), как при печати"));
    smoothText_->setText(tr("Сглаживание &текста"));
    smoothImages_->setText(tr("Сглаживание &изображений"));
    smoothLineArt_->setText(tr("Сглаживание &векторной графики"));
    thinLines_->setText(tr("&Утолщать тонкие линии"));
    logical_->setText(tr("Использовать &логические номера страниц"));
    boxes_->setText(tr("Показывать рамки &обрезки, подрезки и выпуска"));
}

}  // namespace ot::ui
