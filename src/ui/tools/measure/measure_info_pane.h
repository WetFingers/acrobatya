// «Сведения об измерении» (план 5.5, п. 2; эталон 3.4 «Измерения» — панель Measurement Info, 6.3): плавающее
// окно в правом верхнем углу вида (ToolWindow, как «Лупа»): что меряем, значение с единицей, Δx и Δy
// последнего отрезка, угол, масштаб («1 см = 10 м», «из документа» — если масштаб задан /VP) и точность.
// Значения — из MeasureMode::reading(); «Закрыть» — скрыть окно до следующего включения настройкой.
#pragma once

#include <QLabel>

#include "ui/tools/measure/measure_mode.h"
#include "ui/view/tools/tool_panels.h"

namespace ot::ui {

class MeasureInfoPanel : public ToolWindow {
    Q_OBJECT
public:
    explicit MeasureInfoPanel(QWidget* host);

    void setReading(const MeasureReading& reading);

    QLabel* kindLabel() const { return kind_; }
    QLabel* valueLabel() const { return value_; }
    QLabel* dxLabel() const { return dx_; }
    QLabel* dyLabel() const { return dy_; }
    QLabel* angleLabel() const { return angle_; }
    QLabel* scaleLabel() const { return scale_; }
    QLabel* precisionLabel() const { return precision_; }

private:
    QLabel* row(const QString& caption, const char* name);

    QWidget* body_ = nullptr;
    QLabel* kind_ = nullptr;
    QLabel* value_ = nullptr;
    QLabel* dx_ = nullptr;
    QLabel* dy_ = nullptr;
    QLabel* angle_ = nullptr;
    QLabel* scale_ = nullptr;
    QLabel* precision_ = nullptr;
};

}  // namespace ot::ui
