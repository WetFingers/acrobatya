#include "ui/tools/print/print_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPrintDialog>
#include <QPrinter>
#include <QPrinterInfo>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/shell/document_view.h"
#include "ui/theme/theme.h"
#include "ui/tools/print/print_preview.h"
#include "ui/tools/print/print_runner.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/segmented_bar.h"

namespace ot::ui {

namespace {

const QString kMsPrintToPdf = QStringLiteral("Microsoft Print to PDF");
constexpr int kPerSheet[] = {2, 4, 6, 9, 16};
constexpr int kImageDpi[] = {150, 300, 600};

// Строка из нескольких полей без отступов (редактор строки PropertySheet).
QWidget* rowOf(std::initializer_list<QWidget*> widgets, int spacing, bool stretch = true) {
    auto* box = new QWidget();
    auto* row = new QHBoxLayout(box);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(spacing);
    for (QWidget* w : widgets) {
        row->addWidget(w);
    }
    if (stretch) {
        row->addStretch(1);
    }
    return box;
}

QWidget* columnOf(std::initializer_list<QWidget*> widgets, int spacing, int indent = 0) {
    auto* box = new QWidget();
    auto* column = new QVBoxLayout(box);
    column->setContentsMargins(indent, 0, 0, 0);
    column->setSpacing(spacing);
    for (QWidget* w : widgets) {
        column->addWidget(w);
    }
    return box;
}

void setItemEnabled(QComboBox* box, int index, bool enabled) {
    if (auto* model = qobject_cast<QStandardItemModel*>(box->model())) {
        if (QStandardItem* item = model->item(index)) {
            item->setEnabled(enabled);
        }
    }
}

}  // namespace

PrintDialog::PrintDialog(DocumentView& view, QList<int> pages, QWidget* parent)
    : QDialog(parent), view_(&view), session_(view.sessionPtr()), pages_(std::move(pages)) {
    setObjectName(QStringLiteral("printDialog"));
    // Заранее выбранные страницы (миниатюры, закладка) — «Выделенные» сразу; выделение миниатюр вкладки —
    // только если выделено больше одной (щелчок по миниатюре выделяет одну — это переход, а не выбор для
    // печати).
    bool preselect = !pages_.isEmpty();
    if (pages_.isEmpty()) {
        pages_ = view.selectedPages();
        preselect = pages_.size() > 1;
    }
    build();
    loadDefaults();
    if (preselect) {
        // Те же страницы и в поле «Страницы» («1-3, 7»; логическими номерами, если они включены — поле так и
        // разбирается) — удобно поправить диапазон.
        const std::vector<int> chosen(pages_.begin(), pages_.end());
        std::vector<std::string> labels;
        for (const QString& l : session_->summary().pageLabels) {
            labels.push_back(l.toStdString());
        }
        rangeEdit_->setText(QString::fromStdString(print::formatPageRange(chosen, labels, useLabels_)));
        selected_->setChecked(true);
    }
    retranslate();
    building_ = false;
    printerChanged();
}

PrintDialog::~PrintDialog() = default;

void PrintDialog::build() {
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* body = new QHBoxLayout();
    body->setSpacing(m.grid * 6);
    column->addLayout(body, 1);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("printOptions"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* form = new QWidget();
    auto* formColumn = new QVBoxLayout(form);
    formColumn->setContentsMargins(0, 0, m.grid * 2, 0);
    auto* sheet = new PropertySheet(form);
    formColumn->addWidget(sheet);
    formColumn->addStretch(1);
    scroll->setWidget(form);
    body->addWidget(scroll, 1);

    // Принтер, копии, серый, как изображение.
    printer_ = new QComboBox();
    printer_->setObjectName(QStringLiteral("printerBox"));
    printer_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    printer_->setMinimumContentsLength(24);
    properties_ = new QPushButton();
    properties_->setObjectName(QStringLiteral("printerProperties"));
    copies_ = new QSpinBox();
    copies_->setObjectName(QStringLiteral("copiesBox"));
    copies_->setRange(1, 999);
    collate_ = new QCheckBox();
    collate_->setObjectName(QStringLiteral("collateBox"));
    collate_->setChecked(true);
    gray_ = new QCheckBox();
    gray_->setObjectName(QStringLiteral("grayBox"));
    asImage_ = new QCheckBox();
    asImage_->setObjectName(QStringLiteral("asImageBox"));
    imageDpi_ = new QComboBox();
    imageDpi_->setObjectName(QStringLiteral("imageDpiBox"));
    for (const int dpi : kImageDpi) {
        imageDpi_->addItem(QString(), dpi);
    }
    imageDpi_->setCurrentIndex(1);
    noPrinters_ = new MessageBar(Tone::Warn);
    noPrinters_->setObjectName(QStringLiteral("noPrintersNote"));
    noPrinters_->setVisible(false);
    rightsNote_ = new MessageBar(Tone::Info);
    rightsNote_->setObjectName(QStringLiteral("lowResolutionNote"));
    rightsNote_->setVisible(false);
    sections_ << sheet->addSection(QString());
    printerLabel_ = sheet->addRow(QString(), rowOf({printer_, properties_}, m.grid * 2));
    printerLabel_->setBuddy(printer_);
    copiesLabel_ = sheet->addRow(QString(), rowOf({copies_, collate_}, m.grid * 4));
    copiesLabel_->setBuddy(copies_);
    sheet->addWidget(gray_);
    sheet->addWidget(rowOf({asImage_, imageDpi_}, m.grid * 2));
    sheet->addWidget(noPrinters_);
    sheet->addWidget(rightsNote_);

    // Страницы для печати.
    all_ = new QRadioButton();
    all_->setObjectName(QStringLiteral("pagesAll"));
    current_ = new QRadioButton();
    current_->setObjectName(QStringLiteral("pagesCurrent"));
    range_ = new QRadioButton();
    range_->setObjectName(QStringLiteral("pagesRange"));
    rangeEdit_ = new QLineEdit();
    rangeEdit_->setObjectName(QStringLiteral("pagesRangeEdit"));
    rangeEdit_->setClearButtonEnabled(true);
    selected_ = new QRadioButton();
    selected_->setObjectName(QStringLiteral("pagesSelected"));
    auto* pagesGroup = new QButtonGroup(this);
    for (QRadioButton* b : {all_, current_, range_, selected_}) {
        pagesGroup->addButton(b);
    }
    all_->setChecked(true);
    subset_ = new QComboBox();
    subset_->setObjectName(QStringLiteral("pagesSubset"));
    for (int i = 0; i < 3; ++i) {
        subset_->addItem(QString(), i);
    }
    reverse_ = new QCheckBox();
    reverse_->setObjectName(QStringLiteral("pagesReverse"));
    rangeError_ = new MessageBar(Tone::Danger);
    rangeError_->setObjectName(QStringLiteral("rangeError"));
    rangeError_->setVisible(false);
    sections_ << sheet->addSection(QString());
    sheet->addWidget(all_);
    sheet->addWidget(current_);
    sheet->addWidget(rowOf({range_, rangeEdit_}, m.grid * 2, false));
    sheet->addWidget(selected_);
    subsetLabel_ = sheet->addRow(QString(), subset_);
    subsetLabel_->setBuddy(subset_);
    sheet->addWidget(reverse_);
    sheet->addWidget(rangeError_);

    // Размер и обработка страниц.
    layoutBar_ = new SegmentedBar();
    layoutBar_->setObjectName(QStringLiteral("printLayoutBar"));
    for (int i = 0; i < 4; ++i) {
        layoutBar_->addSegment(QString());
    }
    layoutBar_->setCurrentIndex(0);
    stack_ = new QStackedWidget();
    stack_->setObjectName(QStringLiteral("printLayoutPages"));
    const auto labelFor = [this](QWidget* editor) {
        auto* label = new QLabel();
        label->setBuddy(editor);
        rowLabels_ << label;
        return label;
    };
    // Размер
    fit_ = new QRadioButton();
    fit_->setObjectName(QStringLiteral("sizeFit"));
    actual_ = new QRadioButton();
    actual_->setObjectName(QStringLiteral("sizeActual"));
    shrink_ = new QRadioButton();
    shrink_->setObjectName(QStringLiteral("sizeShrink"));
    customScale_ = new QRadioButton();
    customScale_->setObjectName(QStringLiteral("sizeCustom"));
    scale_ = new NumberField();
    scale_->setObjectName(QStringLiteral("sizeScale"));
    scale_->setDecimals(0);
    scale_->setRange(1, 1000);
    scale_->setValue(100);
    auto* sizeGroup = new QButtonGroup(this);
    for (QRadioButton* b : {fit_, actual_, shrink_, customScale_}) {
        sizeGroup->addButton(b);
    }
    fit_->setChecked(true);
    stack_->addWidget(columnOf({fit_, actual_, shrink_, rowOf({customScale_, scale_}, m.grid * 2)}, m.grid));
    // Постер
    tileScale_ = new NumberField();
    tileScale_->setObjectName(QStringLiteral("posterScale"));
    tileScale_->setDecimals(0);
    tileScale_->setRange(1, 1000);
    tileScale_->setValue(100);
    overlap_ = new NumberField();
    overlap_->setObjectName(QStringLiteral("posterOverlap"));
    overlap_->setDecimals(1);
    overlap_->setRange(0, 50);
    overlap_->setValue(10);
    cutMarks_ = new QCheckBox();
    cutMarks_->setObjectName(QStringLiteral("posterCutMarks"));
    cutMarks_->setChecked(true);
    labels_ = new QCheckBox();
    labels_->setObjectName(QStringLiteral("posterLabels"));
    largeOnly_ = new QCheckBox();
    largeOnly_->setObjectName(QStringLiteral("posterLargeOnly"));
    {
        auto* page = new QWidget();
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(m.grid * 2);
        grid->setVerticalSpacing(m.grid);
        grid->addWidget(labelFor(tileScale_), 0, 0);
        grid->addWidget(tileScale_, 0, 1);
        grid->addWidget(labelFor(overlap_), 1, 0);
        grid->addWidget(overlap_, 1, 1);
        grid->addWidget(cutMarks_, 2, 0, 1, 3);
        grid->addWidget(labels_, 3, 0, 1, 3);
        grid->addWidget(largeOnly_, 4, 0, 1, 3);
        grid->setColumnStretch(2, 1);
        stack_->addWidget(page);
    }
    // Несколько
    perSheet_ = new QComboBox();
    perSheet_->setObjectName(QStringLiteral("nupPerSheet"));
    for (const int n : kPerSheet) {
        perSheet_->addItem(QString::number(n), n);
    }
    perSheet_->setCurrentIndex(1);
    order_ = new QComboBox();
    order_->setObjectName(QStringLiteral("nupOrder"));
    for (int i = 0; i < 4; ++i) {
        order_->addItem(QString(), i);
    }
    borders_ = new QCheckBox();
    borders_->setObjectName(QStringLiteral("nupBorders"));
    nupRotate_ = new QCheckBox();
    nupRotate_->setObjectName(QStringLiteral("nupAutoRotate"));
    nupRotate_->setChecked(true);
    {
        auto* page = new QWidget();
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(m.grid * 2);
        grid->setVerticalSpacing(m.grid);
        grid->addWidget(labelFor(perSheet_), 0, 0);
        grid->addWidget(perSheet_, 0, 1);
        grid->addWidget(labelFor(order_), 1, 0);
        grid->addWidget(order_, 1, 1);
        grid->addWidget(borders_, 2, 0, 1, 3);
        grid->addWidget(nupRotate_, 3, 0, 1, 3);
        grid->setColumnStretch(2, 1);
        stack_->addWidget(page);
    }
    // Буклет
    bookletSubset_ = new QComboBox();
    bookletSubset_->setObjectName(QStringLiteral("bookletSubset"));
    for (int i = 0; i < 3; ++i) {
        bookletSubset_->addItem(QString(), i);
    }
    binding_ = new QComboBox();
    binding_->setObjectName(QStringLiteral("bookletBinding"));
    for (int i = 0; i < 2; ++i) {
        binding_->addItem(QString(), i);
    }
    bookletRotate_ = new QCheckBox();
    bookletRotate_->setObjectName(QStringLiteral("bookletAutoRotate"));
    bookletRotate_->setChecked(true);
    bookletHint_ = new MessageBar(Tone::Warn);
    bookletHint_->setObjectName(QStringLiteral("bookletHint"));
    bookletHint_->setVisible(false);
    {
        auto* page = new QWidget();
        auto* grid = new QGridLayout(page);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(m.grid * 2);
        grid->setVerticalSpacing(m.grid);
        grid->addWidget(labelFor(bookletSubset_), 0, 0);
        grid->addWidget(bookletSubset_, 0, 1);
        grid->addWidget(labelFor(binding_), 1, 0);
        grid->addWidget(binding_, 1, 1);
        grid->addWidget(bookletRotate_, 2, 0, 1, 3);
        grid->addWidget(bookletHint_, 3, 0, 1, 3);
        grid->setColumnStretch(2, 1);
        stack_->addWidget(page);
    }
    autoOrientation_ = new QRadioButton();
    autoOrientation_->setObjectName(QStringLiteral("orientationAuto"));
    portrait_ = new QRadioButton();
    portrait_->setObjectName(QStringLiteral("orientationPortrait"));
    landscape_ = new QRadioButton();
    landscape_->setObjectName(QStringLiteral("orientationLandscape"));
    auto* orientationGroup = new QButtonGroup(this);
    for (QRadioButton* b : {autoOrientation_, portrait_, landscape_}) {
        orientationGroup->addButton(b);
    }
    autoOrientation_->setChecked(true);
    sections_ << sheet->addSection(QString());
    sheet->addWidget(layoutBar_);
    sheet->addWidget(stack_);
    orientationLabel_ = sheet->addRow(QString(), columnOf({autoOrientation_, portrait_, landscape_}, m.grid));
    orientationLabel_->setBuddy(autoOrientation_);

    // Двусторонняя печать.
    duplex_ = new QCheckBox();
    duplex_->setObjectName(QStringLiteral("duplexBox"));
    longEdge_ = new QRadioButton();
    longEdge_->setObjectName(QStringLiteral("duplexLong"));
    shortEdge_ = new QRadioButton();
    shortEdge_->setObjectName(QStringLiteral("duplexShort"));
    auto* edgeGroup = new QButtonGroup(this);
    edgeGroup->addButton(longEdge_);
    edgeGroup->addButton(shortEdge_);
    longEdge_->setChecked(true);
    sheet->addWidget(duplex_);
    sheet->addWidget(columnOf({longEdge_, shortEdge_}, m.grid, m.grid * 6));

    // Комментарии и формы.
    content_ = new QComboBox();
    content_->setObjectName(QStringLiteral("printContent"));
    for (const auto c : {print::PrintContent::DocumentAndMarkups, print::PrintContent::Document,
                         print::PrintContent::FormFieldsOnly}) {
        content_->addItem(QString(), static_cast<int>(c));
    }
    summary_ = new QPushButton();
    summary_->setObjectName(QStringLiteral("commentsSummary"));
    summary_->setEnabled(false);  // сводка комментариев — 9.1
    sections_ << sheet->addSection(QString());
    contentLabel_ = sheet->addRow(QString(), rowOf({content_, summary_}, m.grid * 2));
    contentLabel_->setBuddy(content_);

    // Предпросмотр справа.
    auto* right = new QVBoxLayout();
    right->setSpacing(m.grid * 2);
    preview_ = new PrintPreview(this);
    preview_->setMinimumWidth(m.printPreview);
    right->addWidget(preview_, 1);
    info_ = new QLabel(this);
    info_->setObjectName(QStringLiteral("printInfo"));
    info_->setAlignment(Qt::AlignCenter);
    right->addWidget(info_);
    body->addLayout(right, 0);

    auto* buttons = new QDialogButtonBox(this);
    print_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    print_->setObjectName(QStringLiteral("printButton"));
    print_->setDefault(true);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    connect(print_, &QPushButton::clicked, this, [this] { print(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    resize(m.dialogWidth, m.dialogHeight + m.row * 2);

    // Любая правка — новый план и предпросмотр.
    const auto changed = [this] { refresh(); };
    connect(printer_, &QComboBox::currentIndexChanged, this, [this] { printerChanged(); });
    connect(properties_, &QPushButton::clicked, this, &PrintDialog::showPrinterProperties);
    connect(copies_, &QSpinBox::valueChanged, this, changed);
    for (QCheckBox* b : {collate_, gray_, asImage_, reverse_, cutMarks_, labels_, largeOnly_, borders_,
                         nupRotate_, bookletRotate_, duplex_}) {
        connect(b, &QCheckBox::toggled, this, changed);
    }
    for (QRadioButton* b : {all_, current_, range_, selected_, fit_, actual_, shrink_, customScale_,
                            autoOrientation_, portrait_, landscape_, longEdge_, shortEdge_}) {
        connect(b, &QRadioButton::toggled, this, [this](bool on) {
            if (on) {
                refresh();
            }
        });
    }
    for (QComboBox* b : {imageDpi_, subset_, perSheet_, order_, bookletSubset_, binding_, content_}) {
        connect(b, &QComboBox::currentIndexChanged, this, changed);
    }
    for (NumberField* f : {scale_, tileScale_, overlap_}) {
        connect(f, &NumberField::valueChanged, this, changed);
    }
    connect(rangeEdit_, &QLineEdit::textChanged, this, [this] {
        if (!range_->isChecked() && !rangeEdit_->text().isEmpty()) {
            range_->setChecked(true);  // ввод диапазона выбирает «Страницы»
        }
        refresh();
    });
    connect(layoutBar_, &SegmentedBar::currentChanged, this, [this](int index) {
        stack_->setCurrentIndex(index);
        if (index == 3 && !duplexSupported_ && bookletSubset_->currentIndex() == 0) {
            bookletSubset_->setCurrentIndex(1);  // без дуплекса — «Только лицевая сторона»
        }
        refresh();
    });
}

void PrintDialog::loadDefaults() {
    const Settings st;
    maxDpi_ = st.get(settings::kPrintQuality);
    useLabels_ = st.get(settings::kUseLogicalPageNumbers);
    const QStringList printers = PrintDevices::printers();
    printer_->addItems(printers);
    QString preferred = st.get(settings::kPrintPrinter);
    if (!printers.contains(preferred)) {
        preferred = PrintDevices::defaultPrinter();
    }
    printer_->setCurrentIndex(std::max<qsizetype>(0, printers.indexOf(preferred)));
    printer_->setEnabled(!printers.isEmpty());
    switch (printScaleFromName(st.get(settings::kPrintScale))) {
        case print::SizeMode::Actual:
            actual_->setChecked(true);
            break;
        case print::SizeMode::ShrinkOversized:
            shrink_->setChecked(true);
            break;
        default:
            fit_->setChecked(true);
            break;
    }
}

QString PrintDialog::printer() const { return printer_->currentText(); }

void PrintDialog::setPrinter(const QString& name) {
    const int index = printer_->findText(name);
    if (index >= 0) {
        printer_->setCurrentIndex(index);
    }
}

void PrintDialog::setLayoutMode(print::Layout layout) {
    const int index = layout == print::Layout::Poster     ? 1
                      : layout == print::Layout::Multiple ? 2
                      : layout == print::Layout::Booklet  ? 3
                                                          : 0;
    layoutBar_->setCurrentIndex(index);
}

void PrintDialog::printerChanged() {
    if (building_) {
        return;
    }
    qprinter_.reset();  // настроен «Свойствами…» для прежнего принтера
    const QString name = printer();
    paper_ = name.isEmpty() ? print::Paper::a4() : PrintDevices::paperFor(name);
    duplexSupported_ = !name.isEmpty() && PrintDevices::supportsDuplex(name);
    duplex_->setToolTip(duplexSupported_ ? QString() : tr("Принтер не печатает на обеих сторонах"));
    if (!duplexSupported_) {
        duplex_->setChecked(false);
        if (layoutBar_->currentIndex() == 3 && bookletSubset_->currentIndex() == 0) {
            bookletSubset_->setCurrentIndex(1);
        }
    }
    refresh();
}

void PrintDialog::showPrinterProperties() {
    const QString name = printer();
    if (name.isEmpty() || PrintDevices::overridden()) {
        return;
    }
    if (qprinter_ == nullptr || qprinter_->printerName() != name) {
        qprinter_ = std::make_unique<QPrinter>(QPrinterInfo::printerInfo(name), QPrinter::HighResolution);
    }
    qprinter_->setCopyCount(copies_->value());
    qprinter_->setCollateCopies(collate_->isChecked());
    QPrintDialog dialog(qprinter_.get(), this);
    dialog.setOptions(QAbstractPrintDialog::PrintCollateCopies | QAbstractPrintDialog::PrintShowPageSize);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    // Нативный диалог мог сменить принтер, копии, бумагу и дуплекс — поля диалога по нему.
    const QSignalBlocker block(printer_);
    const int index = printer_->findText(qprinter_->printerName());
    if (index >= 0) {
        printer_->setCurrentIndex(index);
    }
    copies_->setValue(qprinter_->copyCount());
    collate_->setChecked(qprinter_->collateCopies());
    duplex_->setChecked(qprinter_->duplex() != QPrinter::DuplexNone);
    shortEdge_->setChecked(qprinter_->duplex() == QPrinter::DuplexShortSide);
    paper_ = makePrinterDevice(*qprinter_)->paper();
    refresh();
}

PrintSettings PrintDialog::settings() const {
    PrintSettings s;
    s.title = view_ != nullptr ? view_->title() : QString();
    s.copies = copies_->value();
    s.collate = collate_->isChecked();
    s.grayscale = gray_->isChecked();
    s.asImage = asImage_->isChecked();
    s.imageDpi = imageDpi_->currentData().toInt();
    s.maxDpi = maxDpi_;
    // Страницы
    s.pages.range = current_->isChecked()    ? print::PageRange::Current
                    : range_->isChecked()    ? print::PageRange::Custom
                    : selected_->isChecked() ? print::PageRange::Selected
                                             : print::PageRange::All;
    s.pages.current = view_ != nullptr ? view_->view().currentPage() : 0;
    s.pages.custom = rangeEdit_->text().toStdString();
    s.pages.selected.assign(pages_.begin(), pages_.end());
    s.pages.parity = subset_->currentIndex() == 1   ? print::PageParity::Odd
                     : subset_->currentIndex() == 2 ? print::PageParity::Even
                                                    : print::PageParity::Both;
    s.pages.reverse = reverse_->isChecked();
    s.pages.useLabels = useLabels_;
    // Размер и обработка
    print::ImposeOptions& l = s.layout;
    switch (layoutBar_->currentIndex()) {
        case 1:
            l.layout = print::Layout::Poster;
            break;
        case 2:
            l.layout = print::Layout::Multiple;
            break;
        case 3:
            l.layout = print::Layout::Booklet;
            break;
        default:
            l.layout = print::Layout::Single;
            break;
    }
    l.orientation = portrait_->isChecked()    ? print::Orientation::Portrait
                    : landscape_->isChecked() ? print::Orientation::Landscape
                                              : print::Orientation::Auto;
    l.size = actual_->isChecked()        ? print::SizeMode::Actual
             : shrink_->isChecked()      ? print::SizeMode::ShrinkOversized
             : customScale_->isChecked() ? print::SizeMode::Custom
                                         : print::SizeMode::Fit;
    l.customScale = static_cast<float>(scale_->value() / 100.0);
    print::nupGrid(perSheet_->currentData().toInt(), l.cols, l.rows);
    l.order = static_cast<print::NupOrder>(order_->currentIndex());
    l.borders = borders_->isChecked();
    l.autoRotate = l.layout == print::Layout::Booklet ? bookletRotate_->isChecked() : nupRotate_->isChecked();
    l.bookletSubset = static_cast<print::BookletSubset>(bookletSubset_->currentIndex());
    l.binding = binding_->currentIndex() == 1 ? print::Binding::Right : print::Binding::Left;
    l.tileScale = static_cast<float>(tileScale_->value() / 100.0);
    l.overlap = static_cast<float>(overlap_->value()) * print::kPointsPerMm;
    l.cutMarks = cutMarks_->isChecked();
    l.labels = labels_->isChecked();
    l.tileLargeOnly = largeOnly_->isChecked();
    l.title = s.title.toStdString();
    s.content = static_cast<print::PrintContent>(content_->currentData().toInt());
    s.duplex = duplex_->isChecked() ? (shortEdge_->isChecked() ? DuplexMode::ShortSide : DuplexMode::LongSide)
                                    : DuplexMode::None;
    return s;
}

void PrintDialog::refresh() {
    if (building_ || view_ == nullptr) {
        return;
    }
    const PrintSettings s = settings();
    plan_ = makePrintPlan(session_->summary(), s, paper_);
    updateEnabled();
    switch (plan_.status) {
        case PrintStatus::Done:
            info_->setText(tr("Страниц: %1 · листов: %2").arg(plan_.pages.size()).arg(plan_.sheets.size()));
            break;
        case PrintStatus::Denied:
            info_->setText(tr("Документ защищён: печать запрещена"));
            break;
        default:
            info_->setText(tr("Нет страниц для печати"));
            break;
    }
    rangeError_->setVisible(plan_.status == PrintStatus::RangeError);
    preview_->setSource(session_, s, paper_, plan_);
}

void PrintDialog::updateEnabled() {
    const bool havePrinter = printer_->count() > 0 && !printer().isEmpty();
    const bool duplex = havePrinter && duplexSupported_;
    const bool booklet = layoutBar_->currentIndex() == 3;
    properties_->setEnabled(havePrinter && !PrintDevices::overridden());
    noPrinters_->setVisible(!havePrinter);
    collate_->setEnabled(copies_->value() > 1);
    imageDpi_->setEnabled(asImage_->isChecked());
    selected_->setEnabled(!pages_.isEmpty());
    scale_->setEnabled(customScale_->isChecked());
    duplex_->setEnabled(duplex && !booklet);
    longEdge_->setEnabled(duplex_->isEnabled() && duplex_->isChecked());
    shortEdge_->setEnabled(longEdge_->isEnabled());
    setItemEnabled(bookletSubset_, 0, duplex);
    bookletHint_->setVisible(booklet && !duplex);
    if (view_ != nullptr) {
        rightsNote_->setVisible(!view_->permissions().printHighRes);
    }
    print_->setEnabled(havePrinter && plan_.status == PrintStatus::Done);
}

QString PrintDialog::pdfOutputPath() {
    const Settings st;
    QString folder = st.get(settings::kPrintPdfFolder);
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    }
    const QString base = view_ != nullptr ? QFileInfo(view_->title()).completeBaseName() : QString();
    QString name = st.get(settings::kPrintPdfName);
    name.replace(QLatin1String("{name}"), base.isEmpty() ? tr("Документ") : base);
    if (!name.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)) {
        name += QStringLiteral(".pdf");
    }
    QString path = QDir(folder).filePath(name);
    // Не поверх существующего (и открытого документа): «имя (2).pdf», «имя (3).pdf»…
    const QString stem = QFileInfo(path).completeBaseName();
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = QDir(folder).filePath(QStringLiteral("%1 (%2).pdf").arg(stem).arg(n));
    }
    if (!st.get(settings::kPrintPdfAsk)) {
        return path;
    }
    return chooser_
               ? chooser_(this, path)
               : QFileDialog::getSaveFileName(this, tr("Печать в PDF"), path, tr("Документы PDF (*.pdf)"));
}

PrintRunner* PrintDialog::print() {
    refresh();
    if (!print_->isEnabled() || view_ == nullptr) {
        return nullptr;
    }
    const QString name = printer();
    QString output;
    if (name.compare(kMsPrintToPdf, Qt::CaseInsensitive) == 0) {
        output = pdfOutputPath();
        if (output.isEmpty()) {
            return nullptr;  // имя файла не выбрано — диалог остаётся
        }
        if (QFileInfo(output) == QFileInfo(view_->path())) {
            rangeError_->setText(
                tr("Нельзя печатать в PDF поверх открытого документа — выберите другое имя."));
            rangeError_->setVisible(true);
            return nullptr;
        }
    }
    std::unique_ptr<QPrinter> qprinter;
    std::unique_ptr<PrintDevice> device = PrintDevices::open(name, output);
    if (device == nullptr) {
        if (qprinter_ != nullptr && qprinter_->printerName() == name) {
            qprinter = std::move(qprinter_);
        } else {
            qprinter = std::make_unique<QPrinter>(QPrinterInfo::printerInfo(name), QPrinter::HighResolution);
        }
        if (!output.isEmpty()) {
            qprinter->setOutputFileName(output);
        }
        device = makePrinterDevice(*qprinter);
    }
    QWidget* host = parentWidget() != nullptr ? parentWidget()->window() : nullptr;
    auto* runner = new PrintRunner(session_, settings(), std::move(qprinter), std::move(device), host, host);
    emit printStarted(runner);
    runner->start();
    accept();
    return runner;
}

void PrintDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void PrintDialog::retranslate() {
    setWindowTitle(tr("Печать"));
    const QStringList sections = {tr("Принтер"), tr("Страницы для печати"), tr("Размер и обработка страниц"),
                                  tr("Комментарии и формы")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    printerLabel_->setText(tr("&Принтер:"));
    properties_->setText(tr("&Свойства…"));
    properties_->setToolTip(tr("Лотки, бумага и качество — в окне драйвера принтера"));
    copiesLabel_->setText(tr("&Копии:"));
    collate_->setText(tr("&Разобрать по копиям"));
    gray_->setText(tr("Печать в оттенках с&ерого (черно-белая)"));
    asImage_->setText(tr("Печать как &изображение:"));
    for (int i = 0; i < imageDpi_->count(); ++i) {
        imageDpi_->setItemText(i, tr("%1 точек на дюйм").arg(kImageDpi[i]));
    }
    noPrinters_->setText(tr("В Windows не установлен ни один принтер."));
    rightsNote_->setText(tr("Документ разрешает печать только с низким разрешением (150 точек на дюйм)."));
    all_->setText(tr("&Все"));
    current_->setText(tr("&Текущая страница"));
    range_->setText(tr("Стр&аницы:"));
    rangeEdit_->setPlaceholderText(tr("1-3, 7, 12-"));
    rangeEdit_->setAccessibleName(tr("Диапазон страниц"));
    selected_->setText(pages_.isEmpty() ? tr("В&ыделенные страницы")
                                        : tr("В&ыделенные страницы (%1)").arg(pages_.size()));
    subsetLabel_->setText(tr("Под&множество:"));
    subset_->setItemText(0, tr("Все страницы в диапазоне"));
    subset_->setItemText(1, tr("Только нечётные страницы"));
    subset_->setItemText(2, tr("Только чётные страницы"));
    reverse_->setText(tr("Обратный &порядок страниц"));
    rangeError_->setText(tr("Неверный диапазон страниц. Пример: 1-3, 7, 12-"));
    const QStringList layouts = {tr("Размер"), tr("Постер"), tr("Несколько"), tr("Буклет")};
    for (int i = 0; i < layoutBar_->count(); ++i) {
        layoutBar_->segment(i)->setText(layouts.value(i));
        layoutBar_->segment(i)->setAccessibleName(layouts.value(i));
    }
    fit_->setText(tr("По&догнать"));
    actual_->setText(tr("&Реальный размер"));
    shrink_->setText(tr("&Уменьшить страницы большого размера"));
    customScale_->setText(tr("&Масштаб:"));
    scale_->setUnit(tr("%"));
    scale_->setAccessibleName(tr("Масштаб"));
    const QStringList rows = {tr("Масштаб плиток:"),  tr("Перекрытие:"),           tr("Страниц на листе:"),
                              tr("Порядок страниц:"), tr("Подмножество буклета:"), tr("Переплёт:")};
    for (int i = 0; i < rowLabels_.size(); ++i) {
        rowLabels_[i]->setText(rows.value(i));
    }
    tileScale_->setUnit(tr("%"));
    overlap_->setUnit(tr("мм"));
    cutMarks_->setText(tr("Метки обреза"));
    labels_->setText(tr("Подписи (имя файла и номер плитки)"));
    largeOnly_->setText(tr("Разбивать только большие страницы"));
    order_->setItemText(0, tr("По горизонтали"));
    order_->setItemText(1, tr("По горизонтали в обратном порядке"));
    order_->setItemText(2, tr("По вертикали"));
    order_->setItemText(3, tr("По вертикали в обратном порядке"));
    borders_->setText(tr("Печатать границы страниц"));
    nupRotate_->setText(tr("Автоповорот страниц"));
    bookletSubset_->setItemText(0, tr("Обе стороны"));
    bookletSubset_->setItemText(1, tr("Только лицевая сторона"));
    bookletSubset_->setItemText(2, tr("Только оборотная сторона"));
    binding_->setItemText(0, tr("Слева"));
    binding_->setItemText(1, tr("Справа"));
    bookletRotate_->setText(tr("Автоповорот страниц на каждом листе"));
    bookletHint_->setText(
        tr("Принтер не печатает с двух сторон: напечатайте «Только лицевая сторона», "
           "переверните стопку и напечатайте «Только оборотная сторона»."));
    orientationLabel_->setText(tr("&Ориентация:"));
    autoOrientation_->setText(tr("Автоматически: книжная или альбомная"));
    portrait_->setText(tr("Книжная"));
    landscape_->setText(tr("Альбомная"));
    duplex_->setText(tr("Печать на &обеих сторонах бумаги"));
    longEdge_->setText(tr("Переворот по длинному краю"));
    shortEdge_->setText(tr("Переворот по короткому краю"));
    contentLabel_->setText(tr("Ко&мментарии и формы:"));
    content_->setItemText(0, tr("Документ и пометки"));
    content_->setItemText(1, tr("Документ"));
    content_->setItemText(2, tr("Только поля форм"));
    summary_->setText(tr("Сводка комментариев"));
    summary_->setToolTip(tr("Появится в версии 0.9"));
    print_->setText(tr("Печать"));
}

}  // namespace ot::ui
