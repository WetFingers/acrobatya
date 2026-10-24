#include "core/edit/reflow.h"

#include <algorithm>
#include <cmath>

namespace ot::edit {

namespace reflow_detail {

constexpr float kFit = 0.01f;  // допуск ширины строки, пт

struct Line {
    int begin = 0;
    int end = 0;      // [begin, end) с пробелами в конце
    int visible = 0;  // конец без пробелов в конце (и без жёсткого разрыва)
    bool hard = false;
};

}  // namespace reflow_detail

namespace rd = reflow_detail;

bool isBreakSpace(char32_t c) noexcept {
    return c == U' ' || c == 0x1680 || (c >= 0x2000 && c <= 0x200A && c != 0x2007) || c == 0x205F ||
           c == 0x3000;
}

bool isHardBreak(char32_t c) noexcept { return c == U'\n' || c == 0x2028 || c == 0x2029; }

float naturalAdvance(char32_t ch, float w0, const CharStyle& style) noexcept {
    float advance = w0 * style.size * style.hscale + style.charSpacing;
    if (ch == U' ') {
        advance += style.wordSpacing;
    }
    return advance;
}

std::vector<RectF> toLayoutRects(const ParagraphEditor& editor, const std::vector<RectF>& pageRects) {
    const Matrix toLayout = invertMatrix(editor.geometry().toPage);
    std::vector<RectF> out;
    out.reserve(pageRects.size());
    for (const RectF& r : pageRects) {
        RectF q = RectF::none();
        for (const PointF p :
             {PointF{r.x0, r.y0}, PointF{r.x1, r.y0}, PointF{r.x0, r.y1}, PointF{r.x1, r.y1}}) {
            const PointF l = mapPoint(p, toLayout);
            q = rectUnion(q, RectF{l.x, l.y, l.x, l.y});
        }
        out.push_back(q);
    }
    return out;
}

ReflowParams reflowParams(const ParagraphEditor& editor, std::vector<RectF> obstacles) {
    const ParagraphGeometry& g = editor.geometry();
    ReflowParams p;
    p.left = g.left;
    p.width = g.width;
    p.firstIndent = g.firstIndent;
    p.firstBaseline = g.firstBaseline;
    p.lineSpacing = editor.lineSpacing();
    p.align = editor.alignment();
    p.emptySize = editor.typingStyle(0).size;
    p.obstacles = std::move(obstacles);
    return p;
}

ReflowResult reflow(const std::vector<ReflowChar>& chars, const ReflowParams& params) {
    const int n = static_cast<int>(chars.size());
    auto space = [&](int i) { return isBreakSpace(chars[static_cast<std::size_t>(i)].ch); };
    auto hard = [&](int i) { return isHardBreak(chars[static_cast<std::size_t>(i)].ch); };
    auto adv = [&](int i) { return chars[static_cast<std::size_t>(i)].advance; };
    auto ink = [&](int i) {
        const ReflowChar& c = chars[static_cast<std::size_t>(i)];
        return c.width >= 0 ? c.width : c.advance;
    };

    // Перенос: строка набирает слова, пока они входят (край — правый край последнего глифа); пробелы перед
    // словом входят в ширину, только когда за ними слово (или это начало абзаца или строки после разрыва).
    std::vector<rd::Line> lines;
    int i = 0;
    while (true) {
        rd::Line line;
        line.begin = i;
        const bool first = lines.empty();
        const float avail = params.width - (first ? params.firstIndent : 0.0f);
        const bool afterHard = first || lines.back().hard;
        float width = 0;  // перо после видимой части
        bool hasWord = false;
        // Ведущие пробелы — в начале абзаца и после разрыва.
        if (afterHard) {
            while (i < n && space(i)) {
                width += adv(i);
                ++i;
            }
        }
        line.visible = i;
        while (i < n) {
            if (hard(i)) {
                line.visible = std::max(line.visible, line.begin);
                line.hard = true;
                ++i;
                break;
            }
            // Пробелы до следующего слова.
            int j = i;
            float gap = 0;
            while (j < n && space(j)) {
                gap += adv(j);
                ++j;
            }
            if (j >= n || hard(j)) {
                i = j;  // пробелы в конце строки — висят
                if (j < n) {
                    line.hard = true;
                    ++i;
                }
                break;
            }
            // Слово [j, k).
            int k = j;
            float word = 0;
            while (k < n && !space(k) && !hard(k)) {
                word += adv(k);
                ++k;
            }
            const float extent = width + gap + word - adv(k - 1) + ink(k - 1);
            if (hasWord || line.visible > line.begin) {
                if (extent <= avail + rd::kFit) {
                    width += gap + word;
                    line.visible = k;
                    i = k;
                    hasWord = true;
                    continue;
                }
                i = j;  // слово — на следующую строку, пробелы остаются здесь
                break;
            }
            // Первое слово строки.
            if (extent <= avail + rd::kFit) {
                width += gap + word;
                line.visible = k;
                i = k;
                hasWord = true;
                continue;
            }
            // Слово длиннее строки — по символам (хотя бы один).
            int m = j;
            float w = gap;
            while (m < k && (m == j || w + ink(m) <= avail + rd::kFit)) {
                w += adv(m);
                ++m;
            }
            width = w;
            line.visible = m;
            i = m;
            hasWord = true;
            break;
        }
        line.end = i;
        lines.push_back(line);
        if (i >= n) {
            if (!line.hard || n == 0) {
                break;
            }
            // Разрыв в самом конце — пустая строка за ним (там встанет курсор).
            rd::Line tail;
            tail.begin = tail.end = tail.visible = n;
            lines.push_back(tail);
            break;
        }
    }

    ReflowResult result;
    TextLayout& layout = result.layout;
    layout.x.assign(static_cast<std::size_t>(n), 0.0f);
    layout.advance.assign(static_cast<std::size_t>(n), 0.0f);
    float baseline = params.firstBaseline;
    float prevSize = params.emptySize;
    for (std::size_t li = 0; li < lines.size(); ++li) {
        const rd::Line& l = lines[li];
        LayoutLine out;
        out.begin = l.begin;
        out.end = l.end;
        out.hardBreak = l.hard;
        // Кегль, верх и низ — по видимым символам (пустая строка — по разрыву или прошлой строке).
        float size = 0;
        float ascent = 0;
        float descent = 0;
        int from = l.begin;
        int to = l.visible;
        if (to <= from) {
            from = l.begin;
            to = l.end;
        }
        for (int c = from; c < to; ++c) {
            const ReflowChar& rc = chars[static_cast<std::size_t>(c)];
            size = std::max(size, rc.size);
            ascent = std::max(ascent, rc.ascent);
            descent = std::max(descent, rc.descent);
        }
        if (size <= 0) {
            size = li == 0 ? params.emptySize : prevSize;
            ascent = 0.8f * size;
            descent = 0.2f * size;
        }
        if (li > 0) {
            baseline += params.lineSpacing * size;
        }
        prevSize = size;
        out.baseline = baseline;
        out.ascent = ascent;
        out.descent = descent;

        // Растягиваются пробелы внутри строки: после первого слова и до последнего (ведущие и висящие — нет).
        int firstWord = l.begin;
        while (firstWord < l.visible && space(firstWord)) {
            ++firstWord;
        }
        float natural = 0;  // от пера первого символа до правого края последнего глифа
        int spaces = 0;
        for (int c = l.begin; c < l.visible; ++c) {
            natural += c + 1 == l.visible ? ink(c) : adv(c);
            if (space(c) && c > firstWord) {
                ++spaces;
            }
        }
        const float lineLeft = params.left + (li == 0 ? params.firstIndent : 0.0f);
        const float avail = params.width - (li == 0 ? params.firstIndent : 0.0f);
        float x0 = lineLeft;
        const bool last = li + 1 == lines.size();
        switch (params.align) {
            case Align::Right:
                x0 = lineLeft + avail - natural;
                break;
            case Align::Center:
                x0 = lineLeft + (avail - natural) / 2;
                break;
            case Align::Justify:
                if (!last && !l.hard && spaces > 0 && natural < avail) {
                    out.wordSpace = (avail - natural) / static_cast<float>(spaces);
                }
                break;
            case Align::Left:
                break;
        }
        out.x0 = x0;
        float x = x0;
        for (int c = l.begin; c < l.end; ++c) {
            float a = adv(c);
            if (c < l.visible && c > firstWord && space(c)) {
                a += out.wordSpace;
            }
            layout.x[static_cast<std::size_t>(c)] = x;
            layout.advance[static_cast<std::size_t>(c)] = a;
            if (c + 1 == l.visible) {
                out.x1 = x + ink(c);
            }
            x += a;
        }
        if (l.visible <= l.begin) {
            out.x1 = x0;
        }
        layout.lines.push_back(out);
        layout.bounds = rectUnion(layout.bounds, RectF{std::min(out.x0, out.x1), baseline - ascent,
                                                       std::max(out.x0, out.x1), baseline + descent});
    }
    // Наложение на соседние блоки: пересечение больше полпункта в обе стороны.
    for (std::size_t k = 0; k < params.obstacles.size(); ++k) {
        const RectF& o = params.obstacles[k];
        for (const LayoutLine& l : layout.lines) {
            const float w = std::min(o.x1, std::max(l.x0, l.x1)) - std::max(o.x0, std::min(l.x0, l.x1));
            const float h = std::min(o.y1, l.baseline + l.descent) - std::max(o.y0, l.baseline - l.ascent);
            if (w > 0.5f && h > 0.5f) {
                result.overlaps.push_back(static_cast<int>(k));
                break;
            }
        }
    }
    return result;
}

}  // namespace ot::edit
