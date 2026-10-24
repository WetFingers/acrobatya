#include "core/edit/rewriter.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_info.h"
#include "core/edit/font_resolver.h"
#include "core/edit/text_blocks.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::edit {

namespace rewriter_detail {

constexpr float kPenTolerance = 0.0005f;  // расхождение пера с раскладкой, которое уже пишется сдвигом TJ, пт

void putNumber(std::string& out, float v) {
    if (!std::isfinite(v) || std::fabs(v) < 1e-6f) {
        out += '0';
        return;
    }
    char buf[96];
    const auto res = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed);
    out.append(buf, res.ptr);
}

std::string number(float v) {
    std::string s;
    putNumber(s, v);
    return s;
}

void putName(std::string& out, std::string_view name) {
    static const char kHex[] = "0123456789ABCDEF";
    static constexpr std::string_view kDelimiters = "()<>[]{}/%";
    out += '/';
    for (unsigned char c : name) {
        if (c <= 32 || c >= 127 || c == '#' ||
            kDelimiters.find(static_cast<char>(c)) != std::string_view::npos) {
            out += '#';
            out += kHex[c >> 4];
            out += kHex[c & 15];
        } else {
            out += static_cast<char>(c);
        }
    }
}

// Литеральная строка: \( \) \\ и восьмеричные для байтов < 32 (эталон, план 3.3).
void putLiteral(std::string& out, std::string_view bytes) {
    out += '(';
    for (unsigned char c : bytes) {
        if (c == '(' || c == ')' || c == '\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c < 32) {
            out += '\\';
            out += static_cast<char>('0' + ((c >> 6) & 7));
            out += static_cast<char>('0' + ((c >> 3) & 7));
            out += static_cast<char>('0' + (c & 7));
        } else {
            out += static_cast<char>(c);
        }
    }
    out += ')';
}

void putMatrix(std::string& out, const Matrix& m) {
    for (float v : {m.a, m.b, m.c, m.d, m.e, m.f}) {
        putNumber(out, v);
        out += ' ';
    }
    out += "Tm";
}

std::string rgbOps(std::uint32_t argb) {
    std::string s;
    for (int shift : {16, 8, 0}) {
        putNumber(s, static_cast<float>((argb >> shift) & 0xFF) / 255.0f);
        s += ' ';
    }
    return s + "rg";
}

// Операторы цвета заливки → те же для обводки (cs → CS, scn → SCN, rg → RG…).
std::string strokeOf(const std::string& fill) {
    std::string out;
    std::size_t i = 0;
    while (i < fill.size()) {
        std::size_t j = i;
        while (j < fill.size() && fill[j] != ' ' && fill[j] != '\n' && fill[j] != '\r' && fill[j] != '\t') {
            ++j;
        }
        std::string token = fill.substr(i, j - i);
        if (token == "g" || token == "rg" || token == "k" || token == "cs" || token == "sc" ||
            token == "scn") {
            for (char& c : token) {
                c = static_cast<char>(c - 'a' + 'A');
            }
        }
        out += token;
        if (j < fill.size()) {
            out += fill[j];
        }
        i = j + 1;
    }
    return out;
}

float ctmScale(const Matrix& m) { return std::sqrt(std::fabs(m.a * m.d - m.b * m.c)); }

// Сдвиг пера спана (Tm перед глифами) от Tlm по x пространства текста.
float lineOffset(const TextSpan& s) {
    const float a = s.tlm.a, b = s.tlm.b;
    const float len = a * a + b * b;
    if (len == 0) {
        return 0;
    }
    return ((s.tm.e - s.tlm.e) * a + (s.tm.f - s.tlm.f) * b) / len;
}

bool sameMatrix(const Matrix& x, const Matrix& y) {
    return x.a == y.a && x.b == y.b && x.c == y.c && x.d == y.d && x.e == y.e && x.f == y.f;
}

// Шрифт набора.
struct ShapedFont {
    std::string key;
    std::string resource;  // имя в /Resources/Font страницы (пусто — раскладка без записи)
    bool fresh = false;    // встроен редактором
    bool twoByte = false;
    float ascender = 0.8f;
    float descender = -0.2f;
};

struct Shaped {
    int font = -1;
    std::string code;
    float w0 = 0;
    bool emit = true;  // false — продолжение лигатуры, перевод строки, символ без глифа
    bool syntheticBold = false;
    bool syntheticItalic = false;
};

struct Shaping {
    std::vector<ShapedFont> fonts;
    std::vector<Shaped> chars;
};

}  // namespace rewriter_detail

namespace rw = rewriter_detail;

const char* rewriteErrorCode(RewriteError error) noexcept {
    switch (error) {
        case RewriteError::None:
            return "";
        case RewriteError::Stale:
            return "stale";
        case RewriteError::ReadOnly:
            return "readOnly";
        case RewriteError::Signed:
            return "signed";
        case RewriteError::Failed:
            return "failed";
    }
    return "?";
}

struct Rewriter::Impl {
    Document& doc;
    FontLibrary& lib;
    FontResolver resolver;
    FontEmbedder embedder;

    Impl(Document& d, FontLibrary& library, SystemFonts& system, const FallbackFonts& fallback)
        : doc(d), lib(library), resolver(library, system, fallback), embedder(d, library) {}

    bool protectedDocument() const;
    std::optional<RewriteError> validate(const ParagraphEditor& e, const ContentModel& m,
                                         std::string& why) const;
    rw::Shaping shape(const ParagraphEditor& e, const ContentModel& m, bool embed);
    std::vector<ReflowChar> measure(const ParagraphEditor& e, const rw::Shaping& sh) const;
    std::vector<RectF> obstacles(const ParagraphEditor& e, const ContentModel& m) const;
    RewriteReport rewrite(ParagraphEditor& e, const RewriteOptions& options);
};

bool Rewriter::Impl::protectedDocument() const {
    if (doc.isSigned()) {
        return true;
    }
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int perms = 0;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        perms = pdf_dict_get(ctx, root, PDF_NAME(Perms)) != nullptr;
    });
    return perms != 0;
}

std::optional<RewriteError> Rewriter::Impl::validate(const ParagraphEditor& e, const ContentModel& m,
                                                     std::string& why) const {
    std::string text;
    for (int op : e.source().ops) {
        if (op < 0 || op >= static_cast<int>(m.operations().size())) {
            why = "paragraph operator out of range";
            return RewriteError::Stale;
        }
        const Operation& o = m.operations()[static_cast<std::size_t>(op)];
        if (!isShowOp(o.kind) || o.span < 0) {
            why = "paragraph operator is not a text-showing span";
            return RewriteError::Stale;
        }
        const TextSpan& s = m.spans()[static_cast<std::size_t>(o.span)];
        text += m.spanText(o.span);
        if (!s.editable()) {
            why = std::string("read-only span: ") + readOnlyCode(s.reason);
            return RewriteError::ReadOnly;
        }
    }
    if (text != e.source().text) {
        why = "page content changed since the paragraph was built";
        return RewriteError::Stale;
    }
    return std::nullopt;
}

rw::Shaping Rewriter::Impl::shape(const ParagraphEditor& e, const ContentModel& m, bool embed) {
    rw::Shaping sh;
    const int page = e.source().page;
    const auto& chars = e.chars();
    const int n = e.size();
    sh.chars.assign(static_cast<std::size_t>(n), rw::Shaped{});
    std::map<std::string, std::unique_ptr<FontProbe>> probes;
    auto probeFor = [&](const std::string& resource) -> FontProbe* {
        if (resource.empty() || resource.rfind("gs:", 0) == 0) {
            return nullptr;
        }
        auto it = probes.find(resource);
        if (it == probes.end()) {
            it = probes.emplace(resource, FontProbe::forPage(doc, page, resource)).first;
        }
        return it->second.get();
    };
    auto fontIndex = [&](rw::ShapedFont f) {
        for (std::size_t i = 0; i < sh.fonts.size(); ++i) {
            if (sh.fonts[i].key == f.key) {
                return static_cast<int>(i);
            }
        }
        sh.fonts.push_back(std::move(f));
        return static_cast<int>(sh.fonts.size()) - 1;
    };
    auto originalFont = [&](const std::string& resource, FontProbe* probe) {
        rw::ShapedFont f;
        f.key = "res:" + resource;
        f.resource = resource;
        bool found = false;
        for (const FontInfo& info : m.fonts()) {
            if (info.resource == resource) {
                f.twoByte = info.twoByte;
                f.ascender = info.ascender;
                f.descender = info.descender;
                found = true;
                break;
            }
        }
        if (!found && probe != nullptr) {
            const FontInfo& info = probe->glyphs().info();
            f.twoByte = info.twoByte;
            f.ascender = info.ascender;
            f.descender = info.descender;
        }
        return fontIndex(std::move(f));
    };
    std::map<std::string, EmbeddedFont> embedded;
    auto newFont = [&](const FontChoice& c) -> std::pair<int, const EmbeddedFont*> {
        const std::string faceKey = "face:" + c.face.file.string() + "#" + std::to_string(c.face.index) +
                                    "#" + embedKindCode(c.embed);
        rw::ShapedFont f;
        f.key = faceKey;
        f.fresh = true;
        f.twoByte = c.embed == EmbedKind::Cid;
        fz_font* font = lib.font(c.face);
        if (font != nullptr) {
            f.ascender = fz_font_ascender(lib.context(), font);
            f.descender = fz_font_descender(lib.context(), font);
        }
        const EmbeddedFont* ef = nullptr;
        if (embed) {
            auto it = embedded.find(faceKey);
            if (it == embedded.end()) {
                it = embedded.emplace(faceKey, embedder.add(page, c.face, c.embed)).first;
            }
            ef = &it->second;
            f.resource = ef->resource;
        }
        return {fontIndex(std::move(f)), ef};
    };

    for (const StyledRun& run : e.runs()) {
        const CharStyle& st = e.styles()[static_cast<std::size_t>(run.style)];
        FontProbe* probe = st.family.empty() ? probeFor(st.fontResource) : nullptr;
        const FontName parsed = parseFontName(st.fontName);
        const bool nameBold = parsed.weight >= 600;
        const bool sameStyle = nameBold == st.bold && parsed.italic == st.italic;
        StyleRequest request;
        if (probe != nullptr) {
            request.weight = st.bold != nameBold ? (st.bold ? 700 : 400) : 0;
            request.italic = st.italic != parsed.italic ? (st.italic ? 1 : 0) : -1;
        } else {
            request.weight = st.bold ? 700 : 400;
            request.italic = st.italic ? 1 : 0;
        }
        const std::string family =
            !st.family.empty() ? st.family : (!parsed.family.empty() ? parsed.family : std::string("Arial"));
        int i = run.begin;
        while (i < run.end) {
            const EditChar& c = chars[static_cast<std::size_t>(i)];
            if (isHardBreak(c.ch)) {
                sh.chars[static_cast<std::size_t>(i)].emit = false;
                ++i;
                continue;
            }
            // Нетронутый символ — его кодом в исходном шрифте (лигатура — если все её символы на месте).
            auto reusable = [&](int k) {
                const EditChar& ch = chars[static_cast<std::size_t>(k)];
                if (!st.family.empty() || probe == nullptr || !sameStyle || ch.code.empty() ||
                    ch.group == 0) {
                    return 0;
                }
                const int g = ch.group;
                for (int t = 1; t < g; ++t) {
                    if (k + t >= run.end || chars[static_cast<std::size_t>(k + t)].group != 0 ||
                        !chars[static_cast<std::size_t>(k + t)].code.empty()) {
                        return 0;
                    }
                }
                return g;
            };
            if (const int g = reusable(i); g > 0) {
                rw::Shaped& s = sh.chars[static_cast<std::size_t>(i)];
                s.font = originalFont(st.fontResource, probe);
                s.code = c.code;
                s.w0 = c.w0;
                for (int t = 1; t < g; ++t) {
                    sh.chars[static_cast<std::size_t>(i + t)].font = s.font;
                    sh.chars[static_cast<std::size_t>(i + t)].emit = false;
                }
                i += g;
                continue;
            }
            // Набранное — через FontResolver до следующего нетронутого символа.
            int j = i;
            while (j < run.end && !isHardBreak(chars[static_cast<std::size_t>(j)].ch) && reusable(j) == 0) {
                ++j;
            }
            std::u32string text;
            for (int k = i; k < j; ++k) {
                text.push_back(chars[static_cast<std::size_t>(k)].ch);
            }
            const std::vector<ResolvedRun> resolved =
                probe != nullptr ? resolver.runs(*probe, text, request)
                                 : resolver.runsInFamily(family, text, request, st.fontName);
            int k = i;
            for (const ResolvedRun& rr : resolved) {
                int fontIdx = -1;
                const EmbeddedFont* ef = nullptr;
                if (rr.choice.newFont) {
                    const auto nf = newFont(rr.choice);
                    fontIdx = nf.first;
                    ef = nf.second;
                } else if (probe != nullptr) {
                    fontIdx = originalFont(st.fontResource, probe);
                }
                for (char32_t u : rr.text) {
                    rw::Shaped& s = sh.chars[static_cast<std::size_t>(k)];
                    s.font = fontIdx;
                    s.syntheticBold = rr.choice.syntheticBold;
                    s.syntheticItalic = rr.choice.syntheticItalic;
                    if (rr.choice.newFont) {
                        if (ef != nullptr) {
                            s.code = embedder.encode(*ef, u);
                            s.w0 = embedder.width(*ef, u);
                        } else {
                            const int gid = lib.gid(rr.choice.face, u);
                            s.w0 = std::trunc(1000.0f * lib.advance(rr.choice.face, gid)) / 1000.0f;
                        }
                        if (s.code.empty() && embed) {
                            // Глифа нет нигде: .notdef запасного (сообщение «нет глифа» — у резолвера).
                            s.code =
                                rr.choice.embed == EmbedKind::Cid ? std::string("\0\0", 2) : std::string("?");
                            s.w0 = std::trunc(1000.0f * lib.advance(rr.choice.face, 0)) / 1000.0f;
                        }
                    } else if (probe != nullptr) {
                        const std::optional<EncodedChar> enc = probe->encode(u);
                        if (enc) {
                            s.code = enc->bytes;
                            s.w0 = enc->w0;
                        } else {
                            s.emit = false;
                        }
                    } else {
                        s.emit = false;
                    }
                    if (fontIdx < 0) {
                        s.emit = false;
                    }
                    ++k;
                }
            }
            i = j;
        }
    }
    return sh;
}

std::vector<ReflowChar> Rewriter::Impl::measure(const ParagraphEditor& e, const rw::Shaping& sh) const {
    std::vector<ReflowChar> out;
    out.reserve(sh.chars.size());
    for (std::size_t i = 0; i < sh.chars.size(); ++i) {
        const EditChar& c = e.chars()[i];
        const CharStyle& st = e.styles()[static_cast<std::size_t>(c.style)];
        const rw::Shaped& s = sh.chars[i];
        ReflowChar r;
        r.ch = c.ch;
        r.size = st.size;
        if (s.emit && s.font >= 0) {
            const rw::ShapedFont& f = sh.fonts[static_cast<std::size_t>(s.font)];
            r.advance = naturalAdvance(c.ch, s.w0, st);
            r.width = s.w0 * st.size * st.hscale;
            r.ascent = std::max(0.0f, f.ascender * st.size + st.rise);
            r.descent = std::max(0.0f, -f.descender * st.size - st.rise);
        } else {
            r.advance = 0;
            r.width = 0;
            if (isHardBreak(c.ch)) {
                r.ascent = 0.8f * st.size;
                r.descent = 0.2f * st.size;
            } else {
                r.size = 0;  // продолжение лигатуры — строку не задаёт
            }
        }
        out.push_back(r);
    }
    return out;
}

std::vector<RectF> Rewriter::Impl::obstacles(const ParagraphEditor& e, const ContentModel& m) const {
    const std::set<int> own(e.source().ops.begin(), e.source().ops.end());
    std::vector<RectF> rects;
    for (const TextBlock& b : blocksForPage(m)) {
        bool mine = false;
        for (int si : b.spans) {
            mine = mine || own.count(m.spans()[static_cast<std::size_t>(si)].op) > 0;
        }
        if (!mine && b.bbox.valid()) {
            rects.push_back(b.bbox);
        }
    }
    return rects;
}

namespace rewriter_detail {

// Восстановление параметра состояния после своего текста: текст операции и её номер (порядок).
struct Restore {
    int order = -1;
    std::string text;
};

// Записать текст абзаца (строки раскладки) операторами; ctm — CTM пространства пользователя там, где текст.
struct Emitter {
    const ParagraphEditor& e;
    const Shaping& sh;
    const TextLayout& layout;
    Matrix toUser;  // PDF → пространство текста (обратная CTM)
    float ctmScale = 1;
    std::string lineWidthRestore;  // как вернуть исходную толщину линии (синтетика Tr 2 её меняет)

    std::string out;
    int shows = 0;  // операторов показа
    // Текущее состояние своей записи (неизвестно в начале — всё пишется).
    int font = -1;
    float size = std::numeric_limits<float>::quiet_NaN();
    float tc = std::numeric_limits<float>::quiet_NaN();
    float tw = std::numeric_limits<float>::quiet_NaN();
    float tz = std::numeric_limits<float>::quiet_NaN();
    float ts = std::numeric_limits<float>::quiet_NaN();
    int tr = -1;
    std::string fill;
    std::string stroke;
    float lw = std::numeric_limits<float>::quiet_NaN();  // NaN — исходная
    float skew = std::numeric_limits<float>::quiet_NaN();
    bool touchedFont = false, touchedTc = false, touchedTw = false, touchedTz = false, touchedTs = false,
         touchedTr = false, touchedFill = false, touchedStroke = false, touchedLw = false;
    std::vector<std::pair<RectF, std::string>> underlines;  // рамка (PDF) и цвет заливки

    bool singleSpace(const Shaped& s) const {
        return s.code.size() == 1 && s.code[0] == ' ' && !sh.fonts[static_cast<std::size_t>(s.font)].twoByte;
    }

    void run() {
        for (const LayoutLine& line : layout.lines) {
            int last = line.end;
            while (last > line.begin) {
                const char32_t ch = e.chars()[static_cast<std::size_t>(last - 1)].ch;
                if (isBreakSpace(ch) || isHardBreak(ch) ||
                    !sh.chars[static_cast<std::size_t>(last - 1)].emit) {
                    --last;
                } else {
                    break;
                }
            }
            bool lineStart = true;
            int i = line.begin;
            while (i < last) {
                if (!sh.chars[static_cast<std::size_t>(i)].emit) {
                    ++i;
                    continue;
                }
                int j = i + 1;
                while (j < last && (!sh.chars[static_cast<std::size_t>(j)].emit || sameSegment(i, j))) {
                    ++j;
                }
                segment(i, j, lineStart, line);
                lineStart = false;
                i = j;
            }
        }
    }

    bool sameSegment(int a, int b) const {
        const Shaped& x = sh.chars[static_cast<std::size_t>(a)];
        const Shaped& y = sh.chars[static_cast<std::size_t>(b)];
        return x.font == y.font && x.syntheticBold == y.syntheticBold &&
               x.syntheticItalic == y.syntheticItalic &&
               e.chars()[static_cast<std::size_t>(a)].style == e.chars()[static_cast<std::size_t>(b)].style;
    }

    void segment(int i, int j, bool lineStart, const LayoutLine& line) {
        const Shaped& first = sh.chars[static_cast<std::size_t>(i)];
        const CharStyle& st =
            e.styles()[static_cast<std::size_t>(e.chars()[static_cast<std::size_t>(i)].style)];
        const ShapedFont& sf = sh.fonts[static_cast<std::size_t>(first.font)];
        const float th = st.hscale > 0 ? st.hscale : 1.0f;
        const int render = st.render == 3 ? 3 : (first.syntheticBold ? (st.render == 1 ? 1 : 2) : st.render);
        const float segSkew = st.skew + (first.syntheticItalic ? 0.2f : 0.0f);
        if (font != first.font || size != st.size) {
            putName(out, sf.resource);
            out += ' ';
            putNumber(out, st.size);
            out += " Tf\n";
            font = first.font;
            size = st.size;
            touchedFont = true;
        }
        const float wantTz = th * 100.0f;
        if (tz != wantTz) {
            putNumber(out, wantTz);
            out += " Tz\n";
            tz = wantTz;
            touchedTz = true;
        }
        const float wantTc = st.charSpacing / th;
        if (tc != wantTc) {
            putNumber(out, wantTc);
            out += " Tc\n";
            tc = wantTc;
            touchedTc = true;
        }
        if (ts != st.rise) {
            putNumber(out, st.rise);
            out += " Ts\n";
            ts = st.rise;
            touchedTs = true;
        }
        if (tr != render) {
            out += std::to_string(render) + " Tr\n";
            tr = render;
            touchedTr = true;
        }
        const std::string wantFill = st.colorOps.empty() ? rgbOps(st.color) : st.colorOps;
        if (fill != wantFill) {
            out += wantFill + "\n";
            fill = wantFill;
            touchedFill = true;
        }
        if (render == 1 || render == 2) {
            const std::string wantStroke =
                !st.strokeOps.empty() && !first.syntheticBold ? st.strokeOps : strokeOf(wantFill);
            if (stroke != wantStroke) {
                out += wantStroke + "\n";
                stroke = wantStroke;
                touchedStroke = true;
            }
            if (first.syntheticBold) {
                const float wantLw = st.size * 0.03f / (ctmScale > 0 ? ctmScale : 1.0f);
                if (lw != wantLw) {
                    putNumber(out, wantLw);
                    out += " w\n";
                    lw = wantLw;
                    touchedLw = true;
                }
            } else if (!std::isnan(lw)) {
                out += lineWidthRestore + "\n";
                lw = std::numeric_limits<float>::quiet_NaN();
            }
        }
        // Tw — растяжка пробелов строки, если у всех однобайтовых пробелов сегмента она одна; иначе — сдвиги
        // TJ.
        std::optional<float> extra;
        bool even = true;
        for (int k = i; k < j; ++k) {
            const Shaped& s = sh.chars[static_cast<std::size_t>(k)];
            if (!s.emit || !singleSpace(s)) {
                continue;
            }
            const float natural = (s.w0 * st.size + wantTc) * th;
            const float x = layout.advance[static_cast<std::size_t>(k)] - natural;
            if (!extra) {
                extra = x;
            } else if (std::fabs(*extra - x) > 0.001f) {
                even = false;
            }
        }
        if (extra) {
            const float wantTw = even ? *extra / th : 0.0f;
            if (tw != wantTw) {
                putNumber(out, wantTw);
                out += " Tw\n";
                tw = wantTw;
                touchedTw = true;
            }
        }
        const float baseline = line.baseline;
        float pen = layout.x[static_cast<std::size_t>(i)];
        if (lineStart || skew != segSkew) {
            const Matrix text{1, 0, segSkew, 1, pen, -baseline};
            putMatrix(out, concat(text, toUser));
            out += '\n';
            skew = segSkew;
        }
        // Строка: коды шрифта, сдвиг TJ там, где перо расходится с раскладкой.
        std::string items;
        std::string str;
        bool numbers = false;
        auto flush = [&] {
            if (!str.empty()) {
                putLiteral(items, str);
                str.clear();
            }
        };
        const float twNow = std::isnan(tw) ? 0.0f : tw;
        for (int k = i; k < j; ++k) {
            const Shaped& s = sh.chars[static_cast<std::size_t>(k)];
            if (!s.emit) {
                continue;
            }
            const float expected = layout.x[static_cast<std::size_t>(k)];
            const float d = expected - pen;
            if (std::fabs(d) > kPenTolerance) {
                const float n = -d * 1000.0f / (st.size * th);
                flush();
                items += ' ';
                putNumber(items, n);
                items += ' ';
                numbers = true;
                pen += -n * 0.001f * st.size * th;
            }
            str += s.code;
            pen += (s.w0 * st.size + wantTc) * th + (singleSpace(s) ? twNow * th : 0.0f);
        }
        flush();
        if (numbers) {
            out += '[' + items + "] TJ\n";
        } else {
            out += items + " Tj\n";
        }
        ++shows;
        if (st.underline) {
            const float x0 = layout.x[static_cast<std::size_t>(i)];
            int lastVisible = j - 1;
            while (lastVisible > i && !sh.chars[static_cast<std::size_t>(lastVisible)].emit) {
                --lastVisible;
            }
            const float x1 = layout.x[static_cast<std::size_t>(lastVisible)] +
                             sh.chars[static_cast<std::size_t>(lastVisible)].w0 * st.size * th;
            const float thick = std::max(0.5f, st.size * 0.05f);
            const float top = -baseline - st.rise - st.size * 0.1f;  // PDF: y вверх
            underlines.push_back({RectF{x0, top - thick, x1, top}, wantFill});
        }
    }
};

}  // namespace rewriter_detail

RewriteReport Rewriter::Impl::rewrite(ParagraphEditor& e, const RewriteOptions& options) {
    RewriteReport report;
    if (protectedDocument()) {
        report.error = RewriteError::Signed;
        report.message = "document is signed or has /Perms: the text editor is disabled";
        return report;
    }
    const int page = e.source().page;
    ContentModel m = ContentModel::build(doc, page);
    if (auto err = validate(e, m, report.message)) {
        report.error = *err;
        return report;
    }
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    const bool appendMode = e.source().ops.empty();
    const int anchor = appendMode ? -1 : e.source().ops.front();
    const TextSpan* anchorSpan =
        appendMode
            ? nullptr
            : &m.spans()[static_cast<std::size_t>(m.operations()[static_cast<std::size_t>(anchor)].span)];
    const Matrix ctm = anchorSpan != nullptr ? anchorSpan->ctm : Matrix{};
    resolver.clearNotices();

    try {
        Journal::Operation op(doc, options.operation);
        rw::Shaping sh = shape(e, m, true);
        const std::vector<RectF> others = obstacles(e, m);
        const ReflowResult flow = reflow(measure(e, sh), reflowParams(e, toLayoutRects(e, others)));
        rw::Emitter em{e, sh, flow.layout, invertMatrix(ctm), rw::ctmScale(ctm), {}};
        if (anchorSpan != nullptr) {
            em.lineWidthRestore =
                anchorSpan->refs.lineWidth >= 0 ? m.opText(anchorSpan->refs.lineWidth) : "1 w";
        } else {
            em.lineWidthRestore = "1 w";
        }
        em.run();
        const bool emitted = em.shows > 0;

        std::map<int, std::string> repl;  // операция → новое содержимое (без обрамляющих пробелов)
        std::set<int> block(e.source().ops.begin(), e.source().ops.end());
        auto operand = [&](const Operation& o, std::size_t index) {
            std::size_t k = 0;
            for (const Operand& x : o.operands) {
                if (x.isNumber() && k++ == index) {
                    return static_cast<float>(x.number);
                }
            }
            return 0.0f;
        };
        auto ghost = [&](const Operation& o) -> std::string {
            if (o.kind == OpKind::NextLineShow) {
                return "T*";
            }
            if (o.kind == OpKind::NextLineShowSpaced) {
                return rw::number(operand(o, 0)) + " Tw " + rw::number(operand(o, 1)) + " Tc T*";
            }
            return {};
        };
        // Восстановление состояния после своего текста — теми же операциями, что его задали.
        std::string restore;
        if (anchorSpan != nullptr && emitted) {
            const StateRefs& refs = anchorSpan->refs;
            std::vector<rw::Restore> items;
            auto opText = [&](int ref, const char* fallback, int numberIndex, const char* op) -> std::string {
                if (ref < 0) {
                    return fallback;
                }
                const Operation& o = m.operations()[static_cast<std::size_t>(ref)];
                if (o.kind == OpKind::NextLineShowSpaced) {
                    return rw::number(operand(o, static_cast<std::size_t>(numberIndex))) + " " + op;
                }
                return m.opText(ref);
            };
            if (em.touchedFont && refs.font >= 0) {
                items.push_back({refs.font, m.opText(refs.font)});
            }
            if (em.touchedTc) {
                items.push_back({refs.charSpace, opText(refs.charSpace, "0 Tc", 1, "Tc")});
            }
            if (em.touchedTw) {
                items.push_back({refs.wordSpace, opText(refs.wordSpace, "0 Tw", 0, "Tw")});
            }
            if (em.touchedTz) {
                items.push_back({refs.scale, opText(refs.scale, "100 Tz", 0, "Tz")});
            }
            if (em.touchedTs) {
                items.push_back({refs.rise, opText(refs.rise, "0 Ts", 0, "Ts")});
            }
            if (em.touchedTr) {
                items.push_back({refs.render, opText(refs.render, "0 Tr", 0, "Tr")});
            }
            if (em.touchedFill) {
                items.push_back({std::max(refs.fillSpace, refs.fill), m.colorOps(refs, false)});
            }
            if (em.touchedStroke) {
                items.push_back({std::max(refs.strokeSpace, refs.stroke), m.colorOps(refs, true)});
            }
            if (em.touchedLw) {
                items.push_back({refs.lineWidth, em.lineWidthRestore});
            }
            std::stable_sort(items.begin(), items.end(),
                             [](const rw::Restore& a, const rw::Restore& b) { return a.order < b.order; });
            for (const rw::Restore& r : items) {
                restore += r.text + "\n";
            }
        }

        // Текстовые объекты абзаца: призраки, сдвиги пера для чужого текста, Tlm, пустые объекты.
        std::set<int> objects;
        for (int o : e.source().ops) {
            objects.insert(m.operations()[static_cast<std::size_t>(o)].textObject);
        }
        const int anchorObject =
            anchor >= 0 ? m.operations()[static_cast<std::size_t>(anchor)].textObject : -2;
        std::string anchorTail;  // после восстановления: Tlm и сдвиг пера
        for (int t : objects) {
            int from = 0;
            int to = 0;
            if (t >= 0) {
                const TextObject& obj = m.textObjects()[static_cast<std::size_t>(t)];
                from = obj.begin;
                to = obj.end >= 0 ? obj.end : static_cast<int>(m.operations().size()) - 1;
            } else {
                from = to = anchor;  // оператор показа вне BT — только он
            }
            const bool anchorHere = t == anchorObject && emitted;
            bool anchorSeen = false;
            bool needTlm = false;
            bool reset = false;
            float pending = 0;
            int carrier = -1;
            float carrierSize = 1;
            float carrierScale = 1;
            bool allMine = true;
            bool split = false;
            for (int k = from; k <= to; ++k) {
                const Operation& o = m.operations()[static_cast<std::size_t>(k)];
                split = split || o.split;
                const bool dependent = anchorHere && anchorSeen && !reset;
                if (block.count(k) > 0) {
                    const TextSpan& s = m.spans()[static_cast<std::size_t>(o.span)];
                    if (k == anchor && emitted) {
                        anchorSeen = true;
                        pending = rw::lineOffset(s) + s.advance;
                    } else {
                        if (o.kind == OpKind::NextLineShow || o.kind == OpKind::NextLineShowSpaced) {
                            needTlm = needTlm || dependent;
                            pending = s.advance;
                        } else {
                            pending += s.advance;
                        }
                        repl[k] = ghost(o);
                    }
                    carrier = k;
                    carrierSize = s.size;
                    carrierScale = s.scale;
                    continue;
                }
                if (isShowOp(o.kind)) {
                    if (o.span >= 0) {
                        allMine = false;
                    }
                    needTlm = needTlm || dependent;
                    if (o.kind == OpKind::NextLineShow || o.kind == OpKind::NextLineShowSpaced) {
                        pending = 0;
                    } else if (std::fabs(pending) > 1e-6f && carrier >= 0 &&
                               carrierSize * carrierScale != 0) {
                        const float n = -pending * 1000.0f / (carrierSize * carrierScale);
                        std::string adj = "[" + rw::number(n) + "] TJ";
                        if (carrier == anchor && emitted) {
                            anchorTail += adj + "\n";
                        } else {
                            repl[carrier] += (repl[carrier].empty() ? "" : " ") + adj;
                        }
                        pending = 0;
                    }
                    continue;
                }
                switch (o.kind) {
                    case OpKind::Move:
                    case OpKind::MoveLeading:
                    case OpKind::NextLine:
                        needTlm = needTlm || dependent;
                        pending = 0;
                        break;
                    case OpKind::TextMatrix:
                        pending = 0;
                        reset = reset || anchorSeen;
                        break;
                    default:
                        break;
                }
            }
            if (anchorHere && needTlm) {
                std::string tlm;
                rw::putMatrix(tlm, anchorSpan->tlm);
                anchorTail = tlm + "\n" + anchorTail;
            }
            // Объект без своего текста убирается целиком: позиционирование и BT/ET — вне BT незаконны, а Tf,
            // Tc, цвет и пометки — законны и остаются (их ждёт следующий текст).
            if (t >= 0 && allMine && !anchorHere && !split &&
                m.textObjects()[static_cast<std::size_t>(t)].end >= 0) {
                for (int k = from; k <= to; ++k) {
                    const Operation& o = m.operations()[static_cast<std::size_t>(k)];
                    switch (o.kind) {
                        case OpKind::BeginText:
                        case OpKind::EndText:
                        case OpKind::Move:
                        case OpKind::TextMatrix:
                        case OpKind::NextLine:
                        case OpKind::ShowText:
                        case OpKind::ShowArray:
                        case OpKind::NextLineShow:
                            repl[k] = std::string();
                            break;
                        case OpKind::MoveLeading:
                            repl[k] = rw::number(-operand(o, 1)) + " TL";
                            break;
                        case OpKind::NextLineShowSpaced:
                            repl[k] = rw::number(operand(o, 0)) + " Tw " + rw::number(operand(o, 1)) + " Tc";
                            break;
                        default:
                            break;
                    }
                }
                ++report.textObjectsRemoved;
            }
        }
        // Подчёркивание — после ET объекта своего текста, в своей пометке.
        std::string underline;
        if (!em.underlines.empty()) {
            underline = "/OttU BMC q\n";
            for (const auto& [r, color] : em.underlines) {
                std::string rect;
                const PointF a = mapPoint({r.x0, r.y0}, em.toUser);
                const PointF b = mapPoint({r.x1, r.y1}, em.toUser);
                underline += color + "\n";
                rw::putNumber(rect, std::min(a.x, b.x));
                rect += ' ';
                rw::putNumber(rect, std::min(a.y, b.y));
                rect += ' ';
                rw::putNumber(rect, std::fabs(b.x - a.x));
                rect += ' ';
                rw::putNumber(rect, std::fabs(b.y - a.y));
                underline += rect + " re f\n";
            }
            underline += "Q EMC";
        }

        ContentModel& out = m;
        auto frame = [&](int k, const std::string& content) {
            const Operation& o = m.operations()[static_cast<std::size_t>(k)];
            std::string bytes(m.raw(k).substr(0, o.tokenOffset - o.offset));
            if (bytes.empty()) {
                bytes = " ";
            }
            if (!content.empty()) {
                bytes += content + "\n";
            }
            out.replace(k, std::move(bytes));
        };
        if (anchor >= 0) {
            if (emitted) {
                frame(anchor, em.out + restore + anchorTail);
            } else {
                repl.emplace(anchor, ghost(m.operations()[static_cast<std::size_t>(anchor)]));
            }
        }
        for (const auto& [k, content] : repl) {
            if (k != anchor || !emitted) {
                frame(k, content);
            }
        }
        if (!underline.empty() && anchor >= 0) {
            const int t = anchorObject;
            const int et = t >= 0 ? m.textObjects()[static_cast<std::size_t>(t)].end : anchor;
            if (et >= 0) {
                const auto it = out.replacements().find(et);
                std::string base = it != out.replacements().end() ? it->second : std::string(m.raw(et));
                out.replace(et, base + "\n" + underline + "\n");
            }
        }

        // Запись: изменённые потоки страницы; общий с другой страницей — копия для этой.
        std::vector<int> streams;
        for (std::size_t s = 0; s < m.streams().size(); ++s) {
            if (!m.streams()[s].form && m.streams()[s].objnum > 0 && out.modified(static_cast<int>(s))) {
                streams.push_back(static_cast<int>(s));
            }
        }
        std::set<int> shared;
        int pages = 0;
        mu::call(ctx, [&] { pages = pdf_count_pages(ctx, pdf); });
        for (int p = 0; p < pages; ++p) {
            if (p == page) {
                continue;
            }
            pdf_obj* c = nullptr;
            int count = 0;
            mu::call(ctx, [&] {
                c = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, p), PDF_NAME(Contents));
                count = pdf_is_array(ctx, c) ? pdf_array_len(ctx, c) : -1;
            });
            for (int i = count < 0 ? -1 : 0; i < count || (count < 0 && i < 0); ++i) {
                int num = 0;
                mu::call(ctx, [&] { num = pdf_to_num(ctx, i < 0 ? c : pdf_array_get(ctx, c, i)); });
                if (num > 0) {
                    shared.insert(num);
                }
            }
        }
        auto pack = [&](const std::string& bytes, mu::BufferHandle& packed) {
            mu::BufferHandle plain(ctx);
            mu::call(ctx, [&] {
                plain.reset(fz_new_buffer_from_copied_data(
                    ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
                size_t length = 0;
                unsigned char* data =
                    fz_new_deflated_data_from_buffer(ctx, &length, plain, FZ_DEFLATE_DEFAULT);
                packed.reset(fz_new_buffer_from_data(ctx, data, length));
            });
        };
        auto newStream = [&](const std::string& bytes, mu::PdfObjHandle& ref) {
            mu::BufferHandle packed(ctx);
            pack(bytes, packed);
            mu::PdfObjHandle dict(ctx);
            mu::call(ctx, [&] {
                dict.reset(pdf_new_dict(ctx, pdf, 2));
                pdf_dict_put(ctx, dict, PDF_NAME(Filter), PDF_NAME(FlateDecode));
                ref.reset(pdf_add_stream(ctx, pdf, packed, dict, 1));
            });
        };
        for (int s : streams) {
            const ContentStream& cs = m.streams()[static_cast<std::size_t>(s)];
            const std::string bytes = out.serialize(s);
            if (shared.count(cs.objnum) > 0) {
                mu::PdfObjHandle ref(ctx);
                newStream(bytes, ref);
                const int objnum = cs.objnum;
                mu::call(ctx, [&] {
                    pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
                    pdf_obj* c = pdf_dict_get(ctx, pageObj, PDF_NAME(Contents));
                    if (pdf_is_array(ctx, c)) {
                        for (int i = 0; i < pdf_array_len(ctx, c); ++i) {
                            if (pdf_to_num(ctx, pdf_array_get(ctx, c, i)) == objnum) {
                                pdf_array_put(ctx, c, i, ref);
                            }
                        }
                    } else {
                        pdf_dict_put(ctx, pageObj, PDF_NAME(Contents), ref);
                    }
                });
            } else {
                mu::BufferHandle packed(ctx);
                pack(bytes, packed);
                mu::PdfObjHandle ref(ctx);
                mu::call(ctx, [&] {
                    ref.reset(pdf_new_indirect(ctx, pdf, cs.objnum, cs.gen));
                    pdf_dict_put(ctx, ref, PDF_NAME(Filter), PDF_NAME(FlateDecode));
                    pdf_dict_del(ctx, ref, PDF_NAME(DecodeParms));
                    pdf_update_stream(ctx, pdf, ref, packed, 1);
                });
            }
            ++report.streams;
        }
        // Абзац без операторов (стёрт раньше) — новым потоком в конце /Contents; исходные потоки — внутри q …
        // Q.
        if (appendMode && emitted) {
            std::string tail = "Q\nBT\n" + em.out + "ET\n";
            if (!underline.empty()) {
                tail += underline + "\n";
            }
            mu::PdfObjHandle open(ctx);
            mu::PdfObjHandle close(ctx);
            newStream("q\n", open);
            newStream(tail, close);
            mu::PdfObjHandle array(ctx);
            mu::call(ctx, [&] {
                pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
                pdf_obj* c = pdf_dict_get(ctx, pageObj, PDF_NAME(Contents));
                if (pdf_is_array(ctx, c)) {
                    pdf_array_insert(ctx, c, open, 0);
                    pdf_array_push(ctx, c, close);
                } else {
                    array.reset(pdf_new_array(ctx, pdf, 3));
                    pdf_array_push(ctx, array, open);
                    if (c != nullptr) {
                        pdf_array_push(ctx, array, c);
                    }
                    pdf_array_push(ctx, array, close);
                    pdf_dict_put(ctx, pageObj, PDF_NAME(Contents), array);
                }
            });
            report.streams += 2;
            report.appended = true;
        }
        op.invalidate(page);
        op.markHeavy();  // правка редактора — полная запись файла (эталон 4.4)
        op.commit();

        // Новый источник: операторы показа своей записи в новой модели.
        const ContentModel next = ContentModel::build(doc, page);
        ParagraphSource source;
        source.page = page;
        int start = 0;
        if (report.appended) {
            int last = -1;
            for (std::size_t s = 0; s < next.streams().size(); ++s) {
                if (!next.streams()[s].form) {
                    last = static_cast<int>(s);
                }
            }
            start = last >= 0 && !next.streams()[static_cast<std::size_t>(last)].ops.empty()
                        ? next.streams()[static_cast<std::size_t>(last)].ops.front()
                        : static_cast<int>(next.operations().size());
        } else {
            start = anchor;
        }
        if (emitted) {
            for (int k = start; k < static_cast<int>(next.operations().size()) &&
                                static_cast<int>(source.ops.size()) < em.shows;
                 ++k) {
                const Operation& o = next.operations()[static_cast<std::size_t>(k)];
                if (isShowOp(o.kind) && !next.streams()[static_cast<std::size_t>(o.stream)].form) {
                    if (o.span >= 0) {
                        source.ops.push_back(k);
                        source.text += next.spanText(o.span);
                    } else {
                        source.ops.clear();  // пустой показ среди своих — так не пишем; источник неизвестен
                        break;
                    }
                }
            }
        }
        // Рамка нового абзаца (координаты страницы).
        RectF bbox = RectF::none();
        if (emitted) {
            const RectF b = flow.layout.bounds;
            for (const PointF p :
                 {PointF{b.x0, b.y0}, PointF{b.x1, b.y0}, PointF{b.x0, b.y1}, PointF{b.x1, b.y1}}) {
                const PointF q = mapPoint(p, e.geometry().toPage);
                bbox = rectUnion(bbox, RectF{q.x, q.y, q.x, q.y});
            }
            if (!em.underlines.empty()) {
                bbox.y1 += 2;
            }
        }
        source.bbox = bbox;
        report.bbox = bbox;
        report.dirty = rectUnion(e.source().bbox, bbox);
        report.lines = static_cast<int>(flow.layout.lines.size());
        for (int k : flow.overlaps) {
            if (k >= 0 && k < static_cast<int>(others.size())) {
                report.overlaps.push_back(others[static_cast<std::size_t>(k)]);
            }
        }
        report.notices = resolver.notices();
        e.setLayout(flow.layout);
        e.markWritten(std::move(source));
    } catch (const MuError& err) {
        report.error = RewriteError::Failed;
        report.message = err.what();
        spdlog::warn("text editor: rewrite failed on page {}: {}", page + 1, err.what());
    } catch (const std::runtime_error& err) {
        report.error = RewriteError::Failed;
        report.message = err.what();
        spdlog::warn("text editor: rewrite failed on page {}: {}", page + 1, err.what());
    }
    return report;
}

Rewriter::Rewriter(Document& doc, FontLibrary& library, SystemFonts& system, const FallbackFonts& fallback)
    : d_(std::make_unique<Impl>(doc, library, system, fallback)) {}

Rewriter::~Rewriter() = default;

RewriteReport Rewriter::rewrite(ParagraphEditor& editor, const RewriteOptions& options) {
    return d_->rewrite(editor, options);
}

ReflowResult Rewriter::layout(ParagraphEditor& editor) {
    const ContentModel m = ContentModel::build(d_->doc, editor.source().page);
    const rw::Shaping sh = d_->shape(editor, m, false);
    ReflowResult flow = reflow(d_->measure(editor, sh),
                               reflowParams(editor, toLayoutRects(editor, d_->obstacles(editor, m))));
    editor.setLayout(flow.layout);
    return flow;
}

FontEmbedder& Rewriter::embedder() noexcept { return d_->embedder; }

SubsetReport Rewriter::subsetFonts() { return d_->embedder.subset(); }

void Rewriter::setFallback(const FallbackFonts& fallback) { d_->resolver.setFallback(fallback); }

}  // namespace ot::edit
