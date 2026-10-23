#include "ui/widgets/card.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QVBoxLayout>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

Card::Card(const QString& title, const QString& text, const QString& iconName, QWidget* parent)
    : QFrame(parent) {
    auto* row = new QHBoxLayout(this);
    icon_ = new QLabel(this);
    icon_->setObjectName(QStringLiteral("cardIcon"));
    icon_->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
    row->addWidget(icon_, 0, Qt::AlignTop);
    auto* column = new QVBoxLayout();
    column->setSpacing(2);
    title_ = new QLabel(title, this);
    title_->setObjectName(QStringLiteral("cardTitle"));
    title_->setWordWrap(true);
    QFont bold = title_->font();
    bold.setWeight(QFont::DemiBold);
    title_->setFont(bold);
    text_ = new QLabel(text, this);
    text_->setObjectName(QStringLiteral("cardText"));
    text_->setWordWrap(true);
    content_ = new QVBoxLayout();
    column->addWidget(title_);
    column->addWidget(text_);
    column->addLayout(content_);
    column->addStretch(1);
    row->addLayout(column, 1);
    setIconName(iconName);
    title_->setVisible(!title.isEmpty());
    text_->setVisible(!text.isEmpty());
    setAccessibleName(title);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &Card::applyMetrics);
}

void Card::setTitle(const QString& title) {
    title_->setText(title);
    title_->setVisible(!title.isEmpty());
    setAccessibleName(title);
}

void Card::setText(const QString& text) {
    text_->setText(text);
    text_->setVisible(!text.isEmpty());
}

void Card::setIconName(const QString& name) {
    iconName_ = name;
    icon_->setVisible(!name.isEmpty());
    applyMetrics();
}

void Card::setClickable(bool clickable) {
    clickable_ = clickable;
    setAttribute(Qt::WA_Hover, clickable);
    setFocusPolicy(clickable ? Qt::StrongFocus : Qt::NoFocus);
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    repolish(this);
}

void Card::setSelected(bool selected) {
    selected_ = selected;
    repolish(this);
}

void Card::mouseReleaseEvent(QMouseEvent* event) {
    if (clickable_ && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
    QFrame::mouseReleaseEvent(event);
}

void Card::keyPressEvent(QKeyEvent* event) {
    if (clickable_ &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        emit clicked();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void Card::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    static_cast<QHBoxLayout*>(layout())->setSpacing(m.grid * 3);
    if (!iconName_.isEmpty()) {
        icon_->setPixmap(Icons::get(iconName_, IconTone::Text, m.iconPanel, devicePixelRatioF()));
    }
}

}  // namespace ot::ui
