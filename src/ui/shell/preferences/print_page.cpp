#include "ui/shell/preferences/print_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "services/print_job.h"
#include "services/settings.h"
#include "ui/theme/theme.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

namespace {

constexpr int kQuality[] = {150, 300, 600, 1200};
constexpr const char* kScales[] = {"fit", "actual", "shrink"};

}  // namespace

PrintPage::PrintPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("printPage"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    printer_ = new QComboBox();
    printer_->setObjectName(QStringLiteral("defaultPrinterBox"));
    printer_->addItem(QString(), QString());  // системный по умолчанию
    for (const QString& p : PrintDevices::printers()) {
        printer_->addItem(p, p);
    }
    quality_ = new QComboBox();
    quality_->setObjectName(QStringLiteral("printQualityBox"));
    for (const int dpi : kQuality) {
        quality_->addItem(QString(), dpi);
    }
    scale_ = new QComboBox();
    scale_->setObjectName(QStringLiteral("printScaleBox"));
    fillData(scale_, kScales);
    folder_ = new QLineEdit();
    folder_->setObjectName(QStringLiteral("pdfFolderEdit"));
    browse_ = new QPushButton();
    browse_->setObjectName(QStringLiteral("pdfFolderBrowse"));
    connect(browse_, &QPushButton::clicked, this, &PrintPage::browse);
    auto* folderRow = new QWidget();
    auto* row = new QHBoxLayout(folderRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(m.grid * 2);
    row->addWidget(folder_, 1);
    row->addWidget(browse_);
    name_ = new QLineEdit();
    name_->setObjectName(QStringLiteral("pdfNameEdit"));
    nameHint_ = new QLabel();
    nameHint_->setObjectName(QStringLiteral("pdfNameHint"));
    nameHint_->setWordWrap(true);
    ask_ = new QCheckBox();
    ask_->setObjectName(QStringLiteral("pdfAskBox"));

    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), printer_);
    rows_ << sheet_->addRow(QString(), quality_);
    rows_ << sheet_->addRow(QString(), scale_);
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), folderRow);
    rows_.back()->setBuddy(folder_);
    rows_ << sheet_->addRow(QString(), name_);
    sheet_->addWidget(nameHint_);
    sheet_->addWidget(ask_);
    retranslate();
}

QString PrintPage::title() const { return tr("Печать"); }

void PrintPage::load(const Settings& settings) {
    selectData(printer_, settings.get(settings::kPrintPrinter));
    const int quality = settings.get(settings::kPrintQuality);
    int best = 1;
    for (int i = 0; i < quality_->count(); ++i) {
        if (quality_->itemData(i).toInt() == quality) {
            best = i;
        }
    }
    quality_->setCurrentIndex(best);
    selectData(scale_, settings.get(settings::kPrintScale));
    folder_->setText(QDir::toNativeSeparators(settings.get(settings::kPrintPdfFolder)));
    name_->setText(settings.get(settings::kPrintPdfName));
    ask_->setChecked(settings.get(settings::kPrintPdfAsk));
}

void PrintPage::apply(Settings& settings) {
    settings.set(settings::kPrintPrinter, printer_->currentData().toString());
    settings.set(settings::kPrintQuality, quality_->currentData().toInt());
    settings.set(settings::kPrintScale, scale_->currentData().toString());
    settings.set(settings::kPrintPdfFolder, QDir::fromNativeSeparators(folder_->text().trimmed()));
    const QString name = name_->text().trimmed();
    settings.set(settings::kPrintPdfName,
                 name.isEmpty() ? QString::fromLatin1(settings::kPrintPdfName.def) : name);
    settings.set(settings::kPrintPdfAsk, ask_->isChecked());
}

void PrintPage::browse() {
    const QString current = QDir::fromNativeSeparators(folder_->text().trimmed());
    const QString dir = chooser_
                            ? chooser_(this, current)
                            : QFileDialog::getExistingDirectory(this, tr("Папка для печати в PDF"), current);
    if (!dir.isEmpty()) {
        folder_->setText(QDir::toNativeSeparators(dir));
    }
}

void PrintPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void PrintPage::retranslate() {
    const QStringList sections = {tr("Принтер"), tr("Печать в PDF (Microsoft Print to PDF)")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Принтер по умолчанию:"), tr("&Качество:"), tr("&Масштаб по умолчанию:"),
                              tr("&Папка:"), tr("&Имя файла:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    const QString system = PrintDevices::defaultPrinter();
    printer_->setItemText(
        0, system.isEmpty() ? tr("Системный по умолчанию") : tr("Системный по умолчанию (%1)").arg(system));
    for (int i = 0; i < quality_->count(); ++i) {
        quality_->setItemText(i, tr("%1 точек на дюйм").arg(quality_->itemData(i).toInt()));
    }
    scale_->setItemText(0, tr("Подогнать"));
    scale_->setItemText(1, tr("Реальный размер"));
    scale_->setItemText(2, tr("Уменьшить страницы большого размера"));
    folder_->setPlaceholderText(tr("Документы"));
    browse_->setText(tr("&Обзор…"));
    nameHint_->setText(
        tr("{name} — имя документа; такой файл уже есть — к имени добавится номер: «отчёт (2).pdf»."));
    ask_->setText(tr("&Спрашивать имя файла при печати в PDF"));
}

}  // namespace ot::ui
