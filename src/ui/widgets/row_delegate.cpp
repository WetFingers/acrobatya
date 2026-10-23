#include "ui/widgets/row_delegate.h"

#include <QPainter>

#include "ui/theme/theme.h"

namespace ot::ui {

void RowDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                        const QModelIndex& index) const {
    const Metrics& m = Theme::sizes();
    const bool dimmed = index.data(kDimmedRole).toBool() || !(index.flags() & Qt::ItemIsEnabled);
    RowState state = RowState::Normal;
    if ((option.state & QStyle::State_Selected) != 0) {
        state = RowState::Selected;
    } else if ((option.state & QStyle::State_MouseOver) != 0 && !dimmed) {
        state = RowState::Hover;
    }
    const RowColors colors = rowColors(Theme::colors(), state);
    painter->save();
    if (colors.background.isValid() && colors.background.alpha() > 0) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.background);
        painter->drawRoundedRect(QRectF(option.rect).adjusted(m.grid / 2.0, 1, -m.grid / 2.0, -1), m.radius,
                                 m.radius);
    }
    const QRect text = option.rect.adjusted(m.grid * 3, 0, -m.grid * 3, 0);
    const QString detail = index.data(kDetailRole).toString();
    const int detailWidth = detail.isEmpty() ? 0 : option.fontMetrics.horizontalAdvance(detail) + m.grid * 4;
    painter->setFont(option.font);
    painter->setPen(dimmed ? rowColors(Theme::colors(), RowState::Disabled).text : colors.text);
    painter->drawText(text.adjusted(0, 0, -detailWidth, 0), Qt::AlignVCenter | Qt::AlignLeft,
                      option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight,
                                                    text.width() - detailWidth));
    if (!detail.isEmpty()) {
        painter->setPen(colors.detail);
        painter->drawText(text, Qt::AlignVCenter | Qt::AlignRight, detail);
    }
    if ((option.state & QStyle::State_HasFocus) != 0 &&
        (option.state & QStyle::State_KeyboardFocusChange) != 0) {
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(QPen(Theme::colors().accent.color(), m.focusRing));
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(QRectF(option.rect).adjusted(m.grid / 2.0 + 1, 2, -m.grid / 2.0 - 1, -2),
                                 m.radius, m.radius);
    }
    painter->restore();
}

QSize RowDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const {
    return QSize(QStyledItemDelegate::sizeHint(option, index).width(), Theme::sizes().row);
}

}  // namespace ot::ui
