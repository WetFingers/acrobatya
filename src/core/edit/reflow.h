// Перевёрстка абзаца (эталон 3.9 «Модель»: правка → Reflow → Rewriter; план 3.3, п. 2; edit-01, edit-03 —
// интервалы и масштаб): жадный перенос по словам в ширину набора (край строки — правый край последнего глифа,
// без Tc после него; слово длиннее строки — по символам), без
// переносов по дефису; перевод строки «\n» — жёсткий разрыв. Пробелы на стыке переноса остаются в конце
// строки и в ширину не входят; ведущие пробелы абзаца и строки после разрыва — входят. Интервал строки —
// lineSpacing × наибольший кегль её символов; первая базовая линия на месте — абзац растёт вниз, соседние
// блоки не двигаются (как в Acrobat), наложение на них — в overlaps. Выравнивание: влево и по ширине — от
// левого края (у первой строки — с отступом), вправо и по центру — сдвигом строки; по ширине — добавка на
// каждый пробел внутри строки (Rewriter пишет её Tw или сдвигом TJ), кроме последней строки и строки перед
// жёстким разрывом.
//
// Ширины символов измеряет Rewriter по шрифту набора (ReflowChar); перевёрстка — без MuPDF, значения.
#pragma once

#include <vector>

#include "core/edit/edit_types.h"
#include "core/edit/paragraph_editor.h"
#include "core/util/geometry.h"

namespace ot::edit {

// Символ для перевёрстки. Пункты пространства пользователя.
struct ReflowChar {
    char32_t ch = 0;
    float advance = 0;  // перо после символа: w0 · кегль · Tz + Tc (+ Tw у пробела; без добавки по ширине)
    float width = -1;   // ширина самого глифа (w0 · кегль · Tz) — правый край строки; −1 — как advance
    float ascent = 0;   // над базовой линией (с подъёмом Ts)
    float descent = 0;  // под базовой линией (≥ 0)
    float size = 0;     // кегль — интервал строки
};

struct ReflowParams {
    float left = 0;
    float width = 0;
    float firstIndent = 0;
    float firstBaseline = 0;
    float lineSpacing = ParagraphEditor::kDefaultLineSpacing;
    Align align = Align::Left;
    float emptySize = 12;          // кегль пустого абзаца (высота курсора)
    std::vector<RectF> obstacles;  // другие блоки страницы (раскладка) — проверка наложения
};

struct ReflowResult {
    TextLayout layout;
    std::vector<int> overlaps;  // индексы obstacles, на которые наехал абзац
};

// Пробел, на котором переносится строка и который растягивается по ширине: U+0020, U+1680, U+2000–U+200A
// (кроме U+2007), U+205F, U+3000; неразрывные (U+00A0, U+2007, U+202F) — часть слова.
bool isBreakSpace(char32_t c) noexcept;
bool isHardBreak(char32_t c) noexcept;  // «\n», U+2028, U+2029

// Перо после символа ch в стиле style с шириной глифа w0 (доли кегля): w0 · кегль · масштаб + Tc (+ Tw у
// U+0020).
float naturalAdvance(char32_t ch, float w0, const CharStyle& style) noexcept;
// Рамки страницы (координаты страницы) в раскладке редактора — obstacles для reflowParams.
std::vector<RectF> toLayoutRects(const ParagraphEditor& editor, const std::vector<RectF>& pageRects);

// Параметры перевёрстки редактора: геометрия набора, интервал, выравнивание; obstacles — блоки страницы.
ReflowParams reflowParams(const ParagraphEditor& editor, std::vector<RectF> obstacles = {});

ReflowResult reflow(const std::vector<ReflowChar>& chars, const ReflowParams& params);

}  // namespace ot::edit
