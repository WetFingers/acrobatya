// «Масштаб и точность» (план 5.5, п. 2; эталон 3.4 «Измерения», 6.3; comment-30): масштаб «[1] [см] на
// странице = [10] [м]» (единицы — мм, см, м, км, дюйм, фут, ярд, миля, пт), точность 1 … 0,0001; калибровка —
// отрезок, который пользователь провёл инструментом («Калибровать»), и его настоящая длина: масштаб считается
// сам (core/annot/measure.h calibrate). Масштаб документа (/VP) важнее масштаба пользователя, пока включена
// «Установки → Измерения (2D) → Использовать масштаб документа», — диалог об этом говорит. ScaleFields — те
// же поля для страницы Установок.
#pragma once

#include <QDialog>
#include <QWidget>

#include <optional>

#include "core/annot/measure.h"

class QComboBox;
class QLabel;
class QPushButton;

namespace ot::ui {

class NumberField;

// Поля масштаба и точности.
class ScaleFields : public QWidget {
    Q_OBJECT
public:
    explicit ScaleFields(QWidget* parent = nullptr);

    measure::Scale scale() const;
    void setScale(const measure::Scale& scale);

    NumberField* paperValue() const { return paperValue_; }
    QComboBox* paperUnit() const { return paperUnit_; }
    NumberField* realValue() const { return realValue_; }
    QComboBox* realUnit() const { return realUnit_; }
    QComboBox* precision() const { return precision_; }

    // Список единиц: подпись и данные — значение measure::Unit.
    static void fillUnits(QComboBox* box);
    static void selectUnit(QComboBox* box, measure::Unit unit);
    static measure::Unit unitOf(const QComboBox* box);

signals:
    void changed();

private:
    NumberField* paperValue_ = nullptr;
    QComboBox* paperUnit_ = nullptr;
    NumberField* realValue_ = nullptr;
    QComboBox* realUnit_ = nullptr;
    QComboBox* precision_ = nullptr;
};

class MeasureScaleDialog : public QDialog {
    Q_OBJECT
public:
    explicit MeasureScaleDialog(const measure::Scale& scale, QWidget* parent = nullptr);
    // Открыть окно-модально (удаляется при закрытии).
    static MeasureScaleDialog* openFor(const measure::Scale& scale, QWidget* parent);

    // Калибровка по отрезку lengthPt пунктов: поле «настоящая длина» и единица; масштаб — по ним.
    void setCalibration(double lengthPt);
    bool calibrating() const noexcept { return calibration_ > 0; }
    // Масштаб документа в точке измерения (/VP): заметка, что он важнее.
    void setDocumentScale(const std::optional<measure::Scale>& scale);

    measure::Scale scale() const { return fields_->scale(); }
    ScaleFields* fields() const { return fields_; }
    NumberField* knownLength() const { return known_; }
    QComboBox* knownUnit() const { return knownUnit_; }
    QLabel* segmentLabel() const { return segment_; }
    QLabel* documentNote() const { return documentNote_; }
    QPushButton* okButton() const { return ok_; }

signals:
    // «ОК»: выбранный масштаб.
    void scaleChosen(const ot::measure::Scale& scale);

private:
    void recalibrate();

    ScaleFields* fields_ = nullptr;
    QWidget* calibrationBox_ = nullptr;
    QLabel* segment_ = nullptr;
    NumberField* known_ = nullptr;
    QComboBox* knownUnit_ = nullptr;
    QLabel* documentNote_ = nullptr;
    QPushButton* ok_ = nullptr;
    double calibration_ = 0;
};

}  // namespace ot::ui
