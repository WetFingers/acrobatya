#include "ui/widgets/tool_button.h"

#include "ui/theme/theme.h"

namespace ot::ui {

ToolButton::ToolButton(const QString& iconName, const QString& label, QWidget* parent) : QToolButton(parent) {
    setToolButtonStyle(Qt::ToolButtonIconOnly);
    setFocusPolicy(Qt::TabFocus);
    if (!iconName.isEmpty()) {
        setIconName(iconName);
    }
    setLabel(label);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &ToolButton::applyMetrics);
}

void ToolButton::setIconName(const QString& name, IconTone tone) {
    iconName_ = name;
    setIcon(Icons::icon(name, tone));
}

void ToolButton::setLabel(const QString& label) {
    label_ = label;
    setText(label);
    setToolTip(label);
    setAccessibleName(label);
}

void ToolButton::setLabelVisible(bool visible) {
    setToolButtonStyle(visible ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonIconOnly);
    setToolTip(visible ? QString() : label_);
}

void ToolButton::setIconSizeKind(IconSize kind) {
    sizeKind_ = kind;
    applyMetrics();
}

void ToolButton::setCompact(int side) {
    compactSide_ = side;
    applyMetrics();
}

void ToolButton::applyMetrics() {
    const Metrics& m = Theme::sizes();
    const int side = sizeKind_ == IconSize::Panel ? m.iconPanel : m.iconRow;
    setIconSize(QSize(side, side));
    if (compactSide_ > 0) {
        setFixedSize(compactSide_, compactSide_);
    } else {
        setMinimumSize(m.hitTarget, m.hitTarget);
    }
}

}  // namespace ot::ui
