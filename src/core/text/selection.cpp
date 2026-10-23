#include "core/text/selection.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/text/stext.h"
#include "core/util/utf8.h"

namespace ot {

namespace {

float dot(PointF a, PointF b) { return a.x * b.x + a.y * b.y; }
// Расстояние от origin до q вдоль dir (знаковое) — linedist MuPDF.
float alongDir(PointF origin, PointF dir, PointF q) { return dot(dir, {q.x - origin.x, q.y - origin.y}); }
PointF mid(PointF a, PointF b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }

float largestSize(const TextPage& page, const TextLine& line) {
    float size = 0;
    for (int i = line.firstChar; i < line.firstChar + line.charCount; ++i) {
        size = std::max(size, page.chars()[i].size);
    }
    return size;
}

// find_closest_in_line: граница внутри строки (idx — индекс первого символа строки).
int closestInLine(const TextPage& page, const TextLine& line, PointF q) {
    const auto& chars = page.chars();
    const float hsize = largestSize(page, line) / 2;
    const PointF vdir{-line.dir.y, line.dir.x};
    const PointF p1 = mid(chars[line.firstChar].quad.ll, chars[line.firstChar].quad.ul);
    const float v = alongDir(p1, vdir, q);  // положительное — ниже средней линии
    if (v < -hsize) {
        return line.firstChar;
    }
    if (v > hsize) {
        return line.firstChar + line.charCount;
    }
    float closest = 1e30f;
    int best = line.firstChar;
    for (int i = line.firstChar; i < line.firstChar + line.charCount; ++i) {
        const QuadF& quad = chars[i].quad;
        const float d1 = std::fabs(alongDir(quad.ll, line.dir, q));
        const float d2 = std::fabs(alongDir(quad.lr, line.dir, q));
        if (d1 < closest) {
            closest = d1;
            best = i;
        }
        if (d2 < closest) {
            closest = d2;
            best = i + 1;
        }
    }
    return best;
}

// Строка как система координат: s — вдоль базовой линии от точки пера первого символа, t — поперёк (вниз).
// Поперечный размах — чернила всех символов строки: квады выделения одной высоты на всю строку (у
// ACCURATE_BBOXES рамка каждого символа — его чернила, у пробела высота нулевая).
struct LineFrame {
    PointF origin;
    PointF dir{1, 0};
    PointF vdir{0, 1};
    float top = 0;
    float bottom = 0;
};

LineFrame frameOf(const TextPage& page, int lineIndex) {
    const TextLine& line = page.lines()[lineIndex];
    const auto& chars = page.chars();
    LineFrame f;
    f.dir = line.dir;
    f.vdir = {-line.dir.y, line.dir.x};
    f.origin = chars[line.firstChar].origin;
    float top = 1e30f, bottom = -1e30f, size = 0;
    for (int i = line.firstChar; i < line.firstChar + line.charCount; ++i) {
        const QuadF& q = chars[i].quad;
        for (const PointF& corner : {q.ul, q.ur, q.ll, q.lr}) {
            const float t = alongDir(f.origin, f.vdir, corner);
            top = std::min(top, t);
            bottom = std::max(bottom, t);
        }
        size = std::max(size, chars[i].size);
    }
    if (bottom - top < 0.01f) {  // строка из пробелов — высота по кеглю
        top = -0.8f * size;
        bottom = 0.2f * size;
    }
    f.top = top;
    f.bottom = bottom;
    return f;
}

// Размах символа вдоль строки.
std::pair<float, float> spanOf(const LineFrame& f, const TextChar& ch) {
    const float a = std::min(alongDir(f.origin, f.dir, ch.quad.ll), alongDir(f.origin, f.dir, ch.quad.ul));
    const float b = std::max(alongDir(f.origin, f.dir, ch.quad.lr), alongDir(f.origin, f.dir, ch.quad.ur));
    return {std::min(a, b), std::max(a, b)};
}

QuadF boxQuad(const LineFrame& f, float s0, float s1) {
    auto at = [&](float s, float t) {
        return PointF{f.origin.x + f.dir.x * s + f.vdir.x * t, f.origin.y + f.dir.y * s + f.vdir.y * t};
    };
    return {at(s0, f.top), at(s1, f.top), at(s0, f.bottom), at(s1, f.bottom)};
}

// Квады выделения: подряд идущие символы строки — один квад высотой в строку; разрыв больше половины кегля
// (столбцы в одной строке) — новый квад (как слияние on_highlight_char у MuPDF).
std::vector<QuadF> highlightQuads(const TextPage& page, const std::vector<int>& chars) {
    std::vector<QuadF> quads;
    int line = -1;
    LineFrame frame;
    float s0 = 0, s1 = 0;
    bool open = false;
    auto flush = [&] {
        if (open && s1 > s0) {
            quads.push_back(boxQuad(frame, s0, s1));
        }
        open = false;
    };
    for (const int index : chars) {
        const TextChar& ch = page.chars()[index];
        if (ch.line != line) {
            flush();
            line = ch.line;
            frame = frameOf(page, line);
        }
        const auto [a, b] = spanOf(frame, ch);
        if (open && a - s1 <= 0.5f * ch.size) {
            s0 = std::min(s0, a);
            s1 = std::max(s1, b);
            continue;
        }
        flush();
        s0 = a;
        s1 = b;
        open = true;
    }
    flush();
    return quads;
}

TextSelection makeSelection(const TextPage& page, std::vector<int> chars, bool rectangle) {
    TextSelection s;
    s.quads = highlightQuads(page, chars);
    s.chars = std::move(chars);
    s.rectangle = rectangle;
    return s;
}

bool isBlankChar(char32_t c) {
    return c == U' ' || c == U'\t' || c == 0xA0 || (c >= 0x2000 && c <= 0x200B) || c == 0x3000;
}

}  // namespace

int closestBoundary(const TextPage& page, PointF q) {
    const auto& chars = page.chars();
    const TextLine* closestLine = nullptr;
    float closestV = 1e30f;
    float closestH = 1e30f;
    for (const TextLine& line : page.lines()) {
        if (line.charCount == 0) {
            continue;
        }
        const float hsize = largestSize(page, line) / 2;
        const PointF hdir = line.dir;
        const PointF vdir{-line.dir.y, line.dir.x};
        const TextChar& first = chars[line.firstChar];
        const TextChar& last = chars[line.firstChar + line.charCount - 1];
        const PointF p1 = mid(first.quad.ll, first.quad.ul);
        const PointF p2 = mid(last.quad.lr, last.quad.ur);
        const float v = alongDir(p1, vdir, q);
        const float h1 = alongDir(p1, hdir, q);
        const float h2 = alongDir(p2, hdir, q);
        if (v >= -hsize && v <= hsize && (h1 > 0) != (h2 > 0)) {
            closestV = 0;
            closestH = 0;
            closestLine = &line;
        } else {
            const float av = std::fabs(v);
            const float ah = std::min(std::fabs(h1), std::fabs(h2));
            if (av < hsize) {
                if (ah <= closestH) {
                    closestV = 0;
                    closestH = ah;
                    closestLine = &line;
                }
            } else if (av <= closestV) {
                closestV = av;
                closestLine = &line;
            }
        }
    }
    return closestLine != nullptr ? closestInLine(page, *closestLine, q) : 0;
}

TextSelection selectStream(const TextPage& page, PointF a, PointF b, SnapMode snap) {
    const int n = static_cast<int>(page.chars().size());
    int start = closestBoundary(page, a);
    int end = closestBoundary(page, b);
    if (start > end) {
        std::swap(start, end);
        std::swap(a, b);
    }
    if (snap != SnapMode::Chars && n > 0) {
        // Концы — символ под точкой (двойной щелчок по слову), иначе символ у границы: справа от начала,
        // слева от конца. Начало уходит к началу слова или строки, конец — к концу.
        int left = page.charIndexAt(a, 1.0f);
        if (left < 0) {
            left = std::clamp(start, 0, n - 1);
        }
        int right = page.charIndexAt(b, 1.0f);
        if (right < 0) {
            right = std::clamp(end > start ? end - 1 : end, 0, n - 1);
        }
        if (right < left) {
            std::swap(left, right);
        }
        const auto& chars = page.chars();
        if (snap == SnapMode::Words) {
            start = TextPage::isWordChar(chars[left].c) ? page.wordBounds(left).first : left;
            end = (TextPage::isWordChar(chars[right].c) ? page.wordBounds(right).second : right) + 1;
        } else {
            start = page.lines()[chars[left].line].firstChar;
            const TextLine& line = page.lines()[chars[right].line];
            end = line.firstChar + line.charCount;
        }
    }
    std::vector<int> chars;
    for (int i = start; i < end && i < n; ++i) {
        chars.push_back(i);
    }
    return makeSelection(page, std::move(chars), false);
}

TextSelection selectRectangle(const TextPage& page, const RectF& area) {
    std::vector<int> chars;
    int line = -1;
    LineFrame frame;
    for (int i = 0; i < static_cast<int>(page.chars().size()); ++i) {
        const TextChar& ch = page.chars()[i];
        if (ch.line != line) {
            line = ch.line;
            frame = frameOf(page, line);
        }
        // Рамка символа высотой в строку: у пробела своя высота нулевая.
        const auto [s0, s1] = spanOf(frame, ch);
        const RectF r = boxQuad(frame, s0, s1).bounds();
        // Рамка символа задевает область (fz_copy_rectangle: непустое пересечение).
        if (std::max(r.x0, area.x0) < std::min(r.x1, area.x1) &&
            std::max(r.y0, area.y0) < std::min(r.y1, area.y1)) {
            chars.push_back(i);
        }
    }
    return makeSelection(page, std::move(chars), true);
}

TextSelection selectAll(const TextPage& page) {
    std::vector<int> chars(page.chars().size());
    for (int i = 0; i < static_cast<int>(chars.size()); ++i) {
        chars[i] = i;
    }
    return makeSelection(page, std::move(chars), false);
}

TextSelection selectRange(const TextPage& page, int first, int last) {
    std::vector<int> chars;
    first = std::max(first, 0);
    last = std::min(last, static_cast<int>(page.chars().size()) - 1);
    for (int i = first; i <= last; ++i) {
        chars.push_back(i);
    }
    return makeSelection(page, std::move(chars), false);
}

std::string toPlainText(const TextPage& page, const TextSelection& selection) {
    std::string out;
    int line = -1;
    for (const int index : selection.chars) {
        const TextChar& ch = page.chars()[index];
        if (line >= 0 && ch.line != line) {
            out += '\n';
        }
        line = ch.line;
        utf8::append(out, ch.c < 32 ? char32_t{0xFFFD} : ch.c);
    }
    return out;
}

std::string htmlEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out += c;
        }
    }
    return out;
}

std::string toHtmlFragment(const TextPage& page, const TextSelection& selection) {
    std::string out;
    int block = -1;
    int line = -1;
    bool bold = false;
    bool italic = false;
    auto closeStyle = [&] {
        if (italic) {
            out += "</i>";
        }
        if (bold) {
            out += "</b>";
        }
        bold = italic = false;
    };
    std::string run;
    for (const int index : selection.chars) {
        const TextChar& ch = page.chars()[index];
        const int chBlock = page.lines()[ch.line].block;
        if (ch.line != line) {
            closeStyle();
            if (chBlock != block) {
                if (block >= 0) {
                    out += "</p>";
                }
                out += "<p>";
                block = chBlock;
            } else {
                out += "<br>";
            }
            line = ch.line;
        }
        const TextFont* font = ch.font >= 0 ? &page.fonts()[ch.font] : nullptr;
        // Пробелы не меняют стиль: «Жирная строка» — одним <b>.
        const bool space = isBlankChar(ch.c);
        const bool wantBold = space ? bold : (font != nullptr && font->bold) || (ch.flags & kCharBold) != 0;
        const bool wantItalic = space ? italic : font != nullptr && font->italic;
        if (wantBold != bold || wantItalic != italic) {
            closeStyle();
            if (wantBold) {
                out += "<b>";
            }
            if (wantItalic) {
                out += "<i>";
            }
            bold = wantBold;
            italic = wantItalic;
        }
        run.clear();
        utf8::append(run, ch.c < 32 ? char32_t{0xFFFD} : ch.c);
        out += htmlEscape(run);
    }
    closeStyle();
    if (block >= 0) {
        out += "</p>";
    }
    return out;
}

std::string htmlDocument(std::string_view fragment) {
    std::string out = "<html><head><meta charset=\"utf-8\"></head><body>";
    out += fragment;
    out += "</body></html>";
    return out;
}

std::string toHtml(const TextPage& page, const TextSelection& selection) {
    return htmlDocument(toHtmlFragment(page, selection));
}

}  // namespace ot
