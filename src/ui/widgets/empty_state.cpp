#include "ui/widgets/empty_state.h"

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

EmptyState::EmptyState(const QString& iconName, const QString& text, const QString& actionText,
                       QWidget* parent)
    : QWidget(parent), iconName_(iconName) {
    auto* column = new QVBoxLayout(this);
    column->addStretch(1);
    icon_ = new QLabel(this);
    icon_->setAlignment(Qt::AlignCenter);
    text_ = new QLabel(text, this);
    text_->setObjectName(QStringLiteral("emptyStateText"));
    text_->setAlignment(Qt::AlignCenter);
    text_->setWordWrap(true);
    action_ = new QPushButton(actionText, this);
    action_->setVisible(!actionText.isEmpty());
    connect(action_, &QPushButton::clicked, this, &EmptyState::actionTriggered);
    column->addWidget(icon_);
    column->addWidget(text_);
    column->addWidget(action_, 0, Qt::AlignHCenter);
    column->addStretch(1);
    setAccessibleName(text);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &EmptyState::applyMetrics);
}

void EmptyState::setIconName(const QString& name) {
    iconName_ = name;
    applyMetrics();
}

void EmptyState::setText(const QString& text) {
    text_->setText(text);
    setAccessibleName(text);
}

QString EmptyState::text() const { return text_->text(); }

void EmptyState::setActionText(const QString& text) {
    action_->setText(text);
    action_->setVisible(!text.isEmpty());
}

void EmptyState::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setSpacing(m.grid * 2);
    layout()->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    icon_->setVisible(!iconName_.isEmpty());
    if (!iconName_.isEmpty()) {
        icon_->setPixmap(Icons::get(iconName_, IconTone::Muted, m.iconEmpty, devicePixelRatioF()));
    }
}

}  // namespace ot::ui
