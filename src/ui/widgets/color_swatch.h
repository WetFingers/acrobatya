// Выбор цвета (комментарии, Fill & Sign, колонтитулы): квадрат текущего цвета и палитра — 12 цветов пометок
// (kAnnotationColors) и «Другой цвет…» (QColorDialog). colorChanged — только по выбору человека.
#pragma once

#include <QColor>
#include <QToolButton>

#include <vector>

class QMenu;

namespace ot::ui {

class ColorSwatch : public QToolButton {
    Q_OBJECT
public:
    explicit ColorSwatch(const QColor& color = {}, QWidget* parent = nullptr);

    QColor color() const { return color_; }
    void setColor(const QColor& color);  // без сигнала
    QMenu* paletteMenu() const { return menu_; }
    const std::vector<QToolButton*>& presetButtons() const { return presets_; }

signals:
    void colorChanged(const QColor& color);

private:
    void choose(const QColor& color);
    void chooseOther();
    void applyMetrics();
    QPixmap swatchPixmap(const QColor& color, int side) const;

    QColor color_;
    QMenu* menu_ = nullptr;
    std::vector<QToolButton*> presets_;
};

}  // namespace ot::ui
