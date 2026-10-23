#include "ui/widgets/chip.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>

#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

Chip::Chip(const QString& text, Tone tone, QWidget* parent) : QFrame(parent), tone_(tone) {
    setAttribute(Qt::WA_Hover);
    auto* row = new QHBoxLayout(this);
    icon_ = new QLabel(this);
    icon_->hide();
    label_ = new QLabel(text, this);
    label_->setObjectName(QStringLiteral("chipText"));
    close_ = new ToolButton(QStringLiteral("x"), tr("Убрать"), this);
    close_->setObjectName(QStringLiteral("chipClose"));
    close_->setIconSizeKind(ToolButton::IconSize::Row);
    close_->hide();
    connect(close_, &QToolButton::clicked, this, &Chip::closeRequested);
    row->addWidget(icon_);
    row->addWidget(label_);
    row->addWidget(close_);
    setAccessibleName(text);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &Chip::applyMetrics);
}

void Chip::setText(const QString& text) {
    label_->setText(text);
    setAccessibleName(text);
}

QString Chip::text() const { return label_->text(); }

void Chip::setTone(Tone tone) {
    tone_ = tone;
    applyMetrics();
    repolish(this);
}

void Chip::setIconName(const QString& name) {
    iconName_ = name;
    icon_->setVisible(!name.isEmpty());
    applyMetrics();
}

void Chip::setClosable(bool closable) { close_->setVisible(closable); }

void Chip::setCheckable(bool checkable) {
    checkable_ = checkable;
    setFocusPolicy(checkable ? Qt::StrongFocus : Qt::NoFocus);
    setCursor(checkable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    repolish(this);
}

void Chip::setChecked(bool checked) {
    if (!checkable_ || checked_ == checked) {
        return;
    }
    checked_ = checked;
    repolish(this);
    emit toggled(checked_);
}

void Chip::activate() {
    emit clicked();
    if (checkable_) {
        setChecked(!checked_);
    }
}

void Chip::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        activate();
    }
    QFrame::mouseReleaseEvent(event);
}

void Chip::keyPressEvent(QKeyEvent* event) {
    if (checkable_ &&
        (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        activate();
        event->accept();
        return;
    }
    QFrame::keyPressEvent(event);
}

void Chip::applyMetrics() {
    const Metrics& m = Theme::sizes();
    const int h = m.row - m.grid * 2;
    setFixedHeight(h);
    layout()->setContentsMargins(m.grid * 2, 0, close_->isVisibleTo(this) ? m.grid / 2 : m.grid * 2, 0);
    layout()->setSpacing(m.grid);
    close_->setCompact(h - m.grid);
    if (!iconName_.isEmpty()) {
        icon_->setPixmap(Icons::get(iconName_, toneIcon(tone_), m.iconRow, devicePixelRatioF()));
    }
}

}  // namespace ot::ui
