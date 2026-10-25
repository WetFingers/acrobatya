// Выравнивание и распределение пометок (план 5.5, п. 3; эталон 3.4 «Выделение и правка на странице»;
// comment-44): несколько выбранных пометок с /Rect одной страницы (не разметка текста) — по левому, правому,
// верхнему, нижнему краю или центрам; распределение — равные промежутки между соседними (крайние — на месте).
// Рамки — координаты страницы MuPDF (y вниз: «верх» — меньший y); размер рамок не меняется, только сдвиг
// (annot::setRect того же размера двигает всю геометрию без нового вида). Чистые функции.
#pragma once

#include <vector>

#include "core/util/geometry.h"

namespace ot::annot {

enum class Align { Left, HorizontalCenter, Right, Top, VerticalCenter, Bottom };
enum class Distribute { Horizontal, Vertical };

// По левому краю — к самой левой рамке, по правому — к самой правой, по верху и низу — так же; по центрам — к
// центру общей рамки. Меньше двух — как есть.
std::vector<RectF> alignRects(const std::vector<RectF>& rects, Align align);
// Равные промежутки по оси между рамками, упорядоченными по левому (верхнему) краю; крайние на месте. Меньше
// трёх — как есть. Порядок результата — порядок входа.
std::vector<RectF> distributeRects(const std::vector<RectF>& rects, Distribute distribute);

}  // namespace ot::annot
