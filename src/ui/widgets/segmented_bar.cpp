#include "ui/widgets/segmented_bar.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QToolButton>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

SegmentedBar::SegmentedBar(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground);
    layout_ = new QHBoxLayout(this);
    layout_->setSpacing(2);
    group_ = new QButtonGroup(this);
    group_->setExclusive(true);
    connect(group_, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        if (checked) {
            emit currentChanged(id);
        }
    });
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &SegmentedBar::applyMetrics);
}

int SegmentedBar::addSegment(const QString& text, const QString& iconName, const QString& tip) {
    auto* b = new QToolButton(this);
    b->setCheckable(true);
    b->setFocusPolicy(Qt::TabFocus);
    b->setText(text);
    b->setAccessibleName(text.isEmpty() ? tip : text);
    b->setToolTip(tip);
    if (!iconName.isEmpty()) {
        b->setIcon(Icons::icon(iconName));
        b->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    } else {
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    const int index = count();
    group_->addButton(b, index);
    layout_->addWidget(b);
    buttons_.push_back(b);
    icons_.push_back(iconName);
    if (index == 0) {
        b->setChecked(true);
    }
    applyMetrics();
    return index;
}

int SegmentedBar::currentIndex() const { return group_->checkedId(); }

void SegmentedBar::setCurrentIndex(int index) {
    if (QToolButton* b = segment(index)) {
        b->setChecked(true);
    }
}

void SegmentedBar::setSegmentEnabled(int index, bool enabled) {
    if (QToolButton* b = segment(index)) {
        b->setEnabled(enabled);
    }
}

QToolButton* SegmentedBar::segment(int index) const {
    return index >= 0 && index < count() ? buttons_[static_cast<size_t>(index)] : nullptr;
}

void SegmentedBar::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout_->setContentsMargins(2, 2, 2, 2);
    for (QToolButton* b : buttons_) {
        b->setIconSize(QSize(m.iconRow, m.iconRow));
        b->setMinimumHeight(m.row - m.grid * 2);
    }
    setFixedHeight(m.row);
}

}  // namespace ot::ui
