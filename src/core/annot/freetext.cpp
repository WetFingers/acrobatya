// Текстовое поле и выноска: имена шрифтов, перенос строк и свой внешний вид FreeText (см. freetext.h).
#include "core/annot/freetext.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/annot/annot_internal.h"
#include "core/doc/document.h"
#include "core/edit/font_embed.h"
#include "core/edit/system_fonts.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/utf8.h"

namespace ot::annot {

namespace {

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

const TextFont& textFont(std::string_view name) {
    for (const TextFont& f : kTextFonts) {
        if (sameName(name, f.family) || sameName(name, f.resource) || sameName(name, f.postscript)) {
            return f;
        }
    }
    if (sameName(name, kSymbolFont.family) || sameName(name, kSymbolFont.resource)) {
        return kSymbolFont;
    }
    return kTextFonts[1];
}

std::string textFontFamily(std::string_view name) {
    if (sameName(name, "Helv") || sameName(name, "Helvetica")) {
        return "Arial";
    }
    if (sameName(name, "TiRo") || sameName(name, "Times") || sameName(name, "Times-Roman")) {
        return "Times New Roman";
    }
    if (sameName(name, "Cour") || sameName(name, "Courier")) {
        return "Courier New";
    }
    return textFont(name).family;
}

std::vector<std::u32string> wrapText(std::u32string_view text, float width,
                                     const std::function<float(char32_t)>& advance) {
    std::vector<std::u32string> lines;
    auto push = [&lines](std::u32string line) {
        while (!line.empty() && line.back() == U' ') {
            line.pop_back();
        }
        lines.push_back(std::move(line));
    };
    std::size_t start = 0;
    while (true) {
        std::size_t end = text.find_first_of(U"\r\n", start);
        const std::u32string_view para =
            text.substr(start, end == std::u32string_view::npos ? std::u32string_view::npos : end - start);
        std::u32string line;
        float lineWidth = 0;
        std::size_t i = 0;
        while (i < para.size()) {
            const bool space = para[i] == U' ';
            std::size_t j = i;
            while (j < para.size() && (para[j] == U' ') == space) {
                ++j;
            }
            const std::u32string_view token = para.substr(i, j - i);
            float tokenWidth = 0;
            for (const char32_t c : token) {
                tokenWidth += advance(c);
            }
            if (space) {
                if (!line.empty()) {
                    line += token;
                    lineWidth += tokenWidth;
                } else if (i == 0) {
                    line += token;  // отступ в начале абзаца
                    lineWidth += tokenWidth;
                }
            } else {
                if (!line.empty() && lineWidth + tokenWidth > width) {
                    push(std::move(line));
                    line.clear();
                    lineWidth = 0;
                }
                if (tokenWidth > width) {
                    for (const char32_t c : token) {
                        const float w = advance(c);
                        if (!line.empty() && lineWidth + w > width) {
                            push(std::move(line));
                            line.clear();
                            lineWidth = 0;
                        }
                        line.push_back(c);
                        lineWidth += w;
                    }
                } else {
                    line += token;
                    lineWidth += tokenWidth;
                }
            }
            i = j;
        }
        push(std::move(line));
        if (end == std::u32string_view::npos) {
            break;
        }
        start = end + 1;
        if (text[end] == U'\r' && start < text.size() && text[start] == U'\n') {
            ++start;
        }
    }
    return lines;
}

namespace detail {

namespace {

// Знаки, которые шрифт поля несёт всегда: ASCII, Latin-1, русский алфавит, тире, кавычки, многоточие, €, ₽, №
// — правка поля в Acrobat и наши следующие поля обходятся без нового шрифта; прочие знаки добавляются по
// тексту.
constexpr std::pair<char32_t, char32_t> kAlwaysEmbedded[] = {
    {0x20, 0x7E},     {0xA0, 0xFF},     {0x401, 0x401},   {0x410, 0x44F},
    {0x451, 0x451},   {0x2013, 0x2014}, {0x2018, 0x201E}, {0x2022, 0x2022},
    {0x2026, 0x2026}, {0x20AC, 0x20AC}, {0x20BD, 0x20BD}, {0x2116, 0x2116},
};

// Состояние поля, прочитанное внутри mu::call (строки копируются сразу).
struct FreeTextState {
    std::string contents;
    std::string daFont;
    float size = 12;
    int colorN = 1;
    std::array<float, 4> color{};
    int quadding = 0;
    int fillN = 0;
    std::array<float, 4> fill{};
    float border = 0;
    std::vector<float> dash;
    float opacity = 1;
    fz_rect design{};  // поле в пунктах PDF
    std::vector<fz_point> callout;
    bool isCallout = false;
    enum pdf_line_ending ending = PDF_ANNOT_LE_NONE;
    int rotate = 0;
};

std::optional<edit::SystemFace> faceOf(const TextFont& font) {
    const std::filesystem::path file = edit::FakeSystemFonts::defaultDir() / font.file;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return std::nullopt;
    }
    edit::SystemFace face;
    face.family = font.family;
    face.postscript = font.postscript;
    face.file = file;
    return face;
}

void readState(fz_context* ctx, pdf_annot* a, pdf_page* page, FreeTextState& s) {
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    const char* contents = pdf_annot_contents(ctx, a);
    s.contents = contents != nullptr ? contents : "";
    char font[64] = {};
    float color[4] = {};
    pdf_annot_default_appearance_unmapped(ctx, a, font, sizeof font, &s.size, &s.colorN, color);
    s.daFont = font;
    std::copy(color, color + 4, s.color.begin());
    if (s.size <= 0) {
        s.size = 12;
    }
    s.quadding = std::clamp(pdf_annot_quadding(ctx, a), 0, 2);
    float fill[4] = {};
    pdf_annot_color(ctx, a, &s.fillN, fill);
    std::copy(fill, fill + 4, s.fill.begin());
    if (pdf_dict_get(ctx, obj, PDF_NAME(BS)) != nullptr ||
        pdf_dict_get(ctx, obj, PDF_NAME(Border)) != nullptr) {
        s.border = pdf_annot_border_width(ctx, a);
        for (int i = 0; i < pdf_annot_border_dash_count(ctx, a); ++i) {
            s.dash.push_back(pdf_annot_border_dash_item(ctx, a, i));
        }
    }
    s.opacity = pdf_annot_opacity(ctx, a);
    s.design = freeTextDesign(ctx, obj);
    s.isCallout = pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(IT)), PDF_NAME(FreeTextCallout)) != 0;
    pdf_obj* cl = pdf_dict_get(ctx, obj, PDF_NAME(CL));
    const int n = pdf_array_len(ctx, cl);
    if (s.isCallout && (n == 4 || n == 6)) {
        for (int i = 0; i < n; i += 2) {
            s.callout.push_back(
                fz_make_point(pdf_array_get_real(ctx, cl, i), pdf_array_get_real(ctx, cl, i + 1)));
        }
        pdf_obj* le = pdf_dict_get(ctx, obj, PDF_NAME(LE));
        if (pdf_is_array(ctx, le)) {
            le = pdf_array_get(ctx, le, 0);
        }
        s.ending = pdf_line_ending_from_name(ctx, le);
    }
    int rotate = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, page->obj, PDF_NAME(Rotate))) % 360;
    if (rotate < 0) {
        rotate += 360;
    }
    s.rotate = rotate - rotate % 90;
}

// Глифы /W (ширины) шрифта Type0 — какие глифы в нём есть.
std::set<int> fontGids(fz_context* ctx, pdf_obj* font) {
    std::set<int> out;
    pdf_obj* w = pdf_dict_get(ctx, pdf_array_get(ctx, pdf_dict_get(ctx, font, PDF_NAME(DescendantFonts)), 0),
                              PDF_NAME(W));
    const int n = pdf_array_len(ctx, w);
    for (int i = 0; i < n;) {
        const int first = pdf_array_get_int(ctx, w, i);
        pdf_obj* next = pdf_array_get(ctx, w, i + 1);
        if (pdf_is_array(ctx, next)) {
            for (int k = 0; k < pdf_array_len(ctx, next); ++k) {
                out.insert(first + k);
            }
            i += 2;
        } else {
            const int last = pdf_to_int(ctx, next);
            for (int c = first; c <= last && c - first < 0x10000; ++c) {
                out.insert(c);
            }
            i += 3;
        }
    }
    return out;
}

// Шрифт поля в /AcroForm /DR /Font /<resource>: прежний, если в нём есть все глифы needed, иначе — новый
// (полный шрифт урезан до kAlwaysEmbedded и needed) и /DR указывает на него. Номер объекта словаря шрифта.
int fieldFont(Document& doc, edit::FontLibrary& lib, const edit::SystemFace& face, const TextFont& font,
              const std::set<int>& needed) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int existing = 0;
    std::set<int> have;
    const char* resource = font.resource;
    mu::call(ctx, [&] {
        pdf_obj* fonts = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/DR/Font");
        pdf_obj* ref = pdf_dict_gets(ctx, fonts, resource);
        pdf_obj* dict = pdf_resolve_indirect(ctx, ref);
        if (pdf_is_indirect(ctx, ref) &&
            pdf_name_eq(ctx, pdf_dict_get(ctx, dict, PDF_NAME(Subtype)), PDF_NAME(Type0)) != 0) {
            const char* base = pdf_dict_get_name(ctx, dict, PDF_NAME(BaseFont));
            const std::string_view name = base != nullptr ? base : "";
            const std::string_view ps = font.postscript;
            if (name.size() >= ps.size() && name.substr(name.size() - ps.size()) == ps) {
                existing = pdf_to_num(ctx, ref);
                have = fontGids(ctx, dict);
            }
        }
    });
    if (existing > 0 && std::includes(have.begin(), have.end(), needed.begin(), needed.end())) {
        return existing;
    }
    edit::FontEmbedder embedder(doc, lib);
    const int objnum = embedder.fontObject(face, edit::EmbedKind::Cid);
    std::set<int> gids = needed;
    for (const auto& [first, last] : kAlwaysEmbedded) {
        if (&font == &kSymbolFont) {
            break;  // шрифт отметок — только знаки текста
        }
        for (char32_t u = first; u <= last; ++u) {
            if (const int g = lib.gid(face, u); g > 0) {
                gids.insert(g);
            }
        }
    }
    edit::subsetFont(doc, objnum, gids);
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* form = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
        if (!pdf_is_dict(ctx, form)) {
            pdf_obj* created = pdf_add_new_dict(ctx, pdf, 2);
            pdf_dict_put_drop(ctx, created, PDF_NAME(Fields), pdf_new_array(ctx, pdf, 0));
            pdf_dict_put_drop(ctx, root, PDF_NAME(AcroForm), created);
            form = pdf_dict_get(ctx, root, PDF_NAME(AcroForm));
        }
        pdf_obj* dr = pdf_dict_get(ctx, form, PDF_NAME(DR));
        if (!pdf_is_dict(ctx, dr)) {
            dr = pdf_dict_put_dict(ctx, form, PDF_NAME(DR), 1);
        }
        pdf_obj* fonts = pdf_dict_get(ctx, dr, PDF_NAME(Font));
        if (!pdf_is_dict(ctx, fonts)) {
            fonts = pdf_dict_put_dict(ctx, dr, PDF_NAME(Font), 2);
        }
        pdf_dict_puts_drop(ctx, fonts, resource, pdf_new_indirect(ctx, pdf, objnum, 0));
    });
    return objnum;
}

// Оператор цвета (заливка — rg/g/k, обводка — RG/G/K).
void appendColor(fz_context* ctx, fz_buffer* buf, int n, const std::array<float, 4>& c, bool stroke) {
    if (n == 1) {
        fz_append_printf(ctx, buf, "%g %s\n", c[0], stroke ? "G" : "g");
    } else if (n == 3) {
        fz_append_printf(ctx, buf, "%g %g %g %s\n", c[0], c[1], c[2], stroke ? "RG" : "rg");
    } else if (n == 4) {
        fz_append_printf(ctx, buf, "%g %g %g %g %s\n", c[0], c[1], c[2], c[3], stroke ? "K" : "k");
    } else {
        fz_append_printf(ctx, buf, "0 %s\n", stroke ? "G" : "g");
    }
}

// Окончание линии выноски у конца стрелки tip (from — соседняя точка линии); w — толщина, size — размер
// знака.
void appendEnding(fz_context* ctx, fz_buffer* buf, enum pdf_line_ending ending, fz_point tip, fz_point from,
                  float size) {
    float dx = tip.x - from.x;
    float dy = tip.y - from.y;
    const float len = std::hypot(dx, dy);
    if (len <= 0.001f || ending == PDF_ANNOT_LE_NONE) {
        return;
    }
    dx /= len;
    dy /= len;
    if (ending == PDF_ANNOT_LE_R_OPEN_ARROW || ending == PDF_ANNOT_LE_R_CLOSED_ARROW) {
        dx = -dx;
        dy = -dy;
    }
    const float c = std::cos(0.5236f);  // 30°
    const float s = std::sin(0.5236f);
    const fz_point a = fz_make_point(tip.x - size * (dx * c - dy * s), tip.y - size * (dy * c + dx * s));
    const fz_point b = fz_make_point(tip.x - size * (dx * c + dy * s), tip.y - size * (dy * c - dx * s));
    const float r = size / 2;
    switch (ending) {
        case PDF_ANNOT_LE_OPEN_ARROW:
        case PDF_ANNOT_LE_R_OPEN_ARROW:
            fz_append_printf(ctx, buf, "%g %g m %g %g l %g %g l S\n", a.x, a.y, tip.x, tip.y, b.x, b.y);
            break;
        case PDF_ANNOT_LE_CLOSED_ARROW:
        case PDF_ANNOT_LE_R_CLOSED_ARROW:
            fz_append_printf(ctx, buf, "%g %g m %g %g l %g %g l h B\n", a.x, a.y, tip.x, tip.y, b.x, b.y);
            break;
        case PDF_ANNOT_LE_SQUARE:
            fz_append_printf(ctx, buf, "%g %g %g %g re B\n", tip.x - r, tip.y - r, size, size);
            break;
        case PDF_ANNOT_LE_CIRCLE: {
            const float m = r * 0.5523f;
            fz_append_printf(ctx, buf, "%g %g m %g %g %g %g %g %g c %g %g %g %g %g %g c ", tip.x, tip.y + r,
                             tip.x + m, tip.y + r, tip.x + r, tip.y + m, tip.x + r, tip.y, tip.x + r,
                             tip.y - m, tip.x + m, tip.y - r, tip.x, tip.y - r);
            fz_append_printf(ctx, buf, "%g %g %g %g %g %g c %g %g %g %g %g %g c B\n", tip.x - m, tip.y - r,
                             tip.x - r, tip.y - m, tip.x - r, tip.y, tip.x - r, tip.y + m, tip.x - m,
                             tip.y + r, tip.x, tip.y + r);
            break;
        }
        case PDF_ANNOT_LE_DIAMOND:
            fz_append_printf(ctx, buf, "%g %g m %g %g l %g %g l %g %g l h B\n", tip.x, tip.y + r, tip.x + r,
                             tip.y, tip.x, tip.y - r, tip.x - r, tip.y);
            break;
        case PDF_ANNOT_LE_BUTT:
            fz_append_printf(ctx, buf, "%g %g m %g %g l S\n", tip.x - dy * r, tip.y + dx * r, tip.x + dy * r,
                             tip.y - dx * r);
            break;
        case PDF_ANNOT_LE_SLASH: {
            const float sx = -dy * c + dx * s;
            const float sy = dx * c + dy * s;
            fz_append_printf(ctx, buf, "%g %g m %g %g l S\n", tip.x - sx * r, tip.y - sy * r, tip.x + sx * r,
                             tip.y + sy * r);
            break;
        }
        default:
            break;
    }
}

// Строка в коде Identity-H (GID двумя байтами, шестнадцатерично).
std::string hexOf(const std::u32string& line, edit::FontLibrary& lib, const edit::SystemFace& face) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(line.size() * 4);
    for (const char32_t c : line) {
        const int g = lib.gid(face, c);
        for (int shift = 12; shift >= 0; shift -= 4) {
            out.push_back(kHex[(g >> shift) & 0xF]);
        }
    }
    return out;
}

}  // namespace

fz_rect freeTextDesign(fz_context* ctx, pdf_obj* annot) {
    const fz_rect rect = pdf_dict_get_rect(ctx, annot, PDF_NAME(Rect));
    pdf_obj* rd = pdf_dict_get(ctx, annot, PDF_NAME(RD));
    if (pdf_array_len(ctx, rd) != 4) {
        return rect;
    }
    const fz_rect design =
        fz_make_rect(rect.x0 + pdf_array_get_real(ctx, rd, 0), rect.y0 + pdf_array_get_real(ctx, rd, 3),
                     rect.x1 - pdf_array_get_real(ctx, rd, 2), rect.y1 - pdf_array_get_real(ctx, rd, 1));
    return fz_is_empty_rect(design) ? rect : design;
}

void writeFreeText(Document& doc, PageAnnots& pa, int objnum) {
    pdf_annot* a = pa.find(objnum);
    if (a == nullptr) {
        notFound({pa.index(), objnum});
    }
    fz_context* ctx = pa.ctx();
    pdf_page* page = pa.page();
    FreeTextState s;
    mu::call(ctx, [&] { readState(ctx, a, page, s); });

    const TextFont& font = textFont(textFontFamily(s.daFont));
    std::optional<edit::SystemFace> face = faceOf(font);
    const TextFont* used = &font;
    if (!face) {
        used = &textFont(kDefaultTextFont);
        face = faceOf(*used);
    }
    edit::FontLibrary lib(ctx);
    if (!face || !lib.embeddable(*face)) {
        spdlog::warn("annot: free text font {} cannot be embedded, MuPDF appearance", font.family);
        mu::call(ctx, [&] {
            pdf_dirty_annot(ctx, a);
            pdf_update_annot(ctx, a);
        });
        return;
    }
    fz_font* fzFont = lib.font(*face);
    float ascender = 0.9f;
    float descender = -0.2f;
    mu::call(ctx, [&] {
        ascender = fz_font_ascender(ctx, fzFont);
        descender = fz_font_descender(ctx, fzFont);
    });

    const std::u32string text = utf8::decode(s.contents);
    std::set<int> needed;
    for (const char32_t c : text) {
        if (const int g = lib.gid(*face, c); g > 0) {
            needed.insert(g);
        }
    }
    const int fontNum = fieldFont(doc, lib, *face, *used, needed);

    // Рамка текста: поле минус рамка и отступ; страница повёрнута на 90/270 — ширина и высота меняются
    // местами.
    const float bw = std::max(0.0f, s.border);
    const fz_rect box = s.design;
    float w = box.x1 - box.x0;
    float h = box.y1 - box.y0;
    if (s.rotate == 90 || s.rotate == 270) {
        std::swap(w, h);
    }
    const float pad = bw + 2;
    const auto advance = [&](char32_t c) {
        const int g = lib.gid(*face, c);
        return g > 0 ? lib.advance(*face, g) * s.size : 0.0f;
    };
    const std::vector<std::u32string> lines = wrapText(text, std::max(1.0f, w - 2 * pad), advance);
    struct Placed {
        float x = 0;
        float y = 0;
        std::string hex;
    };
    std::vector<Placed> placed;
    const float lineHeight = (ascender - descender) * s.size;
    float y = h - pad - ascender * s.size;
    for (const std::u32string& line : lines) {
        float lw = 0;
        for (const char32_t c : line) {
            lw += advance(c);
        }
        float x = pad;
        if (s.quadding == 1) {
            x = (w - lw) / 2;
        } else if (s.quadding == 2) {
            x = w - pad - lw;
        }
        if (!line.empty()) {
            placed.push_back({x, y, hexOf(line, lib, *face)});
        }
        y -= lineHeight;
    }

    // Рамка аннотации: поле и линия выноски с окончанием.
    const float lineWidth = std::max(1.0f, bw);
    const float endSize = 6 + 2 * lineWidth;
    fz_rect rect = box;
    for (const fz_point& p : s.callout) {
        rect = fz_include_point_in_rect(rect, p);
    }
    if (!s.callout.empty()) {
        const float grow = endSize + lineWidth;
        rect = fz_union_rect(rect, fz_make_rect(s.callout.front().x - grow, s.callout.front().y - grow,
                                                s.callout.front().x + grow, s.callout.front().y + grow));
    }
    // /RD — по ISO 32000 (12.5.6.4, табл. 180): левый, верхний, правый, нижний отступ (MuPDF читает второй
    // как нижний — поле выноски ядро читает само, freeTextDesign).
    const std::array<float, 4> rd = {box.x0 - rect.x0, rect.y1 - box.y1, rect.x1 - box.x1, box.y0 - rect.y0};
    const bool hasRd = std::any_of(rd.begin(), rd.end(), [](float v) { return v > 0.001f; });

    const char* resource = used->resource;
    const std::string daFont = s.daFont;
    mu::BufferHandle buf(ctx);
    mu::PdfObjHandle res(ctx);
    mu::call(ctx, [&] {
        pdf_document* pdf = pa.pdf();
        pdf_obj* obj = pdf_annot_obj(ctx, a);
        if (daFont != resource) {
            pdf_set_annot_default_appearance(ctx, a, resource, s.size, s.colorN, s.color.data());
        }
        if (!s.isCallout) {
            // MuPDF 1.28.5 pdf_set_annot_rect «пересчитывает выноску» любому FreeText и пишет /CL от (0, 0) к
            // краю поля — у поля без выноски /CL не нужен (ISO 32000: только при /IT /FreeTextCallout).
            pdf_dict_del(ctx, obj, PDF_NAME(CL));
        }
        if (s.rotate != 0) {
            pdf_dict_put_int(ctx, obj, PDF_NAME(Rotate), s.rotate);
        } else {
            pdf_dict_del(ctx, obj, PDF_NAME(Rotate));
        }
        pdf_dict_put_rect(ctx, obj, PDF_NAME(Rect), rect);
        if (hasRd) {
            pdf_obj* arr = pdf_dict_put_array(ctx, obj, PDF_NAME(RD), 4);
            for (const float v : rd) {
                pdf_array_push_real(ctx, arr, v);
            }
        } else {
            pdf_dict_del(ctx, obj, PDF_NAME(RD));
        }

        res.reset(pdf_new_dict(ctx, pdf, 2));
        pdf_obj* fonts = pdf_dict_put_dict(ctx, res, PDF_NAME(Font), 1);
        pdf_dict_puts_drop(ctx, fonts, resource, pdf_new_indirect(ctx, pdf, fontNum, 0));
        buf.reset(fz_new_buffer(ctx, 512));
        fz_buffer* b = buf;
        if (s.opacity < 1) {
            pdf_obj* states = pdf_dict_put_dict(ctx, res, PDF_NAME(ExtGState), 1);
            pdf_obj* gs = pdf_new_dict(ctx, pdf, 3);
            pdf_dict_puts_drop(ctx, states, "GS0", gs);
            pdf_dict_put_name(ctx, gs, PDF_NAME(Type), "ExtGState");
            pdf_dict_put_real(ctx, gs, PDF_NAME(CA), s.opacity);
            pdf_dict_put_real(ctx, gs, PDF_NAME(ca), s.opacity);
            fz_append_string(ctx, b, "/GS0 gs\n");
        }
        if (s.fillN > 0) {
            appendColor(ctx, b, s.fillN, s.fill, false);
            fz_append_printf(ctx, b, "%g %g %g %g re\nf\n", box.x0, box.y0, box.x1 - box.x0, box.y1 - box.y0);
        }
        appendColor(ctx, b, s.colorN, s.color, true);
        if (bw > 0) {
            fz_append_printf(ctx, b, "%g w\n", bw);
            if (!s.dash.empty()) {
                fz_append_string(ctx, b, "[");
                for (const float d : s.dash) {
                    fz_append_printf(ctx, b, " %g", d);
                }
                fz_append_string(ctx, b, " ] 0 d\n");
            }
            fz_append_printf(ctx, b, "%g %g %g %g re\nS\n", box.x0 + bw / 2, box.y0 + bw / 2,
                             box.x1 - box.x0 - bw, box.y1 - box.y0 - bw);
            if (!s.dash.empty()) {
                fz_append_string(ctx, b, "[] 0 d\n");
            }
        }
        if (s.callout.size() >= 2) {
            appendColor(ctx, b, s.colorN, s.color, false);
            fz_append_printf(ctx, b, "%g w\n1 j\n%g %g m\n", lineWidth, s.callout[0].x, s.callout[0].y);
            for (std::size_t i = 1; i < s.callout.size(); ++i) {
                fz_append_printf(ctx, b, "%g %g l\n", s.callout[i].x, s.callout[i].y);
            }
            fz_append_string(ctx, b, "S\n");
            appendEnding(ctx, b, s.ending, s.callout[0], s.callout[1], endSize);
        }
        // Текст — в рамке поля; страница повёрнута — матрица как у синтеза MuPDF (/Rotate поля).
        fz_append_printf(ctx, b, "q\n%g %g %g %g re\nW\nn\n", box.x0 + bw, box.y0 + bw,
                         std::max(0.0f, box.x1 - box.x0 - 2 * bw), std::max(0.0f, box.y1 - box.y0 - 2 * bw));
        fz_matrix tfm = fz_rotate(static_cast<float>(s.rotate));
        if (s.rotate == 270) {
            tfm.f += w;
        } else if (s.rotate == 90) {
            tfm.e += h;
        } else if (s.rotate == 180) {
            tfm.e += w;
            tfm.f += h;
        }
        tfm.e += box.x0;
        tfm.f += box.y0;
        fz_append_printf(ctx, b, "%g %g %g %g %g %g cm\nBT\n/%s %g Tf\n", tfm.a, tfm.b, tfm.c, tfm.d, tfm.e,
                         tfm.f, resource, s.size);
        appendColor(ctx, b, s.colorN, s.color, false);
        for (const Placed& p : placed) {
            fz_append_printf(ctx, b, "1 0 0 1 %g %g Tm\n<%s> Tj\n", p.x, p.y, p.hex.c_str());
        }
        fz_append_string(ctx, b, "ET\nQ\n");
        pdf_set_annot_appearance(ctx, a, "N", nullptr, fz_identity, rect, res, buf);
    });
}

void updateAppearance(Document& doc, PageAnnots& pa, int objnum, bool dirty) {
    pdf_annot* a = pa.find(objnum);
    if (a == nullptr) {
        return;
    }
    fz_context* ctx = pa.ctx();
    bool freeText = false;
    mu::call(ctx, [&] {
        freeText = pdf_annot_type(ctx, a) == PDF_ANNOT_FREE_TEXT;
        if (!freeText) {
            if (dirty) {
                pdf_dirty_annot(ctx, a);
            }
            pdf_update_annot(ctx, a);
        }
    });
    if (freeText && dirty) {
        writeFreeText(doc, pa, objnum);  // без dirty — прежний вид (вставка со сдвигом)
    }
}

}  // namespace detail

namespace {

std::mutex& metricsMutex() {
    static std::mutex m;
    return m;
}

}  // namespace

FontMetrics fontMetrics(std::string_view family) {
    const TextFont& font = textFont(family);
    static std::map<std::string, FontMetrics> cache;
    const std::lock_guard lock(metricsMutex());
    if (const auto it = cache.find(font.family); it != cache.end()) {
        return it->second;
    }
    FontMetrics m;
    if (const std::optional<edit::SystemFace> face = detail::faceOf(font)) {
        fz_context* ctx = MuContext::forThisThread().get();
        edit::FontLibrary lib(ctx);
        if (fz_font* f = lib.font(*face)) {
            mu::call(ctx, [&] {
                m.ascender = fz_font_ascender(ctx, f);
                m.descender = fz_font_descender(ctx, f);
            });
        }
    }
    cache.emplace(font.family, m);
    return m;
}

GlyphMetrics glyphMetrics(std::string_view family, char32_t c) {
    const TextFont& font = textFont(family);
    static std::map<std::pair<std::string, char32_t>, GlyphMetrics> cache;
    const std::lock_guard lock(metricsMutex());
    const auto key = std::make_pair(std::string(font.family), c);
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    GlyphMetrics g;
    if (const std::optional<edit::SystemFace> face = detail::faceOf(font)) {
        fz_context* ctx = MuContext::forThisThread().get();
        edit::FontLibrary lib(ctx);
        fz_font* f = lib.font(*face);
        const int gid = lib.gid(*face, c);
        if (f != nullptr && gid > 0) {
            g.found = true;
            g.advance = lib.advance(*face, gid);
            fz_rect box{};
            mu::call(ctx, [&] { box = fz_bound_glyph(ctx, f, gid, fz_identity); });
            g.x0 = box.x0;
            g.y0 = box.y0;
            g.x1 = box.x1;
            g.y1 = box.y1;
        }
    }
    cache.emplace(key, g);
    return g;
}

}  // namespace ot::annot
