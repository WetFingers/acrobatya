#include "ui/widgets/message_bar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

MessageBar::MessageBar(Tone tone, const QString& text, QWidget* parent) : QFrame(parent), tone_(tone) {
    auto* row = new QHBoxLayout(this);
    icon_ = new QLabel(this);
    icon_->setObjectName(QStringLiteral("messageBarIcon"));
    text_ = new QLabel(text, this);
    text_->setObjectName(QStringLiteral("messageBarText"));
    text_->setWordWrap(true);
    text_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    actions_ = new QHBoxLayout();
    close_ = new ToolButton(QStringLiteral("x"), tr("Закрыть"), this);
    close_->setIconSizeKind(ToolButton::IconSize::Row);
    connect(close_, &QToolButton::clicked, this, [this] {
        hide();
        emit closed();
    });
    row->addWidget(icon_, 0, Qt::AlignVCenter);
    row->addWidget(text_, 1);
    row->addLayout(actions_);
    row->addWidget(close_, 0, Qt::AlignVCenter);
    setAccessibleName(text);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &MessageBar::applyMetrics);
}

void MessageBar::setTone(Tone tone) {
    tone_ = tone;
    applyMetrics();
    repolish(this);
}

void MessageBar::setText(const QString& text) {
    text_->setText(text);
    setAccessibleName(text);
}

QString MessageBar::text() const { return text_->text(); }

QPushButton* MessageBar::addAction(const QString& text, const QString& id, bool primary) {
    auto* b = new QPushButton(text, this);
    b->setProperty("primary", primary);
    connect(b, &QPushButton::clicked, this, [this, id] { emit actionTriggered(id); });
    actions_->addWidget(b);
    return b;
}

void MessageBar::setClosable(bool closable) { close_->setVisible(closable); }

void MessageBar::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setContentsMargins(m.grid * 3, m.grid, m.grid, m.grid);
    layout()->setSpacing(m.grid * 2);
    actions_->setSpacing(m.grid * 2);
    setMinimumHeight(m.row + m.grid * 2);
    icon_->setPixmap(Icons::get(toneIconName(tone_), toneIcon(tone_), m.iconPanel, devicePixelRatioF()));
}

}  // namespace ot::ui
