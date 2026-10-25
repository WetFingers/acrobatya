// Fill & Sign: умное размещение текста и отметок (план 5.3, п. 2; эталон 3.5 «Умное размещение (из C): при
// клике на плоской форме поле ищется по подчёркиваниям и рамкам из COLLECT_VECTORS — текст ставится на линию,
// кегль под высоту клетки; «гребёнка» (одна буква в клетку) — режим с шагом»; forms-07, comment-09):
//   * геометрия страницы — из векторов текстового слоя (TextPage::Options{collectVectors}): тонкий осевой
//     прямоугольник — горизонталь или вертикаль (отрезок m l S, полоса re f), обводка re S — четыре края
//     рамки (квадрат 6–20 пт — ещё и флажок), светлая заливка — края поля; рамки MuPDF расширены на толщину
//     линии — края сдвигаются внутрь на половину толщины тонких линий страницы;
//   * щелчок внутри замкнутой клетки (горизонтали сверху и снизу ≤ 40 пт, вертикали по бокам) — текст от её
//     левого края по центру высоты; иначе — ближайшая горизонталь под точкой (до 30 пт) или чуть над ней (до
//     6 пт): текст на линии с точки щелчка до конца линии, высота клетки — до горизонтали или строки текста
//     выше (нет — 20 пт); кегль — 0,55 высоты клетки в 6–12 пт;
//   * гребёнка — у линии-основания не меньше 4 клеток равного шага 6–30 пт: засечки (короткие вертикали от
//     линии) или смежные рамки; символы — по одному в клетку шрифтом текста (цифры Arial одной ширины) по
//     центру;
//   * отметки ✓ ✕ ● — ▭ — FreeText с глифом Segoe UI Symbol (core/annot/freetext.h kSymbolFont), во флажке —
//   по
//     его центру и размеру, иначе — центром в точке щелчка;
//   * текст и отметки — FreeText без рамки и заливки с /IT /FreeTextTypeWriter (так пишет Acrobat «Заполнить
//   и
//     подписать»), вид — свой вид ядра (встроенный TrueType в /DR).
// Координаты — страницы MuPDF (пункты, y вниз), как у annots.h. Чистые функции, кроме createAll (журнал).
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/annot/annots.h"
#include "core/text/text_types.h"
#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::fillsign {

// Горизонталь (подчёркивание, край рамки) и вертикаль (край рамки, засечка).
struct HLine {
    float y = 0;
    float x0 = 0;
    float x1 = 0;
};
struct VLine {
    float x = 0;
    float y0 = 0;
    float y1 = 0;
};

struct PageGeometry {
    std::vector<HLine> horizontal;
    std::vector<VLine> vertical;
    std::vector<RectF> checks;  // квадраты-флажки
    std::vector<RectF> text;    // строки текста
};

// Геометрия бланка по векторам и строкам текстового слоя.
PageGeometry geometryOf(const std::vector<TextVector>& vectors, const std::vector<TextLine>& lines);

enum class FieldKind { None, Line, Box, Comb, Check };

inline constexpr float kDefaultFontSize = 10;  // текст без привязки (как у Acrobat)
inline constexpr float kMinFontSize = 6;
inline constexpr float kMaxFontSize = 12;
inline constexpr float kMarkSize = 14;  // отметка вне флажка

struct Placement {
    FieldKind kind = FieldKind::None;
    RectF field;               // клетка или линия с высотой клетки; None — рамка у точки
    float x = 0;               // начало текста
    float baseline = 0;        // базовая линия первой строки
    float fontSize = 0;        // кегль под клетку
    float cellHeight = 0;      // высота клетки
    std::vector<RectF> cells;  // гребёнка — клетки слева направо

    bool snapped() const noexcept { return kind != FieldKind::None; }
};

// Куда ставить текст по щелчку point; без поля — строка с кеглем defaultSize, точка — середина строки.
Placement placeText(const PageGeometry& geometry, PointF point, float defaultSize = kDefaultFontSize);
// Куда ставить отметку: флажок под точкой (±3 пт) — его рамка и кегль, иначе кегль size центром в точке.
Placement placeMark(const PageGeometry& geometry, PointF point, float size = kMarkSize);

inline constexpr const char* kTextFont = "Arial";
inline constexpr const char* kCombFont = "Arial";  // цифры Arial одной ширины; третий шрифт в файл не идёт
inline constexpr const char* kMarkFont = "Segoe UI Symbol";
inline constexpr const char* kTypeWriterIntent = "FreeTextTypeWriter";

enum class Mark { Check, Cross, Dot, Line, Box };
// Символ отметки в UTF-8: ✓ ✕ ● — ▭.
std::string markText(Mark mark);

// Поле FreeText шрифта family, у которого первая базовая линия — baseline, текст — с x0 до x1 (отступ поля
// 2 пт внутри).
RectF textBoxRect(std::string_view family, float x0, float x1, float baseline, float fontSize);
// FreeText Fill & Sign: text от placement.x до края клетки (без привязки — до 200 пт, не дальше pageWidth),
// Arial кеглем placement, цвет текста color; без рамки и заливки, /IT /FreeTextTypeWriter.
annot::AnnotProps textProps(const Placement& placement, std::string text, annot::AnnotColor color,
                            float pageWidth);
// Гребёнка: по символу в клетку (лишние отброшены, пробел — пустая клетка), Arial по центру клетки.
std::vector<annot::AnnotProps> combProps(const Placement& placement, std::u32string_view text,
                                         annot::AnnotColor color);
// Отметка: глиф Segoe UI Symbol центром в центре placement.field кеглем placement.
annot::AnnotProps markProps(Mark mark, const Placement& placement, annot::AnnotColor color);

// Несколько аннотаций одним шагом журнала operation (гребёнка — «Add text»).
std::vector<annot::AnnotRef> createAll(Document& doc, int page, const std::vector<annot::AnnotProps>& items,
                                       const std::string& operation = "Add text");

}  // namespace ot::fillsign
