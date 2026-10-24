#include "core/print/imposition.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ot::print {

namespace {

constexpr float kTie = 1e-4f;        // относительная разница масштабов «равно»
constexpr float kMarkLength = 9.0f;  // метка реза, пункты
constexpr float kMarkGap = 3.0f;     // зазор от угла
constexpr SizeF kLetter{612.0f, 792.0f};

SizeF sizeOf(const std::vector<SizeF>& sizes, int page) noexcept {
    if (page < 0 || page >= static_cast<int>(sizes.size())) {
        return kLetter;
    }
    const SizeF s = sizes[static_cast<std::size_t>(page)];
    return s.width > 0 && s.height > 0 ? s : kLetter;  // страница нулевого размера — как Letter (fit.h)
}

RectF intersect(const RectF& a, const RectF& b) noexcept {
    RectF r{std::max(a.x0, b.x0), std::max(a.y0, b.y0), std::min(a.x1, b.x1), std::min(a.y1, b.y1)};
    if (r.empty()) {
        return {};
    }
    return r;
}

RectF centered(const RectF& area, SizeF size) noexcept {
    const float x = area.x0 + (area.width() - size.width) / 2;
    const float y = area.y0 + (area.height() - size.height) / 2;
    return {x, y, x + size.width, y + size.height};
}

SizeF rotated(SizeF s, int rotation) noexcept { return rotation % 180 == 0 ? s : SizeF{s.height, s.width}; }

struct Fitted {
    float scale = 1;
    int rotation = 0;
    SizeF size;  // после поворота и масштаба
};

// Вписать страницу в ячейку: без поворота или (autoRotate) повернув на 90°, если так крупнее.
Fitted fitInto(SizeF page, SizeF cell, bool autoRotate) noexcept {
    const float s0 = std::min(cell.width / page.width, cell.height / page.height);
    Fitted f{s0, 0, {page.width * s0, page.height * s0}};
    if (autoRotate) {
        const float s1 = std::min(cell.width / page.height, cell.height / page.width);
        if (s1 > s0 * (1 + kTie)) {
            f = {s1, 90, {page.height * s1, page.width * s1}};
        }
    }
    return f;
}

// Лучше ли кандидат (масштаб, без поворота, книжный) прежнего.
struct Score {
    float scale = 0;
    bool rotated = false;
    bool landscape = false;
};
bool better(const Score& a, const Score& b) noexcept {
    if (a.scale > b.scale * (1 + kTie)) {
        return true;
    }
    if (b.scale > a.scale * (1 + kTie)) {
        return false;
    }
    if (a.rotated != b.rotated) {
        return !a.rotated;
    }
    return !a.landscape && b.landscape;
}

std::vector<bool> orientationsFor(Orientation o) {
    switch (o) {
        case Orientation::Portrait:
            return {false};
        case Orientation::Landscape:
            return {true};
        case Orientation::Auto:
            break;
    }
    return {false, true};
}

Sheet makeSheet(const Paper& paper, bool landscape, SheetSide side = SheetSide::Single) {
    Sheet s;
    s.landscape = landscape;
    s.size = paper.oriented(landscape);
    s.side = side;
    return s;
}

// ---- Один на листе ----------------------------------------------------------------------------------------

void imposeSingle(const std::vector<SizeF>& sizes, int page, const Paper& paper, const ImposeOptions& o,
                  std::vector<Sheet>& out, float fixedScale = 0) {
    const SizeF ps = sizeOf(sizes, page);
    const bool landscape =
        o.orientation == Orientation::Auto ? ps.landscape() : o.orientation == Orientation::Landscape;
    Sheet sheet = makeSheet(paper, landscape);
    const RectF printable = paper.printableFor(landscape);
    const float fit = std::min(printable.width() / ps.width, printable.height() / ps.height);
    float scale = 1;
    bool toPrintable = false;
    if (fixedScale > 0) {
        scale = fixedScale;
    } else {
        switch (o.size) {
            case SizeMode::Fit:
                scale = fit;
                toPrintable = true;
                break;
            case SizeMode::ShrinkOversized:
                if (fit < 1) {
                    scale = fit;
                    toPrintable = true;
                }
                break;
            case SizeMode::Actual:
                break;
            case SizeMode::Custom:
                scale = o.customScale > 0 ? o.customScale : 1;
                break;
        }
    }
    Placement p;
    p.page = page;
    p.scale = scale;
    p.target = centered(toPrintable ? printable : RectF{0, 0, sheet.size.width, sheet.size.height},
                        {ps.width * scale, ps.height * scale});
    p.clip = intersect(p.target, printable);
    sheet.placements.push_back(p);
    out.push_back(std::move(sheet));
}

// ---- Несколько на листе ----------------------------------------------------------------------------------

void imposeMultiple(const std::vector<SizeF>& sizes, const std::vector<int>& pages, const Paper& paper,
                    const ImposeOptions& o, std::vector<Sheet>& out) {
    const int cols0 = std::max(1, o.cols);
    const int rows0 = std::max(1, o.rows);
    const SizeF ref = sizeOf(sizes, pages.front());
    bool landscape = false;
    int cols = cols0;
    int rows = rows0;
    Score best;
    bool have = false;
    for (const bool land : orientationsFor(o.orientation)) {
        const RectF printable = paper.printableFor(land);
        const std::pair<int, int> grids[] = {{cols0, rows0}, {rows0, cols0}};
        for (const auto& [c, r] : grids) {
            const SizeF cell{printable.width() / static_cast<float>(c),
                             printable.height() / static_cast<float>(r)};
            const Fitted f = fitInto(ref, cell, o.autoRotate);
            const Score score{f.scale, f.rotation != 0, land};
            if (!have || better(score, best)) {
                best = score;
                have = true;
                landscape = land;
                cols = c;
                rows = r;
            }
        }
    }
    const RectF printable = paper.printableFor(landscape);
    const SizeF cell{printable.width() / static_cast<float>(cols),
                     printable.height() / static_cast<float>(rows)};
    const std::size_t perSheet = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);
    for (std::size_t first = 0; first < pages.size(); first += perSheet) {
        Sheet sheet = makeSheet(paper, landscape);
        for (std::size_t k = 0; k < perSheet && first + k < pages.size(); ++k) {
            const int i = static_cast<int>(k);
            int col = 0;
            int row = 0;
            switch (o.order) {
                case NupOrder::Horizontal:
                    col = i % cols;
                    row = i / cols;
                    break;
                case NupOrder::HorizontalReversed:
                    col = cols - 1 - i % cols;
                    row = i / cols;
                    break;
                case NupOrder::Vertical:
                    row = i % rows;
                    col = i / rows;
                    break;
                case NupOrder::VerticalReversed:
                    row = i % rows;
                    col = cols - 1 - i / rows;
                    break;
            }
            const RectF cellRect{printable.x0 + cell.width * static_cast<float>(col),
                                 printable.y0 + cell.height * static_cast<float>(row),
                                 printable.x0 + cell.width * static_cast<float>(col + 1),
                                 printable.y0 + cell.height * static_cast<float>(row + 1)};
            const int page = pages[first + k];
            const Fitted f = fitInto(sizeOf(sizes, page), cell, o.autoRotate);
            Placement p;
            p.page = page;
            p.scale = f.scale;
            p.rotation = f.rotation;
            p.target = centered(cellRect, f.size);
            p.clip = intersect(p.target, printable);
            sheet.placements.push_back(p);
            if (o.borders) {
                sheet.frames.push_back(p.target);
            }
        }
        out.push_back(std::move(sheet));
    }
}

// ---- Буклет
// -------------------------------------------------------------------------------------------------

struct BookletGeometry {
    bool landscape = false;
    float scale = 0;
    float spine = 0;
    RectF printable;
};

// Поворот страницы в половине листа: поперёк половины — на 90° (автоповорот).
int bookletRotation(SizeF page, SizeF half, bool autoRotate) noexcept {
    if (!autoRotate || page.width == page.height) {
        return 0;
    }
    return page.landscape() != half.landscape() ? 90 : 0;
}

BookletGeometry bookletFor(const std::vector<SizeF>& sizes, const std::vector<int>& pages, const Paper& paper,
                           bool landscape, bool autoRotate) {
    BookletGeometry g;
    g.landscape = landscape;
    g.printable = paper.printableFor(landscape);
    g.spine = paper.oriented(landscape).width / 2;
    const SizeF half{std::min(g.spine - g.printable.x0, g.printable.x1 - g.spine), g.printable.height()};
    if (half.width <= 0 || half.height <= 0) {
        return g;
    }
    float maxW = 0;
    float maxH = 0;
    for (const int page : pages) {
        const SizeF ps = sizeOf(sizes, page);
        const SizeF r = rotated(ps, bookletRotation(ps, half, autoRotate));
        maxW = std::max(maxW, r.width);
        maxH = std::max(maxH, r.height);
    }
    g.scale = std::min(half.width / maxW, half.height / maxH);
    return g;
}

Sheet bookletSide(const std::vector<SizeF>& sizes, const std::vector<int>& seq, const Paper& paper,
                  const BookletGeometry& g, bool autoRotate, int leftNumber, int rightNumber,
                  SheetSide side) {
    Sheet sheet = makeSheet(paper, g.landscape, side);
    const SizeF half{std::min(g.spine - g.printable.x0, g.printable.x1 - g.spine), g.printable.height()};
    for (const int number : {leftNumber, rightNumber}) {
        const bool left = number == leftNumber;
        const int index = number - 1;
        const int page = index < static_cast<int>(seq.size()) ? seq[static_cast<std::size_t>(index)] : -1;
        Placement p;
        p.page = page;
        p.scale = g.scale;
        if (page < 0) {
            p.target = left ? RectF{g.spine - half.width, g.printable.y0, g.spine, g.printable.y1}
                            : RectF{g.spine, g.printable.y0, g.spine + half.width, g.printable.y1};
            sheet.placements.push_back(p);
            continue;
        }
        const SizeF ps = sizeOf(sizes, page);
        p.rotation = bookletRotation(ps, half, autoRotate);
        const SizeF r = rotated(ps, p.rotation);
        const SizeF size{r.width * g.scale, r.height * g.scale};
        const float y = g.printable.y0 + (g.printable.height() - size.height) / 2;
        // Прижата к корешку: левая — правым краем, правая — левым.
        const float x = left ? g.spine - size.width : g.spine;
        p.target = {x, y, x + size.width, y + size.height};
        p.clip = intersect(p.target, g.printable);
        sheet.placements.push_back(p);
    }
    return sheet;
}

void imposeBooklet(const std::vector<SizeF>& sizes, const std::vector<int>& pages, const Paper& paper,
                   const ImposeOptions& o, std::vector<Sheet>& out) {
    BookletGeometry g;
    bool have = false;
    // «Авто» — ориентация с наибольшим масштабом; при равенстве — альбомная (две книжные рядом).
    for (const bool land : o.orientation == Orientation::Auto ? std::vector<bool>{true, false}
                                                              : orientationsFor(o.orientation)) {
        const BookletGeometry c = bookletFor(sizes, pages, paper, land, o.autoRotate);
        if (!have || c.scale > g.scale * (1 + kTie)) {
            g = c;
            have = true;
        }
    }
    const int count = static_cast<int>(pages.size());
    const int padded = (count + 3) / 4 * 4;
    const int sheets = padded / 4;
    const bool right = o.binding == Binding::Right;
    std::vector<Sheet> fronts;
    std::vector<Sheet> backs;
    for (int k = 0; k < sheets; ++k) {
        int fl = padded - 2 * k;
        int fr = 1 + 2 * k;
        int bl = 2 + 2 * k;
        int br = padded - 1 - 2 * k;
        if (right) {
            std::swap(fl, fr);
            std::swap(bl, br);
        }
        fronts.push_back(bookletSide(sizes, pages, paper, g, o.autoRotate, fl, fr, SheetSide::Front));
        backs.push_back(bookletSide(sizes, pages, paper, g, o.autoRotate, bl, br, SheetSide::Back));
    }
    switch (o.bookletSubset) {
        case BookletSubset::BothSides:
            for (int k = 0; k < sheets; ++k) {
                out.push_back(std::move(fronts[static_cast<std::size_t>(k)]));
                out.push_back(std::move(backs[static_cast<std::size_t>(k)]));
            }
            break;
        case BookletSubset::FrontOnly:
            for (Sheet& s : fronts) {
                out.push_back(std::move(s));
            }
            break;
        case BookletSubset::BackOnly:
            for (auto it = backs.rbegin(); it != backs.rend(); ++it) {
                out.push_back(std::move(*it));
            }
            break;
    }
}

// ---- Постер
// ---------------------------------------------------------------------------------------------------

int tilesAlong(float length, float overlap, float step) noexcept {
    if (step <= 0) {
        return 1;
    }
    return std::max(1, static_cast<int>(std::ceil((length - overlap) / step - 1e-3f)));
}

// Отрезок метки по оси, обрезанный областью печати (вне её — нет).
void addMark(std::vector<Line>& marks, PointF a, PointF b, const RectF& area) {
    if (a.y == b.y) {  // горизонтальная
        if (a.y < area.y0 || a.y > area.y1) {
            return;
        }
        const float x0 = std::max(std::min(a.x, b.x), area.x0);
        const float x1 = std::min(std::max(a.x, b.x), area.x1);
        if (x1 - x0 >= 0.5f) {
            marks.push_back({{x0, a.y}, {x1, a.y}});
        }
    } else {
        if (a.x < area.x0 || a.x > area.x1) {
            return;
        }
        const float y0 = std::max(std::min(a.y, b.y), area.y0);
        const float y1 = std::min(std::max(a.y, b.y), area.y1);
        if (y1 - y0 >= 0.5f) {
            marks.push_back({{a.x, y0}, {a.x, y1}});
        }
    }
}

void cutMarks(Sheet& sheet, const RectF& own, const RectF& area) {
    const float corners[4][4] = {
        {own.x0, own.y0, -1, -1}, {own.x1, own.y0, 1, -1}, {own.x0, own.y1, -1, 1}, {own.x1, own.y1, 1, 1}};
    for (const auto& c : corners) {
        const float x = c[0], y = c[1], dx = c[2], dy = c[3];
        addMark(sheet.marks, {x + dx * kMarkGap, y}, {x + dx * (kMarkGap + kMarkLength), y}, area);
        addMark(sheet.marks, {x, y + dy * kMarkGap}, {x, y + dy * (kMarkGap + kMarkLength)}, area);
    }
}

void imposePoster(const std::vector<SizeF>& sizes, int page, const Paper& paper, const ImposeOptions& o,
                  std::vector<Sheet>& out) {
    const SizeF ps = sizeOf(sizes, page);
    const float scale = o.tileScale > 0 ? o.tileScale : 1;
    const SizeF poster{ps.width * scale, ps.height * scale};
    if (o.tileLargeOnly) {
        for (const bool land : orientationsFor(o.orientation)) {
            const RectF printable = paper.printableFor(land);
            if (poster.width <= printable.width() + 0.01f && poster.height <= printable.height() + 0.01f) {
                ImposeOptions single = o;
                single.orientation = land ? Orientation::Landscape : Orientation::Portrait;
                imposeSingle(sizes, page, paper, single, out, scale);
                return;
            }
        }
    }
    bool landscape = false;
    int cols = 1;
    int rows = 1;
    float overlap = 0;
    bool have = false;
    for (const bool land : orientationsFor(o.orientation)) {
        const RectF printable = paper.printableFor(land);
        const float ov = std::clamp(o.overlap, 0.0f, std::min(printable.width(), printable.height()) / 2);
        const int c = tilesAlong(poster.width, ov, printable.width() - ov);
        const int r = tilesAlong(poster.height, ov, printable.height() - ov);
        if (!have || c * r < cols * rows) {
            have = true;
            landscape = land;
            cols = c;
            rows = r;
            overlap = ov;
        }
    }
    const RectF printable = paper.printableFor(landscape);
    const float stepX = printable.width() - overlap;
    const float stepY = printable.height() - overlap;
    const float offX = (static_cast<float>(cols) * stepX + overlap - poster.width) / 2;
    const float offY = (static_cast<float>(rows) * stepY + overlap - poster.height) / 2;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Sheet sheet = makeSheet(paper, landscape);
            sheet.tileRow = r;
            sheet.tileCol = c;
            Placement p;
            p.page = page;
            p.scale = scale;
            const float x = printable.x0 + offX - static_cast<float>(c) * stepX;
            const float y = printable.y0 + offY - static_cast<float>(r) * stepY;
            p.target = {x, y, x + poster.width, y + poster.height};
            p.clip = intersect(p.target, printable);
            sheet.placements.push_back(p);
            if (o.cutMarks) {
                // Собственная область плитки: без перекрытия слева и сверху (его печатает соседняя).
                const RectF own{std::max(printable.x0 + (c > 0 ? overlap : 0), p.target.x0),
                                std::max(printable.y0 + (r > 0 ? overlap : 0), p.target.y0),
                                std::min(printable.x1, p.target.x1), std::min(printable.y1, p.target.y1)};
                cutMarks(sheet, own, printable);
            }
            if (o.labels) {
                sheet.label = (o.title.empty() ? std::string() : o.title + " ") + "(" +
                              std::to_string(r + 1) + ", " + std::to_string(c + 1) + ")";
            }
            out.push_back(std::move(sheet));
        }
    }
}

}  // namespace

SizeF Paper::oriented(bool landscape) const noexcept {
    const float w = std::min(size.width, size.height);
    const float h = std::max(size.width, size.height);
    return landscape ? SizeF{h, w} : SizeF{w, h};
}

RectF Paper::printableFor(bool landscape) const noexcept {
    const SizeF portrait = oriented(false);
    const RectF p = printable.empty() ? RectF{0, 0, portrait.width, portrait.height} : printable;
    if (!landscape) {
        return p;
    }
    if (!printableLandscape.empty()) {
        return printableLandscape;
    }
    // Лист повёрнут против часовой: слева — верхнее поле, сверху — правое, справа — нижнее, снизу — левое.
    const float left = p.x0, top = p.y0, right = portrait.width - p.x1, bottom = portrait.height - p.y1;
    const SizeF o = oriented(true);
    return {top, right, o.width - bottom, o.height - left};
}

Paper Paper::withMargins(SizeF portrait, float margin) noexcept {
    Paper p;
    p.size = portrait;
    const SizeF po = p.oriented(false);
    const SizeF lo = p.oriented(true);
    p.printable = {margin, margin, po.width - margin, po.height - margin};
    p.printableLandscape = {margin, margin, lo.width - margin, lo.height - margin};
    return p;
}

void nupGrid(int perSheet, int& cols, int& rows) noexcept {
    switch (perSheet) {
        case 2:
            cols = 2;
            rows = 1;
            return;
        case 4:
            cols = rows = 2;
            return;
        case 6:
            cols = 3;
            rows = 2;
            return;
        case 9:
            cols = rows = 3;
            return;
        case 16:
            cols = rows = 4;
            return;
        default:
            break;
    }
    if (perSheet <= 1) {
        cols = rows = 1;
        return;
    }
    rows = static_cast<int>(std::sqrt(static_cast<double>(perSheet)));
    while (rows > 1 && perSheet % rows != 0) {
        --rows;
    }
    cols = perSheet / rows;
}

Matrix placementMatrix(const Placement& placement, const RectF& page) noexcept {
    const float w = page.width();
    const float h = page.height();
    Matrix m = Matrix::translate(-page.x0, -page.y0);
    switch (((placement.rotation % 360) + 360) % 360) {
        case 90:
            m = concat(m, Matrix{0, 1, -1, 0, h, 0});  // (x, y) → (h − y, x)
            break;
        case 180:
            m = concat(m, Matrix{-1, 0, 0, -1, w, h});
            break;
        case 270:
            m = concat(m, Matrix{0, -1, 1, 0, 0, w});  // (x, y) → (y, w − x)
            break;
        default:
            break;
    }
    m = concat(m, Matrix::scale(placement.scale));
    return concat(m, Matrix::translate(placement.target.x0, placement.target.y0));
}

std::vector<Sheet> impose(const std::vector<SizeF>& sizes, const std::vector<int>& pages, const Paper& paper,
                          const ImposeOptions& options) {
    std::vector<Sheet> out;
    if (pages.empty()) {
        return out;
    }
    switch (options.layout) {
        case Layout::Single:
            for (const int page : pages) {
                imposeSingle(sizes, page, paper, options, out);
            }
            break;
        case Layout::Multiple:
            imposeMultiple(sizes, pages, paper, options, out);
            break;
        case Layout::Booklet:
            imposeBooklet(sizes, pages, paper, options, out);
            break;
        case Layout::Poster:
            for (const int page : pages) {
                imposePoster(sizes, page, paper, options, out);
            }
            break;
    }
    return out;
}

}  // namespace ot::print
