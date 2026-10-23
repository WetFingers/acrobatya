// Кнопка-иконка (эталон 6.4–6.6): иконка Lucide по теме (20 px в панелях, 16 в строках), зона нажатия ≥ 32
// px, наведение — hover, нажатая (checkable) — accentSoft и иконка accent, фокус — рамка accent 2 px. Подпись
// — рядом с иконкой или только подсказкой и именем для доступности.
#pragma once

#include <QToolButton>

#include "ui/theme/icons.h"

namespace ot::ui {

class ToolButton : public QToolButton {
    Q_OBJECT
public:
    enum class IconSize { Panel, Row };

    explicit ToolButton(const QString& iconName = {}, const QString& label = {}, QWidget* parent = nullptr);

    void setIconName(const QString& name, IconTone tone = IconTone::Text);
    QString iconName() const { return iconName_; }
    // Подпись: видимая (рядом с иконкой) или только подсказка; в обоих случаях — имя для доступности.
    void setLabel(const QString& label);
    QString label() const { return label_; }
    void setLabelVisible(bool visible);
    void setIconSizeKind(IconSize kind);
    // Компактная — без минимума зоны нажатия (крестик внутри чипа: зона нажатия — сам чип); сторона — side.
    void setCompact(int side);

private:
    void applyMetrics();

    QString iconName_;
    QString label_;
    IconSize sizeKind_ = IconSize::Panel;
    int compactSide_ = 0;
};

}  // namespace ot::ui
