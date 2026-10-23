#include "ui/widgets/number_field.h"

#include <QLineEdit>
#include <QWheelEvent>

#include "ui/theme/theme.h"

namespace ot::ui {

NumberField::NumberField(QWidget* parent) : QDoubleSpinBox(parent) {
    setKeyboardTracking(false);
    setFocusPolicy(Qt::StrongFocus);
    setAccelerated(true);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &NumberField::applyMetrics);
}

void NumberField::setUnit(const QString& unit) {
    unit_ = unit;
    setSuffix(unit.isEmpty() ? QString() : QChar(0x00A0) + unit);
}

QString NumberField::normalized(QString text) const {
    // Запятая и точка — обе как десятичный разделитель локали.
    const QString point = locale().decimalPoint();
    text.replace(QLatin1Char(point == QLatin1String(",") ? '.' : ','), point);
    return text;
}

double NumberField::valueFromText(const QString& text) const {
    return QDoubleSpinBox::valueFromText(normalized(text));
}

QValidator::State NumberField::validate(QString& input, int& pos) const {
    input = normalized(input);
    return QDoubleSpinBox::validate(input, pos);
}

void NumberField::wheelEvent(QWheelEvent* event) {
    if (!hasFocus()) {
        event->ignore();
        return;
    }
    QDoubleSpinBox::wheelEvent(event);
}

void NumberField::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setMinimumHeight(m.row - m.grid * 2);
    lineEdit()->setTextMargins(m.grid, 0, m.grid, 0);  // QSS-отступ спинбокса до поля ввода не доходит
}

}  // namespace ot::ui
