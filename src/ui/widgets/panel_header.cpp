#include "ui/widgets/panel_header.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

PanelHeader::PanelHeader(const QString& title, QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground);
    auto* column = new QVBoxLayout(this);
    column->setSpacing(0);

    back_ = new ToolButton(QStringLiteral("chevron-left"), QString(), this);
    back_->setObjectName(QStringLiteral("panelHeaderBack"));
    back_->setIconSizeKind(ToolButton::IconSize::Row);
    back_->setLabelVisible(true);
    back_->hide();
    connect(back_, &QToolButton::clicked, this, &PanelHeader::backClicked);
    auto* backRow = new QHBoxLayout();
    backRow->addWidget(back_);
    backRow->addStretch(1);
    column->addLayout(backRow);

    auto* row = new QHBoxLayout();
    title_ = new QLabel(title, this);
    title_->setObjectName(QStringLiteral("panelHeaderTitle"));
    row->addWidget(title_, 1);
    actions_ = new QHBoxLayout();
    actions_->setSpacing(0);
    row->addLayout(actions_);
    column->addLayout(row);

    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &PanelHeader::applyMetrics);
}

void PanelHeader::setTitle(const QString& title) { title_->setText(title); }

QString PanelHeader::title() const { return title_->text(); }

void PanelHeader::setBackText(const QString& text) {
    back_->setLabel(text);
    back_->setLabelVisible(true);
    back_->setVisible(!text.isEmpty());
}

ToolButton* PanelHeader::addAction(const QString& iconName, const QString& toolTip) {
    auto* b = new ToolButton(iconName, toolTip, this);
    actions_->addWidget(b);
    return b;
}

void PanelHeader::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setContentsMargins(m.grid * 2, m.grid, m.grid, m.grid);
    title_->setFont(panelTitleFont(m));
    title_->setMinimumHeight(m.hitTarget);
}

}  // namespace ot::ui
