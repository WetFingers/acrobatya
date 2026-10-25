#include "ui/tools/measure/measure_scale_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <cmath>

#include "ui/theme/theme.h"
#include "ui/tools/measure/measure_mode.h"
#include "ui/widgets/number_field.h"

namespace ot::ui {

namespace {

// Число масштаба без лишних нулей: «1», «2,54», «5,6449» (до 4 знаков).
class ScaleNumber : public NumberField {
public:
    using NumberField::NumberField;

protected:
    QString textFromValue(double value) const override {
        QString text = NumberField::textFromValue(value);
        const QString point = locale().decimalPoint();
        if (text.contains(point)) {
            while (text.endsWith(QLatin1Char('0'))) {
                text.chop(1);
            }
            if (text.endsWith(point)) {
                text.chop(point.size());
            }
        }
        return text;
    }
};

}  // namespace

// ---------------------------------------------------------------- ScaleFields

ScaleFields::ScaleFields(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("measureScaleFields"));
    const Metrics& m = Theme::sizes();
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(m.grid * 2);

    auto* row = new QHBoxLayout();
    row->setSpacing(m.grid * 2);
    paperValue_ = new ScaleNumber(this);
    paperValue_->setObjectName(QStringLiteral("measurePaperValue"));
    paperValue_->setRange(0.0001, 1e6);
    paperValue_->setDecimals(4);
    paperValue_->setAccessibleName(tr("Длина на странице"));
    paperUnit_ = new QComboBox(this);
    paperUnit_->setObjectName(QStringLiteral("measurePaperUnit"));
    paperUnit_->setAccessibleName(tr("Единица на странице"));
    fillUnits(paperUnit_);
    realValue_ = new ScaleNumber(this);
    realValue_->setObjectName(QStringLiteral("measureRealValue"));
    realValue_->setRange(0.0001, 1e9);
    realValue_->setDecimals(4);
    realValue_->setAccessibleName(tr("Длина в натуре"));
    realUnit_ = new QComboBox(this);
    realUnit_->setObjectName(QStringLiteral("measureRealUnit"));
    realUnit_->setAccessibleName(tr("Единица в натуре"));
    fillUnits(realUnit_);
    row->addWidget(paperValue_, 1);
    row->addWidget(paperUnit_);
    row->addWidget(new QLabel(QStringLiteral("="), this));
    row->addWidget(realValue_, 1);
    row->addWidget(realUnit_);
    auto* scaleLabel = new QLabel(tr("&Масштаб:"), this);
    scaleLabel->setBuddy(paperValue_);
    form->addRow(scaleLabel, row);

    precision_ = new QComboBox(this);
    precision_->setObjectName(QStringLiteral("measurePrecision"));
    for (int p = 0; p <= measure::kMaxPrecision; ++p) {
        precision_->addItem(MeasureMode::number(std::pow(10.0, -p), p), p);
    }
    auto* precisionLabel = new QLabel(tr("&Точность:"), this);
    precisionLabel->setBuddy(precision_);
    form->addRow(precisionLabel, precision_);

    for (QComboBox* box : {paperUnit_, realUnit_, precision_}) {
        connect(box, &QComboBox::currentIndexChanged, this, &ScaleFields::changed);
    }
    for (NumberField* field : {paperValue_, realValue_}) {
        connect(field, &NumberField::valueChanged, this, &ScaleFields::changed);
    }
    setScale(measure::Scale{});
}

void ScaleFields::fillUnits(QComboBox* box) {
    for (const measure::Unit u : measure::kUnits) {
        box->addItem(MeasureMode::unitLabel(u), static_cast<int>(u));
    }
}

void ScaleFields::selectUnit(QComboBox* box, measure::Unit unit) {
    const int index = box->findData(static_cast<int>(unit));
    box->setCurrentIndex(index >= 0 ? index : 0);
}

measure::Unit ScaleFields::unitOf(const QComboBox* box) {
    return static_cast<measure::Unit>(box->currentData().toInt());
}

measure::Scale ScaleFields::scale() const {
    measure::Scale s;
    s.paperValue = paperValue_->value();
    s.paperUnit = unitOf(paperUnit_);
    s.realValue = realValue_->value();
    s.realUnit = unitOf(realUnit_);
    s.precision = precision_->currentData().toInt();
    return s;
}

void ScaleFields::setScale(const measure::Scale& scale) {
    paperValue_->setValue(scale.paperValue);
    selectUnit(paperUnit_, scale.paperUnit);
    realValue_->setValue(scale.realValue);
    selectUnit(realUnit_, scale.realUnit);
    const int index = precision_->findData(scale.precision);
    precision_->setCurrentIndex(index >= 0 ? index : 2);
}

// ---------------------------------------------------------------- MeasureScaleDialog

MeasureScaleDialog::MeasureScaleDialog(const measure::Scale& scale, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("measureScaleDialog"));
    setWindowTitle(tr("Масштаб и точность"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);

    auto* intro = new QLabel(tr("Сколько в натуре составляет длина на странице. Новые измерения считаются "
                                "в этом масштабе; уже поставленные не меняются."),
                             this);
    intro->setObjectName(QStringLiteral("measureScaleIntro"));
    intro->setWordWrap(true);
    column->addWidget(intro);
    fields_ = new ScaleFields(this);
    fields_->setScale(scale);
    column->addWidget(fields_);

    calibrationBox_ = new QWidget(this);
    calibrationBox_->setObjectName(QStringLiteral("measureCalibration"));
    auto* calibration = new QFormLayout(calibrationBox_);
    calibration->setContentsMargins(0, 0, 0, 0);
    calibration->setSpacing(m.grid * 2);
    segment_ = new QLabel(calibrationBox_);
    segment_->setObjectName(QStringLiteral("measureCalibrationSegment"));
    calibration->addRow(tr("Отрезок на странице:"), segment_);
    auto* knownRow = new QHBoxLayout();
    knownRow->setSpacing(m.grid * 2);
    known_ = new ScaleNumber(calibrationBox_);
    known_->setObjectName(QStringLiteral("measureKnownLength"));
    known_->setRange(0.0001, 1e9);
    known_->setDecimals(4);
    known_->setValue(1);
    knownUnit_ = new QComboBox(calibrationBox_);
    knownUnit_->setObjectName(QStringLiteral("measureKnownUnit"));
    knownUnit_->setAccessibleName(tr("Единица настоящей длины"));
    ScaleFields::fillUnits(knownUnit_);
    ScaleFields::selectUnit(knownUnit_, scale.realUnit);
    knownRow->addWidget(known_, 1);
    knownRow->addWidget(knownUnit_);
    auto* knownLabel = new QLabel(tr("&Настоящая длина:"), calibrationBox_);
    knownLabel->setBuddy(known_);
    calibration->addRow(knownLabel, knownRow);
    calibrationBox_->setVisible(false);
    column->addWidget(calibrationBox_);
    connect(known_, &NumberField::valueChanged, this, [this] { recalibrate(); });
    connect(knownUnit_, &QComboBox::currentIndexChanged, this, [this] { recalibrate(); });

    documentNote_ = new QLabel(this);
    documentNote_->setObjectName(QStringLiteral("measureDocumentNote"));
    documentNote_->setWordWrap(true);
    documentNote_->setVisible(false);
    column->addWidget(documentNote_);
    column->addStretch(1);

    auto* buttons = new QDialogButtonBox(this);
    ok_ = buttons->addButton(QDialogButtonBox::Ok);
    ok_->setObjectName(QStringLiteral("measureScaleOk"));
    ok_->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const measure::Scale s = this->scale();
        if (!s.valid()) {
            return;
        }
        emit scaleChosen(s);
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
    setMinimumWidth(m.propsDialogWidth);
}

MeasureScaleDialog* MeasureScaleDialog::openFor(const measure::Scale& scale, QWidget* parent) {
    auto* dialog = new MeasureScaleDialog(scale, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    return dialog;
}

void MeasureScaleDialog::setCalibration(double lengthPt) {
    calibration_ = lengthPt;
    calibrationBox_->setVisible(lengthPt > 0);
    if (lengthPt <= 0) {
        return;
    }
    measure::Scale paper;
    paper.paperUnit = ScaleFields::unitOf(fields_->paperUnit());
    paper.realUnit = paper.paperUnit;
    paper.precision = 2;
    segment_->setText(
        MeasureMode::formatValue(lengthPt / measure::pointsPerUnit(paper.paperUnit), paper, false));
    // Настоящая длина по умолчанию — то, что даёт текущий масштаб.
    const measure::Scale current = fields_->scale();
    known_->setValue(lengthPt * current.unitsPerPoint());
    ScaleFields::selectUnit(knownUnit_, current.realUnit);
    recalibrate();
}

void MeasureScaleDialog::recalibrate() {
    if (calibration_ <= 0) {
        return;
    }
    const measure::Scale current = fields_->scale();
    try {
        const measure::Scale s =
            measure::calibrate(calibration_, known_->value(), ScaleFields::unitOf(knownUnit_),
                               current.precision, current.paperUnit);
        fields_->setScale(s);
    } catch (const std::invalid_argument&) {  // NOLINT(bugprone-empty-catch): нулевая длина — масштаб прежний
    }
}

void MeasureScaleDialog::setDocumentScale(const std::optional<measure::Scale>& scale) {
    documentNote_->setVisible(scale.has_value());
    if (scale) {
        documentNote_->setText(
            tr("В документе задан свой масштаб (%1) — измерения на этой странице идут в нём. "
               "Выключить: Установки → Измерения (2D).")
                .arg(MeasureMode::scaleText(*scale)));
    }
}

}  // namespace ot::ui
