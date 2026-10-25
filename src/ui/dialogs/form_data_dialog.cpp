#include "ui/dialogs/form_data_dialog.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

constexpr forms::DataFormat kFormats[] = {forms::DataFormat::Fdf, forms::DataFormat::Xfdf,
                                          forms::DataFormat::Xml, forms::DataFormat::Tsv};

// Список имён полей строкой: не больше 12, дальше «и ещё N».
QString names(const std::vector<std::string>& list) {
    QStringList out;
    for (std::size_t i = 0; i < list.size() && i < 12; ++i) {
        out << QString::fromStdString(list[i]);
    }
    QString text = out.join(QStringLiteral(", "));
    if (list.size() > 12) {
        text += QCoreApplication::translate("ot::ui::FormDataReportDialog", " и ещё %1")
                    .arg(static_cast<int>(list.size() - 12));
    }
    return text;
}

}  // namespace

// ---------------------------------------------------------------- экспорт

QString FormDataExportDialog::extension(forms::DataFormat format) {
    return QLatin1Char('.') + QLatin1String(forms::dataFormatName(format));
}

QString FormDataExportDialog::formatTitle(forms::DataFormat format) {
    switch (format) {
        case forms::DataFormat::Fdf:
            return tr("Acrobat FDF (*.fdf)");
        case forms::DataFormat::Xfdf:
            return tr("Acrobat XFDF (*.xfdf)");
        case forms::DataFormat::Xml:
            return tr("XML (*.xml)");
        case forms::DataFormat::Tsv:
            break;
    }
    return tr("Текст с разделителями табуляции (*.txt)");
}

FormDataExportDialog::FormDataExportDialog(const QString& documentPath, int values, QWidget* parent)
    : QDialog(parent), documentPath_(documentPath) {
    setObjectName(QStringLiteral("formDataExportDialog"));
    setWindowTitle(tr("Экспорт данных формы"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);

    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    auto* icon = new QLabel(this);
    icon->setPixmap(Icons::get(QStringLiteral("file-output"), IconTone::Accent, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    auto* intro =
        new QLabel(tr("Значения полей сохраняются в отдельный файл — его можно отправить или загрузить "
                      "в такую же форму. Полей с данными: %1.")
                       .arg(values),
                   this);
    intro->setObjectName(QStringLiteral("formDataSummary"));
    intro->setWordWrap(true);
    head->addWidget(intro, 1);
    column->addLayout(head);

    auto* formatLabel = new QLabel(tr("Формат:"), this);
    column->addWidget(formatLabel);
    formats_ = new QButtonGroup(this);
    for (const forms::DataFormat f : kFormats) {
        auto* radio = new QRadioButton(formatTitle(f), this);
        radio->setObjectName(QStringLiteral("formDataFormat-") + QLatin1String(forms::dataFormatName(f)));
        formats_->addButton(radio, static_cast<int>(f));
        column->addWidget(radio);
    }
    formats_->button(static_cast<int>(forms::DataFormat::Fdf))->setChecked(true);
    connect(formats_, &QButtonGroup::idToggled, this, [this](int, bool on) {
        if (on) {
            updatePathExtension();
        }
    });

    auto* fileRow = new QHBoxLayout();
    fileRow->setSpacing(m.grid * 2);
    auto* fileLabel = new QLabel(tr("&Файл:"), this);
    path_ = new QLineEdit(this);
    path_->setObjectName(QStringLiteral("formDataPath"));
    fileLabel->setBuddy(path_);
    auto* browse = new QPushButton(tr("&Обзор…"), this);
    browse->setObjectName(QStringLiteral("formDataBrowse"));
    connect(browse, &QPushButton::clicked, this, &FormDataExportDialog::browse);
    fileRow->addWidget(fileLabel);
    fileRow->addWidget(path_, 1);
    fileRow->addWidget(browse);
    column->addLayout(fileRow);

    const QFileInfo info(documentPath);
    const QString dir = documentPath.isEmpty() ? QDir::homePath() : info.absolutePath();
    const QString base = documentPath.isEmpty() ? tr("Данные формы") : info.completeBaseName();
    path_->setText(QDir::toNativeSeparators(QDir(dir).filePath(base + extension(forms::DataFormat::Fdf))));

    auto* buttons = new QDialogButtonBox(this);
    export_ = buttons->addButton(tr("&Экспорт"), QDialogButtonBox::AcceptRole);
    export_->setObjectName(QStringLiteral("formDataExport"));
    export_->setDefault(true);
    buttons->addButton(tr("Отмена"), QDialogButtonBox::RejectRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(path_, &QLineEdit::textChanged, this,
            [this](const QString& text) { export_->setEnabled(!text.trimmed().isEmpty()); });
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth / 2 + m.grid * 24);
}

forms::DataFormat FormDataExportDialog::format() const {
    return static_cast<forms::DataFormat>(formats_->checkedId());
}

void FormDataExportDialog::setFormat(forms::DataFormat format) {
    if (QAbstractButton* b = formats_->button(static_cast<int>(format))) {
        b->setChecked(true);
    }
}

QString FormDataExportDialog::path() const { return QDir::fromNativeSeparators(path_->text().trimmed()); }

void FormDataExportDialog::setPath(const QString& path) { path_->setText(QDir::toNativeSeparators(path)); }

void FormDataExportDialog::updatePathExtension() {
    QString p = path();
    if (p.isEmpty()) {
        return;
    }
    const QFileInfo info(p);
    if (forms::dataFormatFromExtension(info.suffix().toStdString())) {
        p = QDir(info.path()).filePath(info.completeBaseName() + extension(format()));
        setPath(p);
    }
}

void FormDataExportDialog::browse() {
    const QString suggested = path();
    QString chosen;
    if (chooser_) {
        chosen = chooser_(suggested, format());
    } else {
        chosen =
            QFileDialog::getSaveFileName(this, tr("Экспорт данных формы"), suggested, formatTitle(format()));
    }
    if (!chosen.isEmpty()) {
        setPath(chosen);
    }
}

// ---------------------------------------------------------------- отчёт импорта

FormDataReportDialog::FormDataReportDialog(const QString& file, const forms::DataImportReport& report,
                                           QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("formDataReportDialog"));
    setWindowTitle(tr("Импорт данных формы"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* head = new QHBoxLayout();
    head->setSpacing(m.grid * 3);
    const bool clean = report.unknown.empty() && report.readOnly.empty() && report.rejected.empty();
    auto* icon = new QLabel(this);
    icon->setPixmap(
        Icons::get(QStringLiteral("file-input"), clean ? IconTone::Ok : IconTone::Warn, m.iconEmpty));
    icon->setAlignment(Qt::AlignTop);
    head->addWidget(icon);
    summary_ = new QLabel(tr("Из «%1» принято значений: %2 из %3.")
                              .arg(QFileInfo(file).fileName())
                              .arg(static_cast<int>(report.updated.size()))
                              .arg(report.total()),
                          this);
    summary_->setObjectName(QStringLiteral("formDataReportSummary"));
    summary_->setWordWrap(true);
    head->addWidget(summary_, 1);
    column->addLayout(head);
    const auto section = [&](const QString& title, const std::vector<std::string>& list, const char* name) {
        if (list.empty()) {
            return;
        }
        auto* label = new QLabel(
            QStringLiteral("<b>%1</b> %2").arg(title.toHtmlEscaped(), names(list).toHtmlEscaped()), this);
        label->setObjectName(QLatin1String(name));
        label->setTextFormat(Qt::RichText);
        label->setWordWrap(true);
        column->addWidget(label);
    };
    section(tr("Нет в этой форме:"), report.unknown, "formDataUnknown");
    section(tr("Только для чтения:"), report.readOnly, "formDataReadOnly");
    section(tr("Не приняты:"), report.rejected, "formDataRejected");
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("ОК"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    column->addWidget(buttons);
    setMinimumWidth(m.dialogWidth / 2 + m.grid * 24);
}

bool confirmFormDataImport(QWidget* parent, const QString& file, int values) {
    QMessageBox box(parent);
    box.setObjectName(QStringLiteral("formDataImportConfirm"));
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QCoreApplication::translate("ot::ui::FormDataExportDialog", "Импорт данных формы"));
    box.setText(QCoreApplication::translate("ot::ui::FormDataExportDialog",
                                            "Импортировать данные формы из «%1»? Значений в файле: %2. Поля "
                                            "документа получат значения из файла (отменяется одним шагом).")
                    .arg(QFileInfo(file).fileName())
                    .arg(values));
    QPushButton* ok =
        box.addButton(QCoreApplication::translate("ot::ui::FormDataExportDialog", "&Импортировать"),
                      QMessageBox::AcceptRole);
    box.addButton(QCoreApplication::translate("ot::ui::FormDataExportDialog", "Отмена"),
                  QMessageBox::RejectRole);
    box.setDefaultButton(ok);
    box.exec();
    return box.clickedButton() == ok;
}

}  // namespace ot::ui
