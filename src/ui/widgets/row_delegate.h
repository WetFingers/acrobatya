// Строка списка 32 px для QListWidget/QListView (эталон 6.4: выбранная — accentSoft, наведение — hover) с
// теми же цветами rowColors(), что ListRow: текст слева, подпись справа (роль kDetailRole — клавиша,
// «появится в 0.N»), недоступная строка — серая (роль kDimmedRole или пункт без Qt::ItemIsEnabled). Категории
// «Установок», палитра команд.
#pragma once

#include <QStyledItemDelegate>

namespace ot::ui {

class RowDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    static constexpr int kDetailRole = Qt::UserRole + 100;
    static constexpr int kDimmedRole = Qt::UserRole + 101;

    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
};

}  // namespace ot::ui
