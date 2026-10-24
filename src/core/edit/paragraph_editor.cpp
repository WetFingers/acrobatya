#include "core/edit/paragraph_editor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <mupdf/ucdn.h>

#include "core/edit/content_model.h"
#include "core/edit/font_info.h"
#include "core/edit/text_blocks.h"
#include "core/util/utf8.h"

namespace ot::edit {

namespace paragraph_detail {

bool isSpace(char32_t c) noexcept {
    return c == U' ' || c == 0x00A0 || c == U'\t' || (c >= 0x2000 && c <= 0x200B);
}

bool isWordChar(char32_t c) noexcept {
    if (c == U'_' || c == U'\'' || c == 0x2019) {
        return true;
    }
    switch (ucdn_get_general_category(static_cast<std::uint32_t>(c))) {
        case UCDN_GENERAL_CATEGORY_LL:
        case UCDN_GENERAL_CATEGORY_LM:
        case UCDN_GENERAL_CATEGORY_LO:
        case UCDN_GENERAL_CATEGORY_LT:
        case UCDN_GENERAL_CATEGORY_LU:
        case UCDN_GENERAL_CATEGORY_MC:
        case UCDN_GENERAL_CATEGORY_ME:
        case UCDN_GENERAL_CATEGORY_MN:
        case UCDN_GENERAL_CATEGORY_ND:
        case UCDN_GENERAL_CATEGORY_NL:
        case UCDN_GENERAL_CATEGORY_NO:
            return true;
        default:
            return false;
    }
}

float median(std::vector<float> v) {
    if (v.empty()) {
        return 0;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

std::u32string normalize(std::u32string_view text) {
    std::u32string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char32_t c = text[i];
        if (c == U'\r') {
            out.push_back(U'\n');
            if (i + 1 < text.size() && text[i + 1] == U'\n') {
                ++i;
            }
        } else if (c == U'\t') {
            out.push_back(U' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

}  // namespace paragraph_detail

namespace pd = paragraph_detail;

CharStyle StyleChange::applied(CharStyle style) const {
    if (family) {
        style.family = *family;
    }
    if (size) {
        style.size = *size;
    }
    if (color) {
        if (*color != style.color) {
            style.colorOps.clear();
        }
        style.color = *color;
    }
    if (bold) {
        style.bold = *bold;
    }
    if (italic) {
        style.italic = *italic;
    }
    if (underline) {
        style.underline = *underline;
    }
    if (rise) {
        style.rise = *rise;
    }
    if (charSpacing) {
        style.charSpacing = *charSpacing;
    }
    if (wordSpacing) {
        style.wordSpacing = *wordSpacing;
    }
    if (hscale) {
        style.hscale = *hscale;
    }
    if (render) {
        style.render = *render;
    }
    return style;
}

ParagraphEditor ParagraphEditor::fromBlock(const ContentModel& model, const TextBlock& block,
                                           const std::vector<TextBlock>& pageBlocks) {
    if (!block.editable() || !block.horizontal || block.lines.empty()) {
        throw std::invalid_argument("paragraph editor: read-only block");
    }
    ParagraphEditor e;
    e.align_ = block.align;
    e.source_.page = model.page();
    e.source_.bbox = block.bbox;
    const Matrix toLayout = concat(invertMatrix(model.pageMatrix()), Matrix{1, 0, 0, -1, 0, 0});
    e.geometry_.toPage = concat(Matrix{1, 0, 0, -1, 0, 0}, model.pageMatrix());
    auto layoutOf = [&](PointF page) { return mapPoint(page, toLayout); };

    // Стиль спана: размеры — в пунктах пространства пользователя (по матрице текста × CTM).
    auto styleOf = [&](const TextSpan& s) {
        CharStyle st;
        const Matrix frame = concat(Matrix{s.tm.a, s.tm.b, s.tm.c, s.tm.d, 0, 0}, s.ctm);
        const float sx = std::hypot(frame.a, frame.b);
        const float sy = std::hypot(frame.c, frame.d);
        const FontInfo& font = model.fonts()[static_cast<std::size_t>(s.font)];
        st.font = s.font;
        st.fontResource = font.resource;
        st.fontName = font.name;
        const FontName parsed = parseFontName(font.name);
        st.bold = parsed.weight >= 600;
        st.italic = parsed.italic;
        st.size = std::fabs(s.size) * sy;
        st.color = s.fill;
        st.colorOps = model.colorOps(s.refs);
        st.rise = s.rise * sy;
        st.charSpacing = s.charSpace * s.scale * sx;
        // Tw действует только на однобайтовый код 32; у абзаца по ширине это растяжка строки, не стиль.
        if (block.align != Align::Justify && !font.twoByte) {
            st.wordSpacing = s.wordSpace * s.scale * sx;
        }
        st.hscale = sy > 0 ? s.scale * sx / sy : s.scale;
        st.skew = frame.d != 0 ? frame.c / frame.d : 0;
        if (std::fabs(st.skew) < 1e-3f) {
            st.skew = 0;
        }
        st.render = s.render;
        return st;
    };

    // Символы — как TextBlock::text: строки через пробел (после дефиса — без), между спанами с промежутком —
    // пробел, «тень» — один раз.
    struct Pending {
        EditChar c;
        float x = 0;
        float advance = 0;
        float top = 0;  // рамка глифа по y (раскладка)
        float bottom = 0;
        bool synthetic = false;
    };
    std::vector<Pending> chars;
    std::vector<int> lineStart;
    std::vector<float> sizes;
    auto addSpace = [&](int style) {
        Pending p;
        p.c.ch = U' ';
        p.c.style = style;
        p.synthetic = true;
        if (!chars.empty()) {
            p.x = chars.back().x + chars.back().advance;
        }
        chars.push_back(p);
    };
    for (std::size_t li = 0; li < block.lines.size(); ++li) {
        const BlockLine& line = block.lines[li];
        if (li > 0 && !chars.empty()) {
            const char32_t c = chars.back().c.ch;
            if (c != U'-' && c != 0x00AD && c != 0x2010 && c != U' ') {
                addSpace(chars.back().c.style);
            }
        }
        lineStart.push_back(static_cast<int>(chars.size()));
        const TextSpan* prev = nullptr;
        std::string prevText;
        for (int si : line.spans) {
            const TextSpan& s = model.spans()[static_cast<std::size_t>(si)];
            const std::string text = model.spanText(si);
            if (prev != nullptr) {
                if (text == prevText && std::hypot(s.origin.x - prev->origin.x, s.origin.y - prev->origin.y) <
                                            0.3f * s.fontSize) {
                    continue;
                }
                const float gap = layoutOf(s.origin).x - layoutOf(prev->end).x;
                const bool spaced =
                    (!chars.empty() && chars.back().c.ch == U' ') || (!text.empty() && text.front() == ' ');
                if (!spaced && gap > 0.25f * std::max(s.fontSize, prev->fontSize)) {
                    addSpace(chars.empty() ? 0 : chars.back().c.style);
                }
            }
            e.source_.ops.push_back(s.op);
            const int style = e.styleIndex(styleOf(s));
            for (int g = s.firstGlyph; g < s.firstGlyph + s.glyphCount; ++g) {
                const Glyph& gl = model.glyphs()[static_cast<std::size_t>(g)];
                const PointF o = layoutOf(gl.origin);
                const PointF a = layoutOf(gl.quad.ll);
                const PointF b = layoutOf(gl.quad.lr);
                const PointF u = layoutOf(gl.quad.ul);
                const float width = b.x - a.x;
                const float top = std::min(u.y, a.y);
                const float bottom = std::max(u.y, a.y);
                const std::size_t n = std::max<std::size_t>(gl.unicode.size(), 1);
                for (std::size_t k = 0; k < n; ++k) {
                    Pending p;
                    p.c.ch = k < gl.unicode.size() ? gl.unicode[k] : U'�';
                    p.c.style = style;
                    if (k == 0) {
                        for (int byte = gl.codeBytes - 1; byte >= 0; --byte) {
                            p.c.code.push_back(static_cast<char>((gl.code >> (8 * byte)) & 0xFF));
                        }
                        p.c.w0 = gl.w0;
                        p.c.group = static_cast<std::uint8_t>(std::min<std::size_t>(n, 255));
                    } else {
                        p.c.group = 0;
                    }
                    p.x = o.x + width * static_cast<float>(k) / static_cast<float>(n);
                    p.advance = width / static_cast<float>(n);
                    p.top = top;
                    p.bottom = bottom;
                    chars.push_back(std::move(p));
                    sizes.push_back(e.styles_[static_cast<std::size_t>(style)].size);
                }
            }
            prev = &s;
            prevText = text;
        }
    }
    // Пробелы по краям абзаца сняты (как в TextBlock::text).
    std::size_t first = 0;
    while (first < chars.size() && chars[first].c.ch == U' ') {
        ++first;
    }
    std::size_t last = chars.size();
    while (last > first && chars[last - 1].c.ch == U' ') {
        --last;
    }
    for (std::size_t i = first; i < last; ++i) {
        e.chars_.push_back(chars[i].c);
    }
    // Источник — по порядку потока.
    std::sort(e.source_.ops.begin(), e.source_.ops.end());
    e.source_.ops.erase(std::unique(e.source_.ops.begin(), e.source_.ops.end()), e.source_.ops.end());
    for (int op : e.source_.ops) {
        e.source_.text += model.spanText(model.operations()[static_cast<std::size_t>(op)].span);
    }

    // Раскладка по позициям глифов.
    TextLayout& layout = e.layout_;
    for (std::size_t li = 0; li < block.lines.size(); ++li) {
        const BlockLine& bl = block.lines[li];
        LayoutLine l;
        const int from =
            std::clamp(lineStart[li] - static_cast<int>(first), 0, static_cast<int>(last - first));
        const int to = li + 1 < block.lines.size() ? std::clamp(lineStart[li + 1] - static_cast<int>(first),
                                                                0, static_cast<int>(last - first))
                                                   : static_cast<int>(last - first);
        l.begin = from;
        l.end = to;
        l.x0 = bl.x0;
        l.x1 = bl.x1;
        l.baseline = bl.y;
        float top = bl.y - 0.8f * bl.size;
        float bottom = bl.y + 0.2f * bl.size;
        bool any = false;
        for (int i = from; i < to; ++i) {
            const Pending& p = chars[static_cast<std::size_t>(i) + first];
            if (p.synthetic) {
                continue;
            }
            top = any ? std::min(top, p.top) : p.top;
            bottom = any ? std::max(bottom, p.bottom) : p.bottom;
            any = true;
        }
        l.ascent = std::max(0.0f, l.baseline - top);
        l.descent = std::max(0.0f, bottom - l.baseline);
        layout.lines.push_back(l);
    }
    // Пустые строки (одни снятые пробелы) не нужны.
    std::erase_if(layout.lines, [](const LayoutLine& l) { return l.end <= l.begin; });
    if (layout.lines.empty()) {
        LayoutLine l;
        l.x0 = l.x1 = block.lines.front().x0;
        l.baseline = block.lines.front().y;
        layout.lines.push_back(l);
    }
    layout.lines.front().begin = 0;
    layout.lines.back().end = e.size();
    for (std::size_t i = first; i < last; ++i) {
        layout.x.push_back(chars[i].x);
        layout.advance.push_back(chars[i].advance);
    }
    for (const LayoutLine& l : layout.lines) {
        layout.bounds = rectUnion(
            layout.bounds, RectF{l.x0, l.baseline - l.ascent, std::max(l.x0, l.x1), l.baseline + l.descent});
    }
    e.layoutValid_ = true;

    // Геометрия набора: левый край — строк после первой (отступ или выступ первой — свой), правый — самой
    // длинной; у центра и правого — крайние строки.
    ParagraphGeometry& g = e.geometry_;
    float left = block.lines.front().x0;
    float right = block.lines.front().x1;
    for (std::size_t li = 0; li < block.lines.size(); ++li) {
        const BlockLine& bl = block.lines[li];
        right = std::max(right, bl.x1);
        if (li == 1 || (li > 1 && bl.x0 < left)) {
            left = bl.x0;
        }
    }
    if (block.lines.size() > 1 && (block.align == Align::Center || block.align == Align::Right)) {
        for (const BlockLine& bl : block.lines) {
            left = std::min(left, bl.x0);
        }
    }
    g.firstIndent = (block.align == Align::Left || block.align == Align::Justify) && block.lines.size() > 1
                        ? block.lines.front().x0 - left
                        : 0;
    if (block.lines.size() == 1) {
        // Одна строка: набор — до края колонки (самый длинный блок, который с ней пересекается по X) или до
        // поля страницы, зеркального левому; у правого и по центру — так же в свою сторону.
        RectF page = RectF::none();
        const RectF pb = model.pageBounds();
        for (const PointF corner :
             {PointF{pb.x0, pb.y0}, PointF{pb.x1, pb.y0}, PointF{pb.x0, pb.y1}, PointF{pb.x1, pb.y1}}) {
            const PointF q = layoutOf(corner);
            page = rectUnion(page, RectF{q.x, q.y, q.x, q.y});
        }
        float columnLeft = left;
        float columnRight = right;
        bool neighbours = false;
        for (const TextBlock& other : pageBlocks) {
            if (!other.horizontal || other.lines.empty() || other.spans == block.spans) {
                continue;
            }
            float ol = other.lines.front().x0;
            float orr = other.lines.front().x1;
            for (const BlockLine& bl : other.lines) {
                ol = std::min(ol, bl.x0);
                orr = std::max(orr, bl.x1);
            }
            if (std::min(orr, right) - std::max(ol, left) > 0) {
                columnLeft = std::min(columnLeft, ol);
                columnRight = std::max(columnRight, orr);
                neighbours = true;
            }
        }
        if (!neighbours) {
            columnRight = std::max(right, page.x1 - (left - page.x0));
            columnLeft = std::min(left, page.x0 + (page.x1 - right));
        }
        if (block.align == Align::Right) {
            left = columnLeft;
        } else if (block.align == Align::Center) {
            const float center = (left + right) / 2;
            const float half =
                std::max((right - left) / 2, std::min(center - columnLeft, columnRight - center));
            left = center - half;
            right = center + half;
        } else {
            right = columnRight;
        }
    }
    g.left = left;
    g.width = std::max(0.0f, right - left);
    g.firstBaseline = block.lines.front().y;
    g.leading = block.leading;
    g.fontSize = pd::median(sizes);
    e.lineSpacing_ = block.lines.size() > 1 && g.leading > 0 && g.fontSize > 0 ? g.leading / g.fontSize
                                                                               : kDefaultLineSpacing;
    return e;
}

std::u32string ParagraphEditor::text() const {
    std::u32string out;
    out.reserve(chars_.size());
    for (const EditChar& c : chars_) {
        out.push_back(c.ch);
    }
    return out;
}

std::string ParagraphEditor::utf8() const { return utf8::encode(text()); }

std::u32string ParagraphEditor::text(TextRange range) const {
    const TextRange r = clamp(range);
    std::u32string out;
    for (int i = r.begin; i < r.end; ++i) {
        out.push_back(chars_[static_cast<std::size_t>(i)].ch);
    }
    return out;
}

const CharStyle& ParagraphEditor::styleAt(int pos) const {
    static const CharStyle kDefault;
    if (chars_.empty()) {
        return styles_.empty() ? kDefault : styles_.front();
    }
    const int i = std::clamp(pos, 0, size() - 1);
    return styles_[static_cast<std::size_t>(chars_[static_cast<std::size_t>(i)].style)];
}

const CharStyle& ParagraphEditor::typingStyle(int pos) const { return styleAt(pos > 0 ? pos - 1 : 0); }

std::vector<StyledRun> ParagraphEditor::runs() const {
    std::vector<StyledRun> out;
    for (int i = 0; i < size(); ++i) {
        const int style = chars_[static_cast<std::size_t>(i)].style;
        if (out.empty() || out.back().style != style) {
            out.push_back({i, i + 1, style});
        } else {
            out.back().end = i + 1;
        }
    }
    return out;
}

int ParagraphEditor::styleIndex(const CharStyle& style) {
    for (std::size_t i = 0; i < styles_.size(); ++i) {
        if (styles_[i] == style) {
            return static_cast<int>(i);
        }
    }
    styles_.push_back(style);
    return static_cast<int>(styles_.size()) - 1;
}

TextRange ParagraphEditor::clamp(TextRange r) const {
    r.begin = std::clamp(r.begin, 0, size());
    r.end = std::clamp(r.end, r.begin, size());
    return r;
}

int ParagraphEditor::insert(int pos, std::u32string_view text) {
    const CharStyle style = typingStyle(pos);
    return insert(pos, text, style);
}

int ParagraphEditor::insert(int pos, std::u32string_view text, const CharStyle& style) {
    pos = std::clamp(pos, 0, size());
    const std::u32string clean = pd::normalize(text);
    if (clean.empty()) {
        return pos;
    }
    const int index = styleIndex(style);
    std::vector<EditChar> added;
    added.reserve(clean.size());
    for (char32_t c : clean) {
        EditChar ec;
        ec.ch = c;
        ec.style = index;
        added.push_back(std::move(ec));
    }
    // Глиф, разрезанный вставкой (внутри лигатуры), пишется заново.
    for (int i = pos; i < size() && chars_[static_cast<std::size_t>(i)].group == 0; ++i) {
        chars_[static_cast<std::size_t>(i)].group = 1;
    }
    chars_.insert(chars_.begin() + pos, added.begin(), added.end());
    shiftLayout(pos, static_cast<int>(added.size()));
    modified_ = true;
    return pos + static_cast<int>(added.size());
}

int ParagraphEditor::erase(TextRange range) {
    const TextRange r = clamp(range);
    if (r.empty()) {
        return r.begin;
    }
    chars_.erase(chars_.begin() + r.begin, chars_.begin() + r.end);
    for (int i = r.begin; i < size() && chars_[static_cast<std::size_t>(i)].group == 0; ++i) {
        chars_[static_cast<std::size_t>(i)].group = 1;
    }
    shiftLayout(r.begin, -r.size());
    modified_ = true;
    return r.begin;
}

int ParagraphEditor::replace(TextRange range, std::u32string_view text) {
    const TextRange r = clamp(range);
    const CharStyle style = r.empty() ? typingStyle(r.begin) : styleAt(r.begin);
    erase(r);
    return insert(r.begin, text, style);
}

void ParagraphEditor::applyStyle(TextRange range, const StyleChange& change) {
    const TextRange r = clamp(range);
    bool changed = false;
    for (int i = r.begin; i < r.end; ++i) {
        EditChar& c = chars_[static_cast<std::size_t>(i)];
        const CharStyle next = change.applied(styles_[static_cast<std::size_t>(c.style)]);
        const int index = styleIndex(next);
        if (index != c.style) {
            c.style = index;
            changed = true;
        }
    }
    if (changed) {
        modified_ = true;
        layoutValid_ = false;
    }
}

void ParagraphEditor::setAlignment(Align align) {
    if (align != align_) {
        align_ = align;
        modified_ = true;
        layoutValid_ = false;
    }
}

void ParagraphEditor::setLineSpacing(float factor) {
    factor = std::clamp(factor, 0.5f, 10.0f);
    if (factor != lineSpacing_) {
        lineSpacing_ = factor;
        modified_ = true;
        layoutValid_ = false;
    }
}

void ParagraphEditor::markWritten(ParagraphSource source, std::vector<EditChar> chars,
                                  std::vector<CharStyle> styles) {
    source_ = std::move(source);
    chars_ = std::move(chars);
    styles_ = std::move(styles);
    modified_ = false;
}

void ParagraphEditor::setLayout(TextLayout layout) {
    layout_ = std::move(layout);
    layoutValid_ = layout_.x.size() == chars_.size() && !layout_.lines.empty();
}

// Правка до новой раскладки: вставленные символы — нулевой ширины в позиции вставки, строки сдвигаются.
void ParagraphEditor::shiftLayout(int pos, int delta) {
    layoutValid_ = false;
    TextLayout& l = layout_;
    if (l.lines.empty()) {
        return;
    }
    const std::size_t at = static_cast<std::size_t>(std::clamp(pos, 0, static_cast<int>(l.x.size())));
    if (delta > 0) {
        float x =
            at < l.x.size() ? l.x[at] : (l.x.empty() ? l.lines.back().x0 : l.x.back() + l.advance.back());
        l.x.insert(l.x.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::size_t>(delta), x);
        l.advance.insert(l.advance.begin() + static_cast<std::ptrdiff_t>(at), static_cast<std::size_t>(delta),
                         0.0f);
    } else if (delta < 0) {
        const std::size_t end = std::min(l.x.size(), at + static_cast<std::size_t>(-delta));
        l.x.erase(l.x.begin() + static_cast<std::ptrdiff_t>(at),
                  l.x.begin() + static_cast<std::ptrdiff_t>(end));
        l.advance.erase(l.advance.begin() + static_cast<std::ptrdiff_t>(at),
                        l.advance.begin() + static_cast<std::ptrdiff_t>(end));
    }
    // Вставка на стыке строк — в строку, где курсор (следующую).
    for (LayoutLine& line : l.lines) {
        auto move = [&](int& v) {
            if (v > pos) {
                v = delta > 0 ? v + delta : std::max(pos, v + delta);
            }
        };
        move(line.begin);
        move(line.end);
    }
    l.lines.front().begin = 0;
    l.lines.back().end = static_cast<int>(l.x.size());
    std::erase_if(l.lines,
                  [&](const LayoutLine& line) { return line.end <= line.begin && l.lines.size() > 1; });
}

PointF ParagraphEditor::toPage(float x, float y) const { return mapPoint({x, y}, geometry_.toPage); }

PointF ParagraphEditor::toLayout(PointF page) const { return mapPoint(page, invertMatrix(geometry_.toPage)); }

int ParagraphEditor::lineOf(int pos) const {
    const auto& lines = layout_.lines;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (pos < lines[i].end) {
            return static_cast<int>(i);
        }
    }
    return lines.empty() ? 0 : static_cast<int>(lines.size()) - 1;
}

float ParagraphEditor::xAt(int pos, const LayoutLine& line) const {
    const int n = static_cast<int>(layout_.x.size());
    if (pos < line.end && pos >= 0 && pos < n) {
        return layout_.x[static_cast<std::size_t>(pos)];
    }
    const int last = std::min(line.end, n) - 1;
    if (last >= line.begin && last >= 0) {
        return layout_.x[static_cast<std::size_t>(last)] + layout_.advance[static_cast<std::size_t>(last)];
    }
    return line.x0;
}

Caret ParagraphEditor::caret(int pos) const {
    Caret c;
    if (layout_.lines.empty()) {
        return c;
    }
    pos = std::clamp(pos, 0, size());
    c.line = lineOf(pos);
    const LayoutLine& line = layout_.lines[static_cast<std::size_t>(c.line)];
    c.x = xAt(pos, line);
    c.top = toPage(c.x, line.baseline - line.ascent);
    c.bottom = toPage(c.x, line.baseline + line.descent);
    return c;
}

int ParagraphEditor::positionAt(PointF pagePoint) const {
    if (layout_.lines.empty()) {
        return 0;
    }
    const PointF p = toLayout(pagePoint);
    const auto& lines = layout_.lines;
    // Строка — по вертикали: полосы строк делятся посередине между соседями.
    std::size_t li = 0;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float bottom =
            i + 1 < lines.size()
                ? ((lines[i].baseline + lines[i].descent) + (lines[i + 1].baseline - lines[i + 1].ascent)) / 2
                : std::numeric_limits<float>::max();
        li = i;
        if (p.y <= bottom) {
            break;
        }
    }
    const LayoutLine& line = lines[li];
    if (li == 0 && p.y < line.baseline - line.ascent - line.ascent) {
        return 0;  // заметно выше абзаца — начало
    }
    if (li + 1 == lines.size() && p.y > line.baseline + line.descent + line.ascent) {
        return size();  // заметно ниже — конец
    }
    // Последняя позиция строки: у перенесённой — перед пробелом переноса.
    int lastPos = line.end;
    if (li + 1 < lines.size() && line.end > line.begin) {
        const char32_t c = chars_[static_cast<std::size_t>(line.end - 1)].ch;
        if (pd::isSpace(c) || c == U'\n') {
            lastPos = line.end - 1;
        } else {
            lastPos = line.end;  // перенос по символам: конец строки — начало следующей
        }
    }
    int best = line.begin;
    float bestDistance = std::numeric_limits<float>::max();
    for (int pos = line.begin; pos <= lastPos; ++pos) {
        const float x = xAt(pos, line);
        const float d = std::fabs(x - p.x);
        if (d < bestDistance) {
            bestDistance = d;
            best = pos;
        }
    }
    return best;
}

int ParagraphEditor::moveLine(int pos, int delta) const {
    if (layout_.lines.empty()) {
        return pos;
    }
    const Caret c = caret(pos);
    const int target = c.line + delta;
    if (target < 0) {
        return 0;
    }
    if (target >= lineCount()) {
        return size();
    }
    const LayoutLine& line = layout_.lines[static_cast<std::size_t>(target)];
    return positionAt(toPage(c.x, line.baseline - line.ascent / 2));
}

TextRange ParagraphEditor::wordAt(int pos) const {
    pos = std::clamp(pos, 0, size());
    auto word = [&](int i) {
        return i >= 0 && i < size() && pd::isWordChar(chars_[static_cast<std::size_t>(i)].ch);
    };
    int begin = pos;
    if (!word(begin)) {
        if (!word(begin - 1)) {
            return {pos, pos};
        }
        --begin;
    }
    while (word(begin - 1)) {
        --begin;
    }
    int end = begin;
    while (word(end)) {
        ++end;
    }
    return {begin, end};
}

std::vector<QuadF> ParagraphEditor::selectionQuads(TextRange range) const {
    std::vector<QuadF> out;
    const TextRange r = clamp(range);
    if (r.empty()) {
        return out;
    }
    for (const LayoutLine& line : layout_.lines) {
        const int a = std::max(r.begin, line.begin);
        const int b = std::min(r.end, line.end);
        if (a >= b) {
            continue;
        }
        const float x0 = xAt(a, line);
        const float x1 = b < line.end ? xAt(b, line) : xAt(line.end, line);
        const float top = line.baseline - line.ascent;
        const float bottom = line.baseline + line.descent;
        out.push_back({toPage(x0, top), toPage(x1, top), toPage(x0, bottom), toPage(x1, bottom)});
    }
    return out;
}

}  // namespace ot::edit
