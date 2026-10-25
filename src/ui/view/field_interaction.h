// Поля формы в виде (план 6.1, п. 2; эталон 3.6; forms-01, forms-03): слой заполнения (ui/tools/forms/
// form_fill.h) получает мышь и клавиши раньше «Выделения», «Руки» и «Панорамирования» (как ссылки,
// ToolController::followsLinks) — щелчок по полю открывает редактор, флажок переключается, кнопка нажимается;
// Tab/Shift+Tab вида — сначала по полям в порядке /Tabs, потом по ссылкам. Вид не владеет слоем
// (PageView::setFieldInteraction; владелец снимает себя nullptr до своей смерти).
#pragma once

#include <QCursor>
#include <QPointF>

#include <optional>

class QKeyEvent;
class QMouseEvent;

namespace ot::ui {

class FieldInteraction {
public:
    virtual ~FieldInteraction() = default;
    // Нажатие и отпускание левой кнопки (координаты вьюпорта): true — событие поля, инструменту не идёт.
    virtual bool fieldPress(QMouseEvent* event) = 0;
    virtual bool fieldRelease(QMouseEvent* event) = 0;
    // Курсор над полем; nullopt — не над полем.
    virtual std::optional<QCursor> fieldCursor(QPointF point) const = 0;
    // Клавиша вида, когда поле в фокусе без редактора (флажок, переключатель, кнопка): пробел, Enter,
    // стрелки.
    virtual bool fieldKey(QKeyEvent* event) = 0;
    // Эту клавишу поле в фокусе возьмёт само (стрелки переключателей, Enter) — раньше ярлыков окна.
    virtual bool wantsKey(const QKeyEvent* event) const = 0;
    // Tab/Shift+Tab вида: true — фокус ушёл на поле.
    virtual bool focusNextField(bool next) = 0;
};

}  // namespace ot::ui
