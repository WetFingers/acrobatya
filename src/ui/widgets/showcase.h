// Витрина компонентов (эталон 6.4: AcroBatya.exe --showcase, сцены showcase/-dark/-large): каждый компонент
// src/ui/widgets в состояниях «обычное», «наведение», «фокус», «выключено» в текущей теме и масштабе.
#pragma once

#include <QWidget>

namespace ot::ui {

enum class ShowcaseState { Normal, Hover, Focus, Disabled };

// Содержимое витрины (без прокрутки и переключателей) — для сцен и тестов.
QWidget* createShowcase(QWidget* parent = nullptr);
// Окно --showcase: переключатели темы и «Крупных элементов» над витриной в прокрутке.
QWidget* createShowcaseWindow();
// Показать состояние без мыши и клавиатуры: наведение — WA_UnderMouse, фокус — свойство otFocus (QSS),
// выключено.
void forceShowcaseState(QWidget* widget, ShowcaseState state);

}  // namespace ot::ui
