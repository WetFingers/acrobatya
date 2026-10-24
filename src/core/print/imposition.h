// Спуск полос печати (план 2.6; эталон 3.3 «Размер и обработка»; view-38…41) — чистая логика без MuPDF и
// без принтера: страницы документа (размеры в пунктах после /Rotate — fz_bound_page) и последовательность к
// печати (page_set.h) → листы с размещениями. Рисует band_renderer.h, печатает services/print_job.h,
// предпросмотр диалога — тот же список листов.
//
// Координаты листа — пункты от левого верхнего угла бумаги в её ориентации (как QPrinter с
// setFullPage(true)), область печати — поля принтера (QPageLayout::paintRect) в той же ориентации.
//   * Один на листе (Single): «Вписать» — в область печати по центру её, «Реальный размер» и «Свой масштаб» —
//     по центру бумаги (лишнее обрезает область печати), «Уменьшить негабаритные» — вписать только большие;
//     ориентация «Авто» — по странице (альбомная страница — альбомный лист).
//   * Несколько на листе (Multiple, n-up): сетка cols × rows по области печати, страница вписана в ячейку по
//     центру; порядок — по горизонтали / по горизонтали справа налево / по вертикали / по вертикали справа
//     налево; «Авто» выбирает ориентацию листа и сетки (2 = 2 × 1 или 1 × 2) с наибольшим масштабом;
//     автоповорот — страница поперёк ячейки поворачивается на 90°; рамки — вокруг каждой страницы.
//   * Буклет (Booklet): число страниц добивается пустыми до кратного 4, лист k (с нуля) — лицо
//     [N−2k, 1+2k], оборот [2+2k, N−1−2k] (номера с 1; корешок справа — пары наоборот); две половины листа,
//     все страницы — одним масштабом по наибольшей, прижаты к корешку; «Обе стороны» — лицо, оборот, лицо…
//     (дуплекс по короткой стороне — print_job), «Только лицо», «Только оборот» — обороты в обратном порядке
//     (стопку лиц переворачивают и кладут обратно).
//   * Постер (Poster): страница в масштабе плитки режется на листы области печати с перекрытием; число
//     листов — наименьшее из двух ориентаций («Авто»); изображение по центру сетки; метки реза — уголки
//     собственной области плитки (без перекрытия слева и сверху, которое повторяет соседнюю), обрезанные
//     областью печати; подпись «<имя> (строка, столбец)»; «Только большие страницы» — остальные как Single.
#pragma once

#include <string>
#include <vector>

#include "core/util/geometry.h"

namespace ot::print {

inline constexpr float kPointsPerMm = 72.0f / 25.4f;

struct SizeF {
    float width = 0;
    float height = 0;
    bool landscape() const noexcept { return width > height; }
    bool operator==(const SizeF&) const = default;
};

enum class Orientation { Auto, Portrait, Landscape };
enum class SizeMode { Fit, Actual, ShrinkOversized, Custom };
enum class Layout { Single, Multiple, Booklet, Poster };
enum class NupOrder { Horizontal, HorizontalReversed, Vertical, VerticalReversed };
enum class BookletSubset { BothSides, FrontOnly, BackOnly };
enum class Binding { Left, Right };
enum class SheetSide { Single, Front, Back };

// Бумага: книжный лист (width ≤ height) и области печати обеих ориентаций (пункты от левого верхнего угла
// листа в этой ориентации). Пустая область альбомного — поля книжного листа, повёрнутого на 90° против
// часовой (как у драйверов Windows): слева — верхнее поле, сверху — правое, справа — нижнее, снизу — левое.
struct Paper {
    SizeF size;
    RectF printable;
    RectF printableLandscape;

    SizeF oriented(bool landscape) const noexcept;
    RectF printableFor(bool landscape) const noexcept;

    // Лист с одинаковыми полями margin пунктов (0 — без полей).
    static Paper withMargins(SizeF portrait, float margin) noexcept;
    static Paper a4(float margin = 0) noexcept { return withMargins({595.2756f, 841.8898f}, margin); }
    static Paper letter(float margin = 0) noexcept { return withMargins({612.0f, 792.0f}, margin); }
};

struct ImposeOptions {
    Layout layout = Layout::Single;
    Orientation orientation = Orientation::Auto;
    // Single
    SizeMode size = SizeMode::Fit;
    float customScale = 1.0f;  // Custom: 1 = 100 %
    // Multiple
    int cols = 2;
    int rows = 2;
    NupOrder order = NupOrder::Horizontal;
    bool borders = false;
    bool autoRotate = true;  // Multiple и Booklet
    // Booklet
    BookletSubset bookletSubset = BookletSubset::BothSides;
    Binding binding = Binding::Left;
    // Poster
    float tileScale = 1.0f;  // 1 = 100 %
    float overlap = 0;       // пункты
    bool cutMarks = false;
    bool labels = false;
    bool tileLargeOnly = false;
    std::string title;  // имя файла для подписи плиток
};

// «Страниц на листе» диалога → сетка cols × rows (cols ≥ rows): 2 → 2 × 1, 4 → 2 × 2, 6 → 3 × 2, 9 → 3 × 3,
// 16 → 4 × 4; прочее — ближайшая сетка из множителей.
void nupGrid(int perSheet, int& cols, int& rows) noexcept;

struct Placement {
    int page = -1;     // страница документа с нуля; -1 — пустое место (добивка буклета)
    RectF target;      // страница на листе после поворота и масштаба (у постера выходит за лист)
    RectF clip;        // что рисовать: target ∩ область печати (∩ ячейка постера)
    int rotation = 0;  // 0, 90, 180, 270 — по часовой
    float scale = 1;   // пункты страницы → пункты листа
};

struct Line {
    PointF a;
    PointF b;
};

struct Sheet {
    SizeF size;  // лист в своей ориентации
    bool landscape = false;
    SheetSide side = SheetSide::Single;
    std::vector<Placement> placements;
    std::vector<RectF> frames;  // рамки страниц n-up
    std::vector<Line> marks;    // метки реза постера
    int tileRow = -1;           // плитка постера (с нуля) или -1
    int tileCol = -1;
    std::string label;  // подпись плитки постера
};

// Пункты страницы (рамка page — fz_bound_page, начало не обязательно 0, 0) → пункты листа.
Matrix placementMatrix(const Placement& placement, const RectF& page) noexcept;

// Листы печати: sizes — размеры всех страниц документа (по номеру), pages — последовательность к печати.
// Пустая последовательность — нет листов. Копии и подбор — не здесь (print_job).
std::vector<Sheet> impose(const std::vector<SizeF>& sizes, const std::vector<int>& pages, const Paper& paper,
                          const ImposeOptions& options);

}  // namespace ot::print
