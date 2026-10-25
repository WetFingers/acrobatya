// Fill & Sign: геометрия бланка, размещение текста, гребёнки и отметок (см. fillsign.h).
#include "core/annot/fillsign.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "core/annot/freetext.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/util/utf8.h"

namespace ot::fillsign {

namespace {

constexpr float kThin = 3;            // тоньше — линия, а не рамка
constexpr float kMinLine = 6;         // короче — не линия (точки, засечки шрифта)
constexpr float kBelow = 6;           // щелчок чуть ниже линии — ещё к ней
constexpr float kReach = 30;          // линия под точкой — не дальше
constexpr float kMinCell = 5;         // клетка ниже — не клетка
constexpr float kMaxCell = 40;        // клетка выше — не клетка
constexpr float kDefaultCell = 20;    // над линией свободно — высота клетки
constexpr float kLineGap = 2;         // базовая линия текста над линией бланка
constexpr float kPad = 2;             // отступ текста в поле FreeText (вид ядра: рамка 0 + 2 пт)
constexpr float kCapHeight = 0.716f;  // высота прописных Arial в долях кегля — центр по высоте клетки
constexpr float kMinStep = 6;         // шаг гребёнки
constexpr float kMaxStep = 30;
constexpr int kMinCombCells = 4;
constexpr float kTextWidth = 200;  // текст без привязки — ширина поля

float luminance(std::uint32_t argb) {
    const float r = static_cast<float>((argb >> 16) & 0xFF);
    const float g = static_cast<float>((argb >> 8) & 0xFF);
    const float b = static_cast<float>(argb & 0xFF);
    return (0.299f * r + 0.587f * g + 0.114f * b) / 255.0f;
}

bool covers(const HLine& h, float x, float slack = 1) { return x >= h.x0 - slack && x <= h.x1 + slack; }

// Доля отрезка [a0, a1], покрытая вертикалью v.
float coverage(const VLine& v, float a0, float a1) {
    const float overlap = std::min(v.y1, a1) - std::max(v.y0, a0);
    return a1 > a0 ? std::max(0.0f, overlap) / (a1 - a0) : 0.0f;
}

float sizeFor(float cell) { return std::clamp(cell * 0.55f, kMinFontSize, kMaxFontSize); }

// Засечки гребёнки у основания y в пределах [x0, x1]: x вертикалей, сходящихся к основанию, по возрастанию
// (рёбра смежных рамок — одна засечка).
std::vector<float> combTicks(const PageGeometry& g, float y, float x0, float x1, float* top) {
    std::vector<std::pair<float, float>> ticks;  // x, верх
    for (const VLine& v : g.vertical) {
        if (std::abs(v.y1 - y) <= 1.5f && v.y1 - v.y0 <= kMaxCell && v.x >= x0 - 2 && v.x <= x1 + 2) {
            ticks.emplace_back(v.x, v.y0);
        }
    }
    std::sort(ticks.begin(), ticks.end());
    std::vector<float> xs;
    float highest = y;
    for (const auto& [x, t] : ticks) {
        if (xs.empty() || x - xs.back() > 2) {
            xs.push_back(x);
        }
        highest = std::min(highest, t);
    }
    if (top != nullptr) {
        *top = highest;
    }
    return xs;
}

// Гребёнка у основания y, в которую попадает x: клетки равного шага (≥ 4); нет — пусто.
std::vector<RectF> combCells(const PageGeometry& g, float y, float x0, float x1, float x,
                             std::optional<float> cellTop) {
    float tickTop = y;
    const std::vector<float> xs = combTicks(g, y, x0, x1, &tickTop);
    if (xs.size() < static_cast<std::size_t>(kMinCombCells) + 1) {
        return {};
    }
    // Ряды засечек равного шага; нужный — тот, где x.
    std::size_t start = 0;
    while (start + 1 < xs.size()) {
        const float step = xs[start + 1] - xs[start];
        std::size_t end = start + 1;
        while (end + 1 < xs.size() &&
               std::abs((xs[end + 1] - xs[end]) - step) <= std::max(1.0f, step * 0.1f)) {
            ++end;
        }
        const bool ok =
            step >= kMinStep && step <= kMaxStep && end - start >= static_cast<std::size_t>(kMinCombCells);
        if (ok && x >= xs[start] - 1 && x <= xs[end] + 1) {
            const float top = cellTop.value_or(tickTop);
            std::vector<RectF> cells;
            for (std::size_t i = start; i < end; ++i) {
                cells.push_back({xs[i], top, xs[i + 1], y});
            }
            return cells;
        }
        start = end;
    }
    return {};
}

}  // namespace

PageGeometry geometryOf(const std::vector<TextVector>& vectors, const std::vector<TextLine>& lines) {
    PageGeometry g;
    // Полутолщина линий бланка: MuPDF расширяет рамку отрезка на толщину с каждой стороны, обводку re S — так
    // же.
    std::vector<float> halves;
    for (const TextVector& v : vectors) {
        if (!v.rectangle) {
            continue;
        }
        const float w = v.bbox.width();
        const float h = v.bbox.height();
        if (h <= kThin && w >= kMinLine) {
            halves.push_back(h / 2);
        } else if (w <= kThin && h >= kMinLine) {
            halves.push_back(w / 2);
        }
    }
    float e = 0.5f;
    if (!halves.empty()) {
        std::nth_element(halves.begin(), halves.begin() + static_cast<std::ptrdiff_t>(halves.size() / 2),
                         halves.end());
        e = halves[halves.size() / 2];
    }
    for (const TextVector& v : vectors) {
        if (!v.rectangle) {
            continue;
        }
        const RectF b = v.bbox;
        const float w = b.width();
        const float h = b.height();
        if (h <= kThin && w >= kMinLine) {
            g.horizontal.push_back({(b.y0 + b.y1) / 2, b.x0 + h / 2, b.x1 - h / 2});
        } else if (w <= kThin && h >= kMinLine - 2) {
            g.vertical.push_back({(b.x0 + b.x1) / 2, b.y0 + w / 2, b.y1 - w / 2});
        } else if (w > kThin && h > kThin &&
                   (v.stroked || (luminance(v.argb) > 0.75f && h <= kMaxCell * 1.5f))) {
            const float inset = v.stroked ? e : 0.0f;
            const RectF r{b.x0 + inset, b.y0 + inset, b.x1 - inset, b.y1 - inset};
            g.horizontal.push_back({r.y0, r.x0, r.x1});
            g.horizontal.push_back({r.y1, r.x0, r.x1});
            g.vertical.push_back({r.x0, r.y0, r.y1});
            g.vertical.push_back({r.x1, r.y0, r.y1});
            if (r.width() >= 6 && r.width() <= 20 && std::abs(r.width() - r.height()) <= 2) {
                g.checks.push_back(r);
            }
        }
    }
    for (const TextLine& l : lines) {
        if (!l.vertical && !l.bbox.empty()) {
            g.text.push_back(l.bbox);
        }
    }
    return g;
}

Placement placeText(const PageGeometry& g, PointF p, float defaultSize) {
    Placement out;
    // 1. Замкнутая клетка вокруг точки: горизонтали сверху и снизу, вертикали по бокам.
    const HLine* above = nullptr;
    const HLine* below = nullptr;
    for (const HLine& h : g.horizontal) {
        if (!covers(h, p.x, 0)) {
            continue;
        }
        if (h.y <= p.y && (above == nullptr || h.y > above->y)) {
            above = &h;
        }
        if (h.y >= p.y && (below == nullptr || h.y < below->y)) {
            below = &h;
        }
    }
    if (above != nullptr && below != nullptr && below->y - above->y >= kMinCell &&
        below->y - above->y <= kMaxCell) {
        const float top = above->y;
        const float bottom = below->y;
        std::optional<float> left;
        std::optional<float> right;
        for (const VLine& v : g.vertical) {
            if (coverage(v, top, bottom) < 0.6f) {
                continue;
            }
            if (v.x <= p.x + 0.5f && (!left || v.x > *left)) {
                left = v.x;
            }
            if (v.x >= p.x - 0.5f && (!right || v.x < *right)) {
                right = v.x;
            }
        }
        if (left && right && *right - *left > kMinCell) {
            const float cell = bottom - top;
            out.cellHeight = cell;
            out.fontSize = sizeFor(cell);
            out.baseline = bottom - (cell - kCapHeight * out.fontSize) / 2;
            // Гребёнка из смежных рамок: засечки по всему ряду у основания клетки.
            out.cells = combCells(g, bottom, -1e9f, 1e9f, p.x, top);
            if (!out.cells.empty()) {
                out.kind = FieldKind::Comb;
                out.field = {out.cells.front().x0, top, out.cells.back().x1, bottom};
                out.fontSize = std::min(out.fontSize, out.cells.front().width() * 1.2f);
            } else {
                out.kind = FieldKind::Box;
                out.field = {*left, top, *right, bottom};
            }
            out.x = out.field.x0 + kPad;
            return out;
        }
    }

    // 2. Линия под точкой (до kReach) или чуть над ней (до kBelow, с весом: ближе нижняя).
    const HLine* line = nullptr;
    float best = 0;
    for (const HLine& h : g.horizontal) {
        if (!covers(h, p.x)) {
            continue;
        }
        float cost = -1;
        if (h.y >= p.y && h.y - p.y <= kReach) {
            cost = h.y - p.y;
        } else if (h.y < p.y && p.y - h.y <= kBelow) {
            cost = (p.y - h.y) * 4;
        }
        if (cost >= 0 && (line == nullptr || cost < best)) {
            line = &h;
            best = cost;
        }
    }
    if (line == nullptr) {
        out.fontSize = std::clamp(defaultSize, kMinFontSize, 72.0f);
        out.x = p.x;
        out.baseline = p.y + 0.35f * out.fontSize;
        out.field = {p.x, out.baseline - out.fontSize, p.x + kTextWidth, out.baseline};
        return out;
    }
    const float bottom = line->y;
    // Потолок клетки: горизонталь или строка текста выше над той же линией.
    float ceiling = bottom - kDefaultCell;
    float nearest = -1e9f;
    for (const HLine& h : g.horizontal) {
        if (&h != line && h.y < bottom - kMinCell && bottom - h.y <= kMaxCell && covers(h, p.x, 0)) {
            nearest = std::max(nearest, h.y);
        }
    }
    for (const RectF& t : g.text) {
        if (t.y1 <= bottom - 2 && bottom - t.y1 <= kMaxCell && t.x1 > line->x0 && t.x0 < line->x1 &&
            t.x0 <= p.x + kTextWidth && t.x1 >= p.x - 4) {
            nearest = std::max(nearest, t.y1);
        }
    }
    if (nearest > -1e8f) {
        ceiling = nearest;
    }
    const float cell = bottom - ceiling;
    out.cells = combCells(g, bottom, line->x0, line->x1, p.x, std::nullopt);
    if (!out.cells.empty()) {
        // Засечки короче строки — клетка не ниже 1,25 шага.
        const float step = out.cells.front().width();
        out.kind = FieldKind::Comb;
        out.cellHeight = std::max(bottom - out.cells.front().y0, step * 1.25f);
        out.fontSize = std::min(sizeFor(out.cellHeight), step * 1.2f);
        out.baseline = bottom - kLineGap;
        out.field = {out.cells.front().x0, bottom - out.cellHeight, out.cells.back().x1, bottom};
        out.x = out.field.x0 + kPad;
        return out;
    }
    out.kind = FieldKind::Line;
    out.cellHeight = cell;
    out.fontSize = sizeFor(cell);
    out.baseline = bottom - kLineGap;
    out.field = {line->x0, ceiling, line->x1, bottom};
    out.x = std::clamp(p.x, line->x0, std::max(line->x0, line->x1 - 10));
    return out;
}

Placement placeMark(const PageGeometry& g, PointF p, float size) {
    Placement out;
    for (const RectF& c : g.checks) {
        if (p.x >= c.x0 - 3 && p.x <= c.x1 + 3 && p.y >= c.y0 - 3 && p.y <= c.y1 + 3) {
            out.kind = FieldKind::Check;
            out.field = c;
            out.cellHeight = c.height();
            out.fontSize = c.height() * 0.85f;
            out.x = c.x0;
            out.baseline = c.y1;
            return out;
        }
    }
    out.fontSize = size;
    out.cellHeight = size;
    out.field = {p.x - size / 2, p.y - size / 2, p.x + size / 2, p.y + size / 2};
    out.x = out.field.x0;
    out.baseline = out.field.y1;
    return out;
}

std::string markText(Mark mark) {
    switch (mark) {
        case Mark::Check:
            return "✓";
        case Mark::Cross:
            return "✕";
        case Mark::Dot:
            return "●";
        case Mark::Line:
            return "—";
        case Mark::Box:
            return "▭";
    }
    return {};
}

RectF textBoxRect(std::string_view family, float x0, float x1, float baseline, float fontSize) {
    const annot::FontMetrics m = annot::fontMetrics(family);
    const float top = baseline - kPad - m.ascender * fontSize;
    const float bottom = baseline - m.descender * fontSize + kPad;
    return {x0 - kPad, top, std::max(x1, x0 + 10), bottom};
}

namespace {

annot::AnnotProps freeText(std::string text, const RectF& rect, const char* font, float size,
                           annot::AnnotColor color, int align) {
    annot::AnnotProps p;
    p.type = annot::AnnotType::FreeText;
    p.rect = rect;
    p.contents = std::move(text);
    p.fontName = font;
    p.fontSize = size;
    p.textColor = color.empty() ? annot::AnnotColor::gray(0) : color;
    p.color = {};  // без заливки
    p.borderWidth = 0;
    p.align = align;
    p.intent = kTypeWriterIntent;
    return p;
}

}  // namespace

annot::AnnotProps textProps(const Placement& placement, std::string text, annot::AnnotColor color,
                            float pageWidth) {
    float x1 = placement.field.x1;
    if (!placement.snapped()) {
        x1 = std::min(pageWidth, placement.x + kTextWidth);
    }
    const RectF rect = textBoxRect(kTextFont, placement.x, std::max(x1, placement.x + 20), placement.baseline,
                                   placement.fontSize);
    return freeText(std::move(text), rect, kTextFont, placement.fontSize, color, 0);
}

std::vector<annot::AnnotProps> combProps(const Placement& placement, std::u32string_view text,
                                         annot::AnnotColor color) {
    std::vector<annot::AnnotProps> out;
    for (std::size_t i = 0; i < text.size() && i < placement.cells.size(); ++i) {
        if (text[i] == U' ') {
            continue;
        }
        const RectF& cell = placement.cells[i];
        RectF rect = textBoxRect(kCombFont, cell.x0 + kPad, cell.x1, placement.baseline, placement.fontSize);
        rect.x1 = cell.x1;  // поле — клетка: центр глифа — центр клетки
        std::string ch;
        utf8::append(ch, text[i]);
        out.push_back(freeText(std::move(ch), rect, kCombFont, placement.fontSize, color, 1));
    }
    return out;
}

annot::AnnotProps markProps(Mark mark, const Placement& placement, annot::AnnotColor color) {
    const std::string text = markText(mark);
    const std::u32string code = utf8::decode(text);
    const float s = placement.fontSize;
    const annot::FontMetrics m = annot::fontMetrics(kMarkFont);
    const annot::GlyphMetrics g = annot::glyphMetrics(kMarkFont, code.empty() ? U' ' : code.front());
    const float cx = (placement.field.x0 + placement.field.x1) / 2;
    const float cy = (placement.field.y0 + placement.field.y1) / 2;
    const float advance = g.found ? g.advance : 1.0f;
    const float gx = g.found ? (g.x0 + g.x1) / 2 : advance / 2;  // центр глифа от начала, доли кегля
    const float gy = g.found ? (g.y0 + g.y1) / 2 : 0.35f;        // центр глифа над базовой линией
    // Поле по центру: ширина с запасом, текст — по центру поля (сдвиг на центр глифа относительно ширины).
    const float width = (advance + 0.4f) * s + 2 * kPad;
    const float x0 = cx - width / 2 + (advance / 2 - gx) * s;
    const float baseline = cy + gy * s;
    const float top = baseline - kPad - m.ascender * s;
    const float bottom = baseline - m.descender * s + kPad;
    return freeText(text, {x0, top, x0 + width, bottom}, kMarkFont, s, color, 1);
}

std::vector<annot::AnnotRef> createAll(Document& doc, int page, const std::vector<annot::AnnotProps>& items,
                                       const std::string& operation) {
    std::vector<annot::AnnotRef> refs;
    Journal::Operation op(doc, operation);
    for (const annot::AnnotProps& item : items) {
        refs.push_back(annot::create(doc, page, item, operation));
    }
    op.invalidate(page);
    op.commit();
    return refs;
}

}  // namespace ot::fillsign
