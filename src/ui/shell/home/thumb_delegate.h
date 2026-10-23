// Карточка файла Главной (эталон 3.18, 6.4: скругление 8): сетка — миниатюра первой страницы 160 px, имя
// (многоточие посередине) и дата; список — строка с маленькой миниатюрой. Миниатюра берётся у ThumbnailCache
// только при отрисовке — значит, только для видимых карточек; пока её нет — значок файла (папки — значок
// папки). Звёздочка — в избранном, булавка — закреплён. Цвета — токены темы (наведение — hover, выбранная —
// рамка accent).
#pragma once

#include <QStyledItemDelegate>

namespace ot {
class ThumbnailCache;
}

namespace ot::ui {

class ThumbDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum class Mode { Grid, List };

    explicit ThumbDelegate(ThumbnailCache& thumbs, QObject* parent = nullptr);

    void setMode(Mode mode) { mode_ = mode; }
    Mode mode() const { return mode_; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    void paintGrid(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    void paintList(QPainter* p, const QStyleOptionViewItem& option, const QModelIndex& index) const;
    QImage thumbnail(const QModelIndex& index) const;

    ThumbnailCache& thumbs_;
    Mode mode_ = Mode::Grid;
};

}  // namespace ot::ui
