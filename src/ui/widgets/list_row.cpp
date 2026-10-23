#include "ui/widgets/list_row.h"

#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

ListRow::ListRow(const QString& text, const QString& iconName, QWidget* parent)
    : QWidget(parent), text_(text), iconName_(iconName) {
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::StrongFocus);
    setAccessibleName(text);
    auto* row = new QHBoxLayout(this);
    row->addStretch(1);
    pin_ = new ToolButton(QStringLiteral("star"), tr("Закрепить наверху"), this);
    pin_->setIconSizeKind(ToolButton::IconSize::Row);
    pin_->setCheckable(true);
    pin_->hide();
    connect(pin_, &QToolButton::toggled, this, &ListRow::pinToggled);
    row->addWidget(pin_);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &ListRow::applyMetrics);
}

void ListRow::setText(const QString& text) {
    text_ = text;
    setAccessibleName(text);
    update();
}

void ListRow::setDetail(const QString& detail) {
    detail_ = detail;
    update();
}

void ListRow::setIconName(const QString& name) {
    iconName_ = name;
    update();
}

void ListRow::setSelected(bool selected) {
    selected_ = selected;
    update();
}

void ListRow::setPinnable(bool pinnable) { pin_->setVisible(pinnable); }

void ListRow::setPinned(bool pinned) { pin_->setChecked(pinned); }

bool ListRow::isPinned() const { return pin_->isChecked(); }

QSize ListRow::sizeHint() const {
    const Metrics& m = Theme::sizes();
    return {m.leftPanel - m.grid * 4, m.row};
}

QSize ListRow::minimumSizeHint() const {
    const Metrics& m = Theme::sizes();
    return {m.row * 3, m.row};
}

void ListRow::paintEvent(QPaintEvent*) {
    const Metrics& m = Theme::sizes();
    const Palette& p = Theme::colors();
    RowState state = RowState::Normal;
    if (!isEnabled()) {
        state = RowState::Disabled;
    } else if (selected_) {
        state = RowState::Selected;
    } else if (underMouse()) {
        state = RowState::Hover;
    }
    const RowColors c = rowColors(p, state);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF box = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    if (c.background.isValid()) {
        QPainterPath path;
        path.addRoundedRect(box, m.radius, m.radius);
        painter.fillPath(path, c.background);
    }
    if (hasFocus() || property("otFocus").toBool()) {
        const qreal inset = m.focusRing / 2.0;
        painter.setPen(QPen(p.accent.color(), m.focusRing));
        painter.drawRoundedRect(QRectF(rect()).adjusted(inset, inset, -inset, -inset), m.radius, m.radius);
    }
    int x = m.grid * 2;
    if (!iconName_.isEmpty()) {
        const int side = m.iconRow;
        const IconTone tone = state == RowState::Disabled ? IconTone::Disabled : IconTone::Text;
        painter.drawPixmap(x, (height() - side) / 2, Icons::get(iconName_, tone, side, devicePixelRatioF()));
        x += side + m.grid * 2;
    }
    int right = width() - m.grid * 2;
    if (pin_->isVisible()) {
        right = pin_->geometry().left() - m.grid;
    }
    const QFontMetrics fm(font());
    if (!detail_.isEmpty()) {
        const QString detail = fm.elidedText(detail_, Qt::ElideRight, (right - x) / 2);
        const int w = fm.horizontalAdvance(detail);
        painter.setPen(c.detail);
        painter.drawText(QRect(right - w, 0, w, height()), Qt::AlignVCenter | Qt::AlignRight, detail);
        right -= w + m.grid * 2;
    }
    painter.setPen(c.text);
    painter.drawText(QRect(x, 0, std::max(0, right - x), height()), Qt::AlignVCenter | Qt::AlignLeft,
                     fm.elidedText(text_, Qt::ElideRight, std::max(0, right - x)));
}

void ListRow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
        emit clicked();
    }
    QWidget::mouseReleaseEvent(event);
}

void ListRow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void ListRow::applyMetrics() {
    const Metrics& m = Theme::sizes();
    layout()->setContentsMargins(m.grid, 0, m.grid, 0);
    setMinimumHeight(m.row);
    setMaximumHeight(m.row);
    pin_->setCompact(m.row - m.grid);
    updateGeometry();
    update();
}

}  // namespace ot::ui
