// Числовое поле (кегль, толщина линии, поля страницы): единица после числа через неразрывный пробел, дробная
// часть с запятой или точкой, значение меняется по Enter/уходу фокуса/клавишам ↑↓ (keyboardTracking
// выключен), колесо — только в фокусе; кнопок-стрелок нет.
#pragma once

#include <QDoubleSpinBox>

namespace ot::ui {

class NumberField : public QDoubleSpinBox {
    Q_OBJECT
public:
    explicit NumberField(QWidget* parent = nullptr);

    void setUnit(const QString& unit);  // «пт», «%», «мм»; пусто — без единицы
    QString unit() const { return unit_; }

protected:
    double valueFromText(const QString& text) const override;
    QValidator::State validate(QString& input, int& pos) const override;
    void wheelEvent(QWheelEvent* event) override;

private:
    QString normalized(QString text) const;
    void applyMetrics();

    QString unit_;
};

}  // namespace ot::ui
