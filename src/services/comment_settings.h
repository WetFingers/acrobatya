// Настройки комментирования (план 5.1; эталон 3.18 «Комментирование»; comment-08, comment-24, comment-42):
// Identity и автор новых комментариев, стиль нового комментария по инструменту (цвет, непрозрачность,
// толщина, иконка) поверх умолчаний ядра (core/annot/defaults.h toolDefaults). Хранение — settings.ini,
// ключи commenting/… (settings_keys.h).
#pragma once

#include <QString>

#include <optional>
#include <string>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "services/settings.h"

namespace ot::comments {

// Identity из Установок.
annot::Identity identity(const Settings& settings);
// Автор /T новых комментариев (имя входа или имя Identity).
std::string author(const Settings& settings);

// Свой стиль инструмента (ключ commenting/toolStyles): что задано — то и есть.
struct ToolStyle {
    std::optional<annot::AnnotColor> color;
    std::optional<float> opacity;
    std::optional<float> width;
    std::optional<std::string> icon;
    // 5.2: текстовое поле и выноска — шрифт (семейство), кегль, цвет текста, выравнивание; фигуры — заливка
    // /IC.
    std::optional<std::string> font;
    std::optional<float> fontSize;
    std::optional<annot::AnnotColor> textColor;
    std::optional<int> align;
    std::optional<annot::AnnotColor> fill;
};
ToolStyle toolStyle(const Settings& settings, const QString& tool);
void setToolStyle(Settings& settings, const QString& tool, const ToolStyle& style);

// Свойства нового комментария типа type для инструмента tool: умолчания ядра + свой стиль + автор.
annot::AnnotProps newProps(const Settings& settings, annot::AnnotType type, const QString& tool = {});

// "#rrggbb" ↔ цвет.
QString colorName(const annot::AnnotColor& color);
std::optional<annot::AnnotColor> parseColor(const QString& name);

}  // namespace ot::comments
