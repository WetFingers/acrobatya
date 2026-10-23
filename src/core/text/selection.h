// Выделение текста (эталон 3.2, план 1.4) над TextPage: поток от точки до точки (протягивание мышью, `V`),
// привязка к словам и строкам (двойной и тройной щелчок — как fz_snap_selection), прямоугольник (столбцы —
// Alt+рамка, как fz_copy_rectangle: символ выделен, если его рамка задевает область — так же Type3 и
// вертикальный CJK), «Выделить всё» (Ctrl+A), диапазон символов (совпадение поиска). Текст — UTF-8 со «\n»
// между строками (управляющие символы — U+FFFD, как у fz_copy_selection), HTML — жирный и курсив по шрифтам.
//
// Свои функции, а не fz_highlight_selection/fz_copy_selection/fz_snap_selection/fz_copy_rectangle: в
// MuPDF 1.28.5 они обходят только верхний уровень блоков, а SEGMENT кладёт текст страницы в struct-узлы — на
// таком слое они не выделяют ничего (канарейка unit.selection.mupdf_selection_ignores_struct_blocks).
// Алгоритм тот же: ближайшая к точке граница символа (find_closest_in_page), символы между границами в
// порядке чтения, квады соседних символов строки слиты (разрыв больше половины кегля — новый квад). Квад —
// высотой в строку: у ACCURATE_BBOXES рамка символа — его чернила (у пробела высота нулевая), выделение по
// ним было бы рваным; так же рамка символа для прямоугольника. Чистый C++ над значениями TextPage — любой
// поток.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/util/geometry.h"

namespace ot {

class TextPage;

enum class SnapMode { Chars, Words, Lines };

struct TextSelection {
    std::vector<int> chars;    // индексы TextPage::chars() по порядку чтения
    std::vector<QuadF> quads;  // подсветка: по кваду высотой в строку на отрезок строки
    bool rectangle = false;    // выделено прямоугольником (столбцы)

    bool empty() const noexcept { return chars.empty(); }
};

// Граница символа, ближайшая к точке: 0…chars().size() (граница i — перед символом i).
int closestBoundary(const TextPage& page, PointF p);

// Поток от a до b (порядок точек любой). Chars: одна точка — пусто; Words/Lines расширяют концы до слова
// (буквы, цифры, метки — TextPage::wordBounds) или строки, так что и одна точка выделяет слово или строку.
TextSelection selectStream(const TextPage& page, PointF a, PointF b, SnapMode snap = SnapMode::Chars);
TextSelection selectRectangle(const TextPage& page, const RectF& area);
TextSelection selectAll(const TextPage& page);
// Символы [first, last] (включительно); first > last — пусто.
TextSelection selectRange(const TextPage& page, int first, int last);

std::string toPlainText(const TextPage& page, const TextSelection& selection);
// HTML-фрагмент для буфера обмена (text/html): <html><head><meta charset="utf-8"></head><body>, текстовый
// блок — <p>, строка внутри блока — <br>, жирный — <b>, курсив — <i> (по имени и флагам шрифта; строка за
// строкой).
std::string toHtml(const TextPage& page, const TextSelection& selection);
// То же без обёртки документа (<p>…</p> подряд) — выделение нескольких страниц склеивается фрагментами.
std::string toHtmlFragment(const TextPage& page, const TextSelection& selection);
// Обёртка фрагмента: <html><head><meta charset="utf-8"></head><body>…</body></html>.
std::string htmlDocument(std::string_view fragment);
std::string htmlEscape(std::string_view text);

}  // namespace ot
