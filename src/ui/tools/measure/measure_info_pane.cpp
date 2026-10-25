#include "ui/tools/measure/measure_info_pane.h"

#include <QFormLayout>

#include <cmath>

#include "ui/theme/theme.h"

namespace ot::ui {

MeasureInfoPanel::MeasureInfoPanel(QWidget* host) : ToolWindow(tr("Сведения об измерении"), host) {
    setObjectName(QStringLiteral("measureInfoPanel"));
    const Metrics& m = Theme::sizes();
    body_ = new QWidget(this);
    auto* form = new QFormLayout(body_);
    form->setContentsMargins(0, 0, 0, 0);
    form->setHorizontalSpacing(m.grid * 3);
    form->setVerticalSpacing(m.grid);
    kind_ = row(tr("Измерение:"), "measureInfoKind");
    value_ = row(tr("Значение:"), "measureInfoValue");
    dx_ = row(tr("ΔX:"), "measureInfoDx");
    dy_ = row(tr("ΔY:"), "measureInfoDy");
    angle_ = row(tr("Угол:"), "measureInfoAngle");
    scale_ = row(tr("Масштаб:"), "measureInfoScale");
    precision_ = row(tr("Точность:"), "measureInfoPrecision");
    value_->setFont(numberFont(m));
    addBody(body_);
    setReading({});
}

QLabel* MeasureInfoPanel::row(const QString& caption, const char* name) {
    auto* label = new QLabel(body_);
    label->setObjectName(QLatin1String(name));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* captionLabel = new QLabel(caption, body_);
    captionLabel->setObjectName(QStringLiteral("measureInfoCaption"));
    static_cast<QFormLayout*>(body_->layout())->addRow(captionLabel, label);
    return label;
}

void MeasureInfoPanel::setReading(const MeasureReading& r) {
    kind_->setText(MeasureMode::actionTitle(r.action));
    const bool area = r.action == MeasureAction::Area;
    const QString dash = QStringLiteral("—");
    value_->setText(r.active ? MeasureMode::formatValue(r.value, r.scale, area) : dash);
    dx_->setText(r.active ? MeasureMode::formatValue(r.dx, r.scale, false) : dash);
    dy_->setText(r.active ? MeasureMode::formatValue(r.dy, r.scale, false) : dash);
    angle_->setText(r.active ? tr("%1°").arg(MeasureMode::number(r.angle, 1)) : dash);
    const QString scale = MeasureMode::scaleText(r.scale);
    scale_->setText(r.documentScale ? tr("%1 (из документа)").arg(scale) : scale);
    precision_->setText(MeasureMode::number(std::pow(10.0, -r.scale.precision), r.scale.precision));
    place();
}

}  // namespace ot::ui
