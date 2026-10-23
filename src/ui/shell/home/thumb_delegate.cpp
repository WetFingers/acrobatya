#include "ui/shell/home/thumb_delegate.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>

#include "services/thumbnails.h"
#include "ui/shell/home/recent_model.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

ThumbDelegate::ThumbDelegate(ThumbnailCache& thumbs, QObject* parent)
    : QStyledItemDelegate(parent), thumbs_(thumbs) {}

QImage ThumbDelegate::thumbnail(const QModelIndex& index) const {
    if (index.data(home::IsDirRole).toBool()) {
        return {};
    }
    return thumbs_.get(index.data(home::PathRole).toString());  // ставит рендер, если миниатюры ещё нет
}

QSize ThumbDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& /*index*/) const {
    const Metrics& m = Theme::sizes();
    if (mode_ == Mode::Grid) {
        return {m.cardWidth, m.cardHeight};
    }
    return {std::max(option.rect.width(), m.cardWidth), m.row + m.grid * 4};
}

void ThumbDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    if (mode_ == Mode::Grid) {
        paintGrid(painter, option, index);
    } else {
        paintList(painter, option, index);
    }
    painter->restore();
}

namespace {

// Картинка в рамке по центру области с сохранением пропорций; без картинки — значок.
void drawThumb(QPainter* p, const QRectF& area, const QImage& image, bool isDir, int iconSide) {
    const Palette& pal = Theme::colors();
    if (image.isNull()) {
        const QPixmap icon = Icons::get(isDir ? QStringLiteral("folder") : QStringLiteral("file-text"),
                                        IconTone::Muted, iconSide);
        const QSizeF s = QSizeF(icon.size()) / icon.devicePixelRatio();
        p->drawPixmap(QPointF(area.center().x() - s.width() / 2, area.center().y() - s.height() / 2), icon);
        return;
    }
    QSizeF s = QSizeF(image.size()).scaled(area.size(), Qt::KeepAspectRatio);
    const QRectF target(area.center().x() - s.width() / 2, area.center().y() - s.height() / 2, s.width(),
                        s.height());
    p->drawImage(target, image);
    p->setPen(QPen(pal.border.color(), 1));
    p->setBrush(Qt::NoBrush);
    p->drawRect(target.adjusted(-0.5, -0.5, 0.5, 0.5));
}

void drawMarks(QPainter* p, const QRectF& box, const QModelIndex& index, int side) {
    qreal x = box.right() - side;
    if (index.data(home::FavoriteRole).toBool()) {
        p->drawPixmap(QPointF(x, box.top()), Icons::get(QStringLiteral("star"), IconTone::Accent, side));
        x -= side + Theme::sizes().grid;
    }
    if (index.data(home::PinnedRole).toBool()) {
        p->drawPixmap(QPointF(x, box.top()), Icons::get(QStringLiteral("pin"), IconTone::Muted, side));
    }
}

}  // namespace

void ThumbDelegate::paintGrid(QPainter* p, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    const Palette& pal = Theme::colors();
    const Metrics& m = Theme::sizes();
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hover = (option.state & QStyle::State_MouseOver) != 0;
    const QRectF card = QRectF(option.rect).adjusted(m.grid, m.grid, -m.grid, -m.grid);
    const qreal pen = selected ? m.focusRing : 1;
    p->setPen(QPen(selected ? pal.accent.color() : hover ? pal.textMuted.color() : pal.border.color(), pen));
    p->setBrush(hover && !selected ? pal.hover.color() : pal.surface.color());
    p->drawRoundedRect(card.adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2), m.radiusLarge, m.radiusLarge);

    const QFontMetrics fm(option.font);
    const qreal pad = m.grid * 2;
    const qreal textBlock = fm.height() * 2 + m.grid;
    const QRectF area = card.adjusted(pad, pad, -pad, -(pad + textBlock));
    drawThumb(p, area, thumbnail(index), index.data(home::IsDirRole).toBool(), m.iconEmpty);
    drawMarks(p, card.adjusted(pad / 2, pad / 2, -pad / 2, 0), index, m.iconRow);

    const QRectF nameRect(card.left() + pad, area.bottom() + m.grid, card.width() - pad * 2, fm.height());
    p->setFont(option.font);
    p->setPen(pal.text.color());
    p->drawText(
        nameRect, Qt::AlignLeft | Qt::AlignVCenter,
        fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, int(nameRect.width())));
    const QRectF detailRect = nameRect.translated(0, fm.height());
    p->setPen(pal.textMuted.color());
    p->drawText(
        detailRect, Qt::AlignLeft | Qt::AlignVCenter,
        fm.elidedText(index.data(home::DetailRole).toString(), Qt::ElideRight, int(detailRect.width())));
}

void ThumbDelegate::paintList(QPainter* p, const QStyleOptionViewItem& option,
                              const QModelIndex& index) const {
    const Palette& pal = Theme::colors();
    const Metrics& m = Theme::sizes();
    const bool selected = (option.state & QStyle::State_Selected) != 0;
    const bool hover = (option.state & QStyle::State_MouseOver) != 0;
    const QRectF row = QRectF(option.rect).adjusted(0, 1, 0, -1);
    if (selected || hover) {
        p->setPen(Qt::NoPen);
        p->setBrush(selected ? pal.accentSoft.color() : pal.hover.color());
        p->drawRoundedRect(row, m.radius, m.radius);
    }
    const qreal side = row.height() - m.grid * 2;
    const QRectF thumbBox(row.left() + m.grid * 2, row.top() + m.grid, side * 0.8, side);
    drawThumb(p, thumbBox, thumbnail(index), index.data(home::IsDirRole).toBool(), m.iconPanel);
    const QFontMetrics fm(option.font);
    const QString detail = index.data(home::DetailRole).toString();
    const qreal detailWidth = fm.horizontalAdvance(detail) + m.grid * 4;
    const QRectF marks(row.right() - detailWidth - m.iconRow * 2 - m.grid * 3,
                       row.center().y() - m.iconRow / 2.0, m.iconRow * 2 + m.grid, m.iconRow);
    drawMarks(p, marks, index, m.iconRow);
    const QRectF nameRect(thumbBox.right() + m.grid * 3, row.top(),
                          marks.left() - thumbBox.right() - m.grid * 4, row.height());
    p->setFont(option.font);
    p->setPen(pal.text.color());
    p->drawText(
        nameRect, Qt::AlignLeft | Qt::AlignVCenter,
        fm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, int(nameRect.width())));
    p->setPen(pal.textMuted.color());
    p->drawText(QRectF(row.right() - detailWidth, row.top(), detailWidth - m.grid * 2, row.height()),
                Qt::AlignRight | Qt::AlignVCenter, detail);
}

}  // namespace ot::ui
