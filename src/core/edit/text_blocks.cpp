#include "core/edit/text_blocks.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <utility>

#include "core/util/utf8.h"

namespace ot::edit {

// Свои имена вместо безымянного пространства: в unity-сборке ottisk_core безымянные пространства соседних
// файлов (content_model.cpp, stext.cpp) — одно, и вспомогательные функции с теми же именами столкнулись бы.
namespace blocks_impl {

constexpr float kDegree = 3.14159265358979f / 180.0f;

Matrix invert(const Matrix& m) noexcept {
    const float det = m.a * m.d - m.b * m.c;
    if (std::fabs(det) < 1e-12f) {
        return Matrix{};
    }
    const float r = 1.0f / det;
    Matrix i{m.d * r, -m.b * r, -m.c * r, m.a * r, 0, 0};
    i.e = -(m.e * i.a + m.f * i.c);
    i.f = -(m.e * i.b + m.f * i.d);
    return i;
}

PointF apply(PointF p, const Matrix& m) noexcept {
    return {p.x * m.a + p.y * m.c + m.e, p.x * m.b + p.y * m.d + m.f};
}

RectF unite(const RectF& r, const RectF& q) noexcept {
    if (!r.valid()) {
        return q;
    }
    if (!q.valid()) {
        return r;
    }
    return {std::fmin(r.x0, q.x0), std::fmin(r.y0, q.y0), std::fmax(r.x1, q.x1), std::fmax(r.y1, q.y1)};
}

float median(std::vector<float> v) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

float spread(const std::vector<float>& v) {
    if (v.size() < 2) {
        return 0;
    }
    const auto [lo, hi] = std::minmax_element(v.begin(), v.end());
    return *hi - *lo;
}

bool isSpace(char32_t c) noexcept {
    return c == U' ' || c == 0x00A0 || c == U'\t' || (c >= 0x2000 && c <= 0x200B);
}

// Маркер пункта списка в начале строки: знак маркера или номер «1.», «12)», «a)».
bool startsWithMarker(const std::u32string& text) {
    std::size_t i = 0;
    while (i < text.size() && isSpace(text[i])) {
        ++i;
    }
    if (i >= text.size()) {
        return false;
    }
    switch (text[i]) {
        case 0x2022:  // •
        case 0x25CF:  // ●
        case 0x25E6:  // ◦
        case 0x25AA:  // ▪
        case 0x25A0:  // ■
        case 0x2023:  // ‣
        case 0x2043:  // ⁃
        case 0x00B7:  // ·
        case 0xF0B7:  // маркер Symbol (Word)
        case 0xF0A7:  // квадрат Wingdings
        case 0xF076:
        case 0xF0D8:
        case 0xF0FC:
            return true;
        default:
            break;
    }
    std::size_t j = i;
    while (j < text.size() && j - i < 3 && text[j] >= U'0' && text[j] <= U'9') {
        ++j;
    }
    if (j == i && j < text.size() &&
        ((text[j] >= U'a' && text[j] <= U'z') || (text[j] >= 0x0430 && text[j] <= 0x044F))) {
        ++j;  // «a)», «б)» (строчная латинская или кириллическая буква)
        return j < text.size() && text[j] == U')' && (j + 1 == text.size() || isSpace(text[j + 1]));
    }
    return j > i && j < text.size() && (text[j] == U'.' || text[j] == U')') &&
           (j + 1 == text.size() || isSpace(text[j + 1]));
}

// Спан в раскладке: пространство пользователя страницы, y вниз.
struct SpanBox {
    int span = -1;
    int source = -1;  // −1 — поток страницы, иначе поток формы (не смешиваются)
    bool horizontal = true;
    float x0 = 0, x1 = 0, y = 0;  // начало, конец по базовой линии, базовая линия (без Ts)
    float u0 = 0, u1 = 0, v = 0;  // повёрнутый: вдоль и поперёк направления текста
    float angle = 0;
    float size = 1;
    bool blank = false;  // только пробелы: в строке есть, но границ строки не задаёт
    RectF ink;           // рамки глифов без пробелов, координаты страницы
};

struct LineBuild {
    std::vector<int> boxes;  // индексы SpanBox
    int source = -1;
    bool horizontal = true;
    float angle = 0;
    float x0 = 0, x1 = 0, y = 0, size = 0;  // повёрнутый: u0/u1/v
    std::u32string text;                    // для маркеров
    bool blank = true;                      // одни пробелы
};

struct BlockBuild {
    std::vector<int> lines;  // индексы LineBuild
    int source = -1;
    bool horizontal = true;
    float left = 0, right = 0;  // пределы по X
    float size = 0;
};

class Builder {
public:
    Builder(const ContentModel& model, const BlockOptions& options)
        : m_(model), o_(options), toUser_(invert(model.pageMatrix())) {}

    std::vector<TextBlock> run() {
        collect();
        buildLines();
        buildParagraphs();
        return finish();
    }

private:
    PointF layout(PointF page) const {
        const PointF u = apply(page, toUser_);
        return {u.x, -u.y};
    }
    PointF toPage(float x, float y) const { return apply({x, -y}, m_.pageMatrix()); }

    void collect() {
        for (std::size_t i = 0; i < m_.spans().size(); ++i) {
            const TextSpan& s = m_.spans()[i];
            if (s.hidden || s.glyphCount == 0) {
                continue;  // выключенный слой в блоки не входит
            }
            SpanBox b;
            b.span = static_cast<int>(i);
            b.source = s.form ? s.stream : -1;
            b.size = s.fontSize > 0.01f ? s.fontSize : 1.0f;
            b.angle = s.angle;
            b.horizontal =
                s.reason != ReadOnly::Rotated && s.reason != ReadOnly::Vertical && std::fabs(s.angle) < 0.5f;
            // Границы — по видимым глифам: пробелы в начале и конце спана (Word ставит пробел в конце строки)
            // рамку не расширяют.
            int first = -1;
            int last = -1;
            b.ink = RectF::none();
            for (int g = s.firstGlyph; g < s.firstGlyph + s.glyphCount; ++g) {
                const Glyph& gl = m_.glyphs()[static_cast<std::size_t>(g)];
                if (!gl.unicode.empty() && isSpace(gl.unicode[0])) {
                    continue;
                }
                first = first < 0 ? g : first;
                last = g;
                b.ink = unite(b.ink, gl.quad.bounds());
            }
            b.blank = first < 0;
            PointF visibleStart = s.origin;
            PointF visibleEnd = s.end;
            if (!b.blank) {
                const Glyph& a = m_.glyphs()[static_cast<std::size_t>(first)];
                const Glyph& z = m_.glyphs()[static_cast<std::size_t>(last)];
                visibleStart = a.origin;
                const float k = z.w0;
                visibleEnd = {z.origin.x + k * (z.quad.lr.x - z.quad.ll.x) / (k != 0 ? k : 1.0f),
                              z.origin.y + k * (z.quad.lr.y - z.quad.ll.y) / (k != 0 ? k : 1.0f)};
            }
            const PointF start = layout(s.baseline);
            const PointF origin = layout(visibleStart);
            const PointF end = layout(visibleEnd);
            if (b.horizontal) {
                b.x0 = origin.x;
                b.x1 = std::max(end.x, origin.x);
                b.y = start.y;
            } else {
                // Направление в раскладке (y вниз): угол пользователя с обратным знаком.
                const float a = -s.angle * kDegree;
                const PointF dir{std::cos(a), std::sin(a)};
                const PointF nrm{-dir.y, dir.x};
                b.u0 = origin.x * dir.x + origin.y * dir.y;
                b.u1 = end.x * dir.x + end.y * dir.y;
                if (b.u1 < b.u0) {
                    std::swap(b.u0, b.u1);
                }
                b.v = start.x * nrm.x + start.y * nrm.y;
            }
            boxes_.push_back(b);
        }
    }

    void buildLines() {
        std::vector<int> order(boxes_.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const SpanBox& p = boxes_[static_cast<std::size_t>(a)];
            const SpanBox& q = boxes_[static_cast<std::size_t>(b)];
            if (p.horizontal != q.horizontal) {
                return p.horizontal;
            }
            if (p.horizontal) {
                return p.y != q.y ? p.y < q.y : p.x0 < q.x0;
            }
            if (std::fabs(p.angle - q.angle) > 0.5f) {
                return p.angle < q.angle;
            }
            return p.v != q.v ? p.v < q.v : p.u0 < q.u0;
        });
        for (int bi : order) {
            const SpanBox& b = boxes_[static_cast<std::size_t>(bi)];
            const float y = b.horizontal ? b.y : b.v;
            const float x0 = b.horizontal ? b.x0 : b.u0;
            const float x1 = b.horizontal ? b.x1 : b.u1;
            int best = -1;
            // Строки идут по y: подходящая — среди последних с близкой базовой линией.
            for (int li = static_cast<int>(lines_.size()) - 1; li >= 0; --li) {
                LineBuild& l = lines_[static_cast<std::size_t>(li)];
                if (l.horizontal != b.horizontal) {
                    break;
                }
                const float size = std::max(l.size, b.size);
                if (y - l.y > 0.35f * size) {
                    break;  // дальше — только строки выше
                }
                if (l.source != b.source || (!b.horizontal && std::fabs(l.angle - b.angle) > 0.5f) ||
                    std::fabs(l.y - y) > 0.35f * size) {
                    continue;
                }
                const float gap = std::max({x0 - l.x1, l.x0 - x1, 0.0f});
                if (gap <= o_.lineGap * size) {
                    best = li;
                    break;
                }
            }
            if (best < 0) {
                LineBuild l;
                l.source = b.source;
                l.horizontal = b.horizontal;
                l.angle = b.angle;
                l.x0 = x0;
                l.x1 = x1;
                l.y = y;
                l.size = b.size;
                l.blank = b.blank;
                lines_.push_back(l);
                best = static_cast<int>(lines_.size()) - 1;
            }
            LineBuild& l = lines_[static_cast<std::size_t>(best)];
            l.boxes.push_back(bi);
            if (!b.blank) {
                if (l.blank) {
                    l.x0 = x0;
                    l.x1 = x1;
                    l.blank = false;
                } else {
                    l.x0 = std::min(l.x0, x0);
                    l.x1 = std::max(l.x1, x1);
                }
            }
        }
        for (LineBuild& l : lines_) {
            std::stable_sort(l.boxes.begin(), l.boxes.end(), [&](int a, int b) {
                const SpanBox& p = boxes_[static_cast<std::size_t>(a)];
                const SpanBox& q = boxes_[static_cast<std::size_t>(b)];
                return (p.horizontal ? p.x0 : p.u0) < (q.horizontal ? q.x0 : q.u0);
            });
            // Кегль строки — у спана с наибольшим числом глифов; базовая линия — у него же.
            int best = l.boxes.front();
            for (int bi : l.boxes) {
                if (boxes_[static_cast<std::size_t>(best)].blank &&
                    !boxes_[static_cast<std::size_t>(bi)].blank) {
                    best = bi;
                    continue;
                }
                if (boxes_[static_cast<std::size_t>(bi)].blank) {
                    continue;
                }
                const int n = m_.spans()[static_cast<std::size_t>(boxes_[static_cast<std::size_t>(bi)].span)]
                                  .glyphCount;
                const int nb =
                    m_.spans()[static_cast<std::size_t>(boxes_[static_cast<std::size_t>(best)].span)]
                        .glyphCount;
                if (n > nb) {
                    best = bi;
                }
            }
            const SpanBox& main = boxes_[static_cast<std::size_t>(best)];
            l.size = main.size;
            l.y = main.horizontal ? main.y : main.v;
            for (int bi : l.boxes) {
                const TextSpan& s =
                    m_.spans()[static_cast<std::size_t>(boxes_[static_cast<std::size_t>(bi)].span)];
                for (int g = s.firstGlyph; g < s.firstGlyph + s.glyphCount && l.text.size() < 8; ++g) {
                    l.text += m_.glyphs()[static_cast<std::size_t>(g)].unicode;
                }
            }
        }
    }

    // Строки горизонтального абзаца (кроме первой) начинаются у левого края.
    bool leftAligned(const BlockBuild& b, float tol) const {
        for (std::size_t i = 1; i < b.lines.size(); ++i) {
            if (lines_[static_cast<std::size_t>(b.lines[i])].x0 - b.left > tol) {
                return false;
            }
        }
        return true;
    }

    float blockLeading(const BlockBuild& b) const {
        std::vector<float> d;
        for (std::size_t i = 1; i < b.lines.size(); ++i) {
            d.push_back(lines_[static_cast<std::size_t>(b.lines[i])].y -
                        lines_[static_cast<std::size_t>(b.lines[i - 1])].y);
        }
        return median(d);
    }

    void buildParagraphs() {
        std::vector<int> order(lines_.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            const LineBuild& p = lines_[static_cast<std::size_t>(a)];
            const LineBuild& q = lines_[static_cast<std::size_t>(b)];
            if (p.horizontal != q.horizontal) {
                return p.horizontal;
            }
            return p.y != q.y ? p.y < q.y : p.x0 < q.x0;
        });
        for (int li : order) {
            const LineBuild& l = lines_[static_cast<std::size_t>(li)];
            if (l.blank) {
                continue;  // строка из одних пробелов (пустой абзац Word) — невидима, блока не даёт
            }
            int best = -1;
            float bestGap = 0;
            if (l.horizontal) {
                for (std::size_t bi = 0; bi < blocks_.size(); ++bi) {
                    BlockBuild& b = blocks_[bi];
                    if (!b.horizontal || b.source != l.source) {
                        continue;
                    }
                    const LineBuild& last = lines_[static_cast<std::size_t>(b.lines.back())];
                    const float size = std::max(l.size, b.size);
                    const float gap = l.y - last.y;
                    if (gap <= 0.35f * size || gap > o_.paragraphGap * size) {
                        continue;
                    }
                    if (b.lines.size() >= 2 && gap > o_.leadingJump * blockLeading(b) + 0.5f) {
                        continue;
                    }
                    if (std::min(l.x1, b.right) - std::max(l.x0, b.left) <= 0) {
                        continue;  // другая колонка
                    }
                    if (best < 0 || gap < bestGap) {
                        best = static_cast<int>(bi);
                        bestGap = gap;
                    }
                }
                if (best >= 0 && breaksParagraph(blocks_[static_cast<std::size_t>(best)], l)) {
                    best = -1;
                }
            }
            if (best < 0) {
                BlockBuild b;
                b.source = l.source;
                b.horizontal = l.horizontal;
                b.left = l.x0;
                b.right = l.x1;
                b.size = l.size;
                blocks_.push_back(b);
                best = static_cast<int>(blocks_.size()) - 1;
            }
            BlockBuild& b = blocks_[static_cast<std::size_t>(best)];
            b.lines.push_back(li);
            b.left = std::min(b.left, l.x0);
            b.right = std::max(b.right, l.x1);
        }
    }

    bool breaksParagraph(const BlockBuild& b, const LineBuild& l) const {
        const float size = std::max(l.size, b.size);
        if (std::fabs(l.size - b.size) > 0.2f * size) {
            return true;  // смена кегля — заголовок, подпись
        }
        if (startsWithMarker(l.text)) {
            return true;  // пункт списка
        }
        // Отступ первой строки: блок выровнен влево, прошлая строка короткая, новая начинается с отступом.
        const float tol = 0.5f * size;
        const LineBuild& prev = lines_[static_cast<std::size_t>(b.lines.back())];
        return b.lines.size() >= 2 && l.x0 - b.left > tol && prev.x1 < b.right - size && leftAligned(b, tol);
    }

    // Растяжка пробелов строки в долях кегля: сдвиг сверх ширины пробела (Tw, TJ) — внутри спанов и на
    // стыках.
    // Растяжка пробелов строки: средний сдвиг сверх ширины пробела (Tw, TJ) на пробел внутри строки, в долях
    // кегля.
    float stretchOf(const BlockLine& line) const {
        float extra = 0;
        float size = 0;
        int spaces = 0;
        const TextSpan* prev = nullptr;
        for (int si : line.spans) {
            const TextSpan& s = m_.spans()[static_cast<std::size_t>(si)];
            size = std::max(size, s.fontSize);
            const float toPage = s.size != 0 ? s.fontSize / std::fabs(s.size) : 1.0f;
            for (int g = s.firstGlyph; g + 1 < s.firstGlyph + s.glyphCount; ++g) {
                const Glyph& a = m_.glyphs()[static_cast<std::size_t>(g)];
                const Glyph& b = m_.glyphs()[static_cast<std::size_t>(g + 1)];
                if (a.unicode.empty() || !isSpace(a.unicode[0])) {
                    continue;
                }
                if (!b.unicode.empty() && isSpace(b.unicode[0])) {
                    continue;  // сдвиг считается у последнего пробела подряд
                }
                const float natural = (a.w0 * s.size + s.charSpace) * s.scale;
                extra += (b.x - a.x - natural) * toPage;
                ++spaces;
            }
            if (prev != nullptr && prev->glyphCount > 0) {
                const Glyph& last =
                    m_.glyphs()[static_cast<std::size_t>(prev->firstGlyph + prev->glyphCount - 1)];
                if (!last.unicode.empty() && isSpace(last.unicode[0])) {
                    extra += layout(s.origin).x - layout(prev->end).x;
                    ++spaces;
                }
            }
            prev = &s;
        }
        return size > 0 && spaces > 0 ? std::max(0.0f, extra / static_cast<float>(spaces) / size) : 0.0f;
    }

    std::string lineText(const BlockLine& line) const {
        std::string out;
        const TextSpan* prev = nullptr;
        std::string prevText;
        for (int si : line.spans) {
            const TextSpan& s = m_.spans()[static_cast<std::size_t>(si)];
            const std::string text = m_.spanText(si);
            if (prev != nullptr) {
                // Тень и «ложный жирный»: тот же текст почти в той же точке — один раз.
                if (text == prevText && std::hypot(s.origin.x - prev->origin.x, s.origin.y - prev->origin.y) <
                                            0.3f * s.fontSize) {
                    continue;
                }
                const float gap = layout(s.origin).x - layout(prev->end).x;
                const bool spaced =
                    (!out.empty() && out.back() == ' ') || (!text.empty() && text.front() == ' ');
                if (!spaced && gap > 0.25f * std::max(s.fontSize, prev->fontSize)) {
                    out += ' ';
                }
            }
            out += text;
            prev = &s;
            prevText = text;
        }
        return out;
    }

    void alignOf(TextBlock& t, const BlockBuild& b, float columnLeft, float columnRight) const {
        const float tol = std::max(1.0f, 0.3f * t.fontSize);
        const std::size_t n = t.lines.size();
        if (!t.horizontal) {
            t.align = Align::Left;
            return;
        }
        if (n == 1) {
            const BlockLine& l = t.lines[0];
            if (std::fabs(l.x0 - columnLeft) <= tol) {
                t.align = Align::Left;
            } else if (std::fabs(l.x1 - columnRight) <= tol) {
                t.align = Align::Right;
            } else if (std::fabs((l.x0 + l.x1) / 2 - (columnLeft + columnRight) / 2) <= tol) {
                t.align = Align::Center;
            } else {
                t.align = Align::Left;
            }
            return;
        }
        std::vector<float> left;
        std::vector<float> rightButLast;
        std::vector<float> right;
        std::vector<float> center;
        for (std::size_t i = 0; i < n; ++i) {
            const BlockLine& l = t.lines[i];
            if (i > 0 || l.x0 - b.left <= tol) {
                left.push_back(l.x0);  // отступ первой строки не мешает «влево»
            }
            if (i + 1 < n) {
                rightButLast.push_back(l.x1);
            }
            right.push_back(l.x1);
            center.push_back((l.x0 + l.x1) / 2);
        }
        // По ширине: строки, кроме последней, растянуты или доходят до правого края (самая длинная строка
        // набора — без растяжки), и хоть одна растянута.
        const float rightEdge = *std::max_element(rightButLast.begin(), rightButLast.end());
        bool stretched = false;
        bool full = true;
        for (std::size_t i = 0; i + 1 < n; ++i) {
            const bool wide = t.lines[i].stretch > 0.02f;
            stretched = stretched || wide;
            full = full && (wide || std::fabs(t.lines[i].x1 - rightEdge) <= tol);
        }
        stretched = stretched && full;
        const bool leftEven = spread(left) <= tol;
        if (stretched && leftEven && spread(rightButLast) <= tol) {
            t.align = Align::Justify;
        } else if (leftEven) {
            t.align = Align::Left;
        } else if (spread(right) <= tol) {
            t.align = Align::Right;
        } else if (spread(center) <= tol) {
            t.align = Align::Center;
        } else {
            t.align = Align::Left;
        }
    }

    std::vector<TextBlock> finish() {
        std::vector<TextBlock> out;
        out.reserve(blocks_.size());
        for (const BlockBuild& b : blocks_) {
            TextBlock t;
            t.horizontal = b.horizontal;
            t.bbox = RectF::none();
            std::vector<float> sizes;
            ReadOnly reason = ReadOnly::None;
            for (int li : b.lines) {
                const LineBuild& lb = lines_[static_cast<std::size_t>(li)];
                BlockLine line;
                line.bbox = RectF::none();
                for (int bi : lb.boxes) {
                    const int si = boxes_[static_cast<std::size_t>(bi)].span;
                    const TextSpan& s = m_.spans()[static_cast<std::size_t>(si)];
                    line.spans.push_back(si);
                    t.spans.push_back(si);
                    line.bbox = unite(line.bbox, boxes_[static_cast<std::size_t>(bi)].ink);
                    if (s.reason != ReadOnly::None && (reason == ReadOnly::None || s.reason < reason)) {
                        reason = s.reason;
                    }
                }
                line.size = lb.size;
                line.x0 = lb.x0;
                line.x1 = lb.x1;
                line.y = lb.y;
                if (b.horizontal) {
                    line.start = toPage(lb.x0, lb.y);
                    line.end = toPage(lb.x1, lb.y);
                } else {
                    const TextSpan& first = m_.spans()[static_cast<std::size_t>(line.spans.front())];
                    const TextSpan& last = m_.spans()[static_cast<std::size_t>(line.spans.back())];
                    line.start = first.baseline;
                    line.end = last.end;
                }
                line.baseline = line.start.y;
                line.stretch = stretchOf(line);
                t.bbox = unite(t.bbox, line.bbox);
                sizes.push_back(lb.size);
                t.lines.push_back(std::move(line));
            }
            t.reason = reason;
            t.fontSize = median(sizes);
            std::vector<float> d;
            for (std::size_t i = 1; i < t.lines.size(); ++i) {
                d.push_back(t.lines[i].y - t.lines[i - 1].y);
            }
            t.leading = median(d);
            t.firstIndent = t.lines.front().x0 - b.left;
            // Текст: строки через пробел; после дефиса — без пробела.
            for (std::size_t i = 0; i < t.lines.size(); ++i) {
                const std::string lt = lineText(t.lines[i]);
                if (i > 0 && !t.text.empty()) {
                    const std::u32string tail =
                        utf8::decode(t.text.substr(t.text.size() - std::min<std::size_t>(t.text.size(), 3)));
                    const char32_t c = tail.empty() ? 0 : tail.back();
                    if (c != U'-' && c != 0x00AD && c != 0x2010 && c != U' ') {
                        t.text += ' ';
                    }
                }
                t.text += lt;
            }
            while (!t.text.empty() && t.text.back() == ' ') {
                t.text.pop_back();
            }
            while (!t.text.empty() && t.text.front() == ' ') {
                t.text.erase(t.text.begin());
            }
            // Пробеги стилей по порядку чтения.
            for (int si : t.spans) {
                const TextSpan& s = m_.spans()[static_cast<std::size_t>(si)];
                if (t.runs.empty() || t.runs.back().font != s.font || t.runs.back().size != s.size ||
                    t.runs.back().fill != s.fill || t.runs.back().render != s.render ||
                    t.runs.back().rise != s.rise) {
                    StyleRun r;
                    r.font = s.font;
                    r.size = s.size;
                    r.fill = s.fill;
                    r.render = s.render;
                    r.rise = s.rise;
                    t.runs.push_back(std::move(r));
                }
                t.runs.back().spans.push_back(si);
                t.runs.back().text += m_.spanText(si);
            }
            out.push_back(std::move(t));
        }
        // Колонки — связные группы горизонтальных блоков, пересекающихся по X не меньше чем наполовину ширины
        // узкого (разделы с разным числом колонок не сливаются в одну); порядок — слева направо.
        auto sameColumn = [&](std::size_t j, std::size_t k) {
            const float overlap =
                std::min(blocks_[j].right, blocks_[k].right) - std::max(blocks_[j].left, blocks_[k].left);
            const float narrow =
                std::min(blocks_[j].right - blocks_[j].left, blocks_[k].right - blocks_[k].left);
            return blocks_[j].source == blocks_[k].source && overlap > 0 && overlap >= 0.5f * narrow;
        };
        std::vector<int> group(out.size(), -1);
        int groups = 0;
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (!out[i].horizontal || group[i] >= 0) {
                continue;
            }
            group[i] = groups;
            std::vector<std::size_t> stack{i};
            while (!stack.empty()) {
                const std::size_t k = stack.back();
                stack.pop_back();
                for (std::size_t j = 0; j < out.size(); ++j) {
                    if (group[j] < 0 && out[j].horizontal && sameColumn(j, k)) {
                        group[j] = groups;
                        stack.push_back(j);
                    }
                }
            }
            ++groups;
        }
        std::vector<float> groupLeft(static_cast<std::size_t>(groups), 1e9f);
        std::vector<float> groupRight(static_cast<std::size_t>(groups), -1e9f);
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (group[i] >= 0) {
                groupLeft[static_cast<std::size_t>(group[i])] =
                    std::min(groupLeft[static_cast<std::size_t>(group[i])], blocks_[i].left);
                groupRight[static_cast<std::size_t>(group[i])] =
                    std::max(groupRight[static_cast<std::size_t>(group[i])], blocks_[i].right);
            }
        }
        std::vector<int> rank(static_cast<std::size_t>(groups));
        std::iota(rank.begin(), rank.end(), 0);
        std::sort(rank.begin(), rank.end(), [&](int a, int b) {
            return groupLeft[static_cast<std::size_t>(a)] < groupLeft[static_cast<std::size_t>(b)];
        });
        std::vector<int> column(static_cast<std::size_t>(groups));
        for (int r = 0; r < groups; ++r) {
            column[static_cast<std::size_t>(rank[static_cast<std::size_t>(r)])] = r;
        }
        for (std::size_t i = 0; i < out.size(); ++i) {
            if (group[i] >= 0) {
                out[i].column = column[static_cast<std::size_t>(group[i])];
                alignOf(out[i], blocks_[i], groupLeft[static_cast<std::size_t>(group[i])],
                        groupRight[static_cast<std::size_t>(group[i])]);
            } else {
                out[i].column = groups;
                alignOf(out[i], blocks_[i], 0, 0);
            }
        }
        // Порядок чтения: горизонтальные по колонкам сверху вниз, затем повёрнутые.
        std::vector<std::size_t> idx(out.size());
        std::iota(idx.begin(), idx.end(), std::size_t{0});
        std::stable_sort(idx.begin(), idx.end(), [&](std::size_t a, std::size_t b) {
            if (out[a].horizontal != out[b].horizontal) {
                return out[a].horizontal;
            }
            if (out[a].column != out[b].column) {
                return out[a].column < out[b].column;
            }
            return out[a].lines.front().y < out[b].lines.front().y;
        });
        std::vector<TextBlock> sorted;
        sorted.reserve(out.size());
        for (std::size_t i : idx) {
            sorted.push_back(std::move(out[i]));
        }
        return sorted;
    }

    const ContentModel& m_;
    BlockOptions o_;
    Matrix toUser_;
    std::vector<SpanBox> boxes_;
    std::vector<LineBuild> lines_;
    std::vector<BlockBuild> blocks_;
};

}  // namespace blocks_impl

const char* alignCode(Align align) noexcept {
    switch (align) {
        case Align::Left:
            return "left";
        case Align::Right:
            return "right";
        case Align::Center:
            return "center";
        case Align::Justify:
            return "justify";
    }
    return "?";
}

std::vector<TextBlock> blocksForPage(const ContentModel& model, const BlockOptions& options) {
    return blocks_impl::Builder(model, options).run();
}

std::vector<UnderlineMark> underlineMarks(const ContentModel& model, int textObject) {
    std::vector<UnderlineMark> out;
    if (textObject < 0 || textObject >= static_cast<int>(model.textObjects().size())) {
        return out;
    }
    const TextObject& t = model.textObjects()[static_cast<std::size_t>(textObject)];
    if (t.end < 0 || t.spans.empty()) {
        return out;
    }
    const Matrix ctm = model.spans()[static_cast<std::size_t>(t.spans.front())].ctm;
    const auto& ops = model.operations();
    const int stream = ops[static_cast<std::size_t>(t.end)].stream;
    int k = t.end + 1;
    while (k < static_cast<int>(ops.size()) && ops[static_cast<std::size_t>(k)].stream == stream &&
           ops[static_cast<std::size_t>(k)].kind == OpKind::BeginMarked) {
        const Operation& begin = ops[static_cast<std::size_t>(k)];
        if (begin.operands.empty() || begin.operands.front().kind != Operand::Kind::Name ||
            begin.operands.front().text != "OttU") {
            break;
        }
        UnderlineMark mark;
        mark.first = k;
        int depth = 0;
        for (; k < static_cast<int>(ops.size()) && ops[static_cast<std::size_t>(k)].stream == stream; ++k) {
            const Operation& o = ops[static_cast<std::size_t>(k)];
            if (o.kind == OpKind::BeginMarked) {
                ++depth;
            } else if (o.kind == OpKind::EndMarked && --depth == 0) {
                mark.last = k;
                ++k;
                break;
            } else if (o.name == "re" && o.operands.size() >= 4) {
                const float x = static_cast<float>(o.operands[0].number);
                const float y = static_cast<float>(o.operands[1].number);
                const float w = static_cast<float>(o.operands[2].number);
                const float h = static_cast<float>(o.operands[3].number);
                RectF r = RectF::none();
                for (const PointF p :
                     {PointF{x, y}, PointF{x + w, y}, PointF{x, y + h}, PointF{x + w, y + h}}) {
                    const PointF q = mapPoint(p, ctm);
                    r = rectUnion(r, RectF{q.x, q.y, q.x, q.y});
                }
                mark.rects.push_back(r);
                mark.bounds = rectUnion(mark.bounds, r);
            }
        }
        if (mark.last < 0) {
            break;  // пометка не закрыта — не наша
        }
        out.push_back(std::move(mark));
    }
    return out;
}

}  // namespace ot::edit
