#include "core/edit/font_info.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <utility>

#include <mupdf/ucdn.h>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"

namespace ot::edit {

namespace font_info_detail {

bool isUpper(char c) noexcept { return c >= 'A' && c <= 'Z'; }
bool isLower(char c) noexcept { return c >= 'a' && c <= 'z'; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool containsWord(std::string_view hay, std::string_view needle) {
    return lower(hay).find(lower(needle)) != std::string::npos;
}

// «TimesNewRomanPSMT» → «TimesNewRoman»: хвосты Monotype «MT» и «PS», если перед ними строчная буква.
std::string stripVendorSuffix(std::string s) {
    for (bool changed = true; changed;) {
        changed = false;
        for (std::string_view suffix : {std::string_view("MT"), std::string_view("PS")}) {
            if (s.size() > suffix.size() + 1 &&
                std::string_view(s).substr(s.size() - suffix.size()) == suffix) {
                const char prev = s[s.size() - suffix.size() - 1];
                if (isLower(prev) || (suffix == "MT" && prev == 'S')) {
                    s.resize(s.size() - suffix.size());
                    changed = true;
                }
            }
        }
    }
    return s;
}

// Слова CamelCase: «BoldItalic» → Bold, Italic; «SegoeUI» → Segoe, UI; «ABCFont» → ABC, Font.
std::vector<std::string> camelWords(std::string_view s) {
    std::vector<std::string> words;
    std::string cur;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == ' ' || c == '_' || c == '-' || c == ',') {
            if (!cur.empty()) {
                words.push_back(std::move(cur));
                cur.clear();
            }
            continue;
        }
        if (!cur.empty() && isUpper(c)) {
            const char prev = cur.back();
            const bool nextLower = i + 1 < s.size() && isLower(s[i + 1]);
            if (isLower(prev) || (isUpper(prev) && nextLower)) {
                words.push_back(std::move(cur));
                cur.clear();
            }
        }
        cur.push_back(c);
    }
    if (!cur.empty()) {
        words.push_back(std::move(cur));
    }
    return words;
}

std::string joinWords(const std::vector<std::string>& words) {
    std::string out;
    for (const std::string& w : words) {
        if (!out.empty()) {
            out.push_back(' ');
        }
        out += w;
    }
    return out;
}

struct Style {
    bool ok = false;  // все слова — слова стиля
    int weight = 0;
    bool italic = false;
};

// Стиль по словам суффикса имени («BoldItalic», «Semibold», «ExtraLight», «Roman», «Oblique», «Narrow»…).
Style parseStyle(std::string_view suffix) {
    Style st;
    const std::vector<std::string> words = camelWords(suffix);
    if (words.empty()) {
        return st;
    }
    int modifier = 0;  // 1 — Semi/Demi, 2 — Extra/Ultra
    for (const std::string& raw : words) {
        const std::string w = lower(raw);
        if (w == "semi" || w == "demi") {
            modifier = 1;
            st.weight = 600;  // «Demi» без продолжения — полужирный
            continue;
        }
        if (w == "extra" || w == "ultra") {
            modifier = 2;
            continue;
        }
        if (w == "bold") {
            st.weight = modifier == 1 ? 600 : modifier == 2 ? 800 : 700;
        } else if (w == "light") {
            st.weight = modifier == 1 ? 350 : modifier == 2 ? 200 : 300;
        } else if (w == "semibold" || w == "demibold") {
            st.weight = 600;
        } else if (w == "extrabold" || w == "ultrabold") {
            st.weight = 800;
        } else if (w == "semilight") {
            st.weight = 350;
        } else if (w == "extralight" || w == "ultralight") {
            st.weight = 200;
        } else if (w == "thin" || w == "hairline") {
            st.weight = 100;
        } else if (w == "medium") {
            st.weight = 500;
        } else if (w == "black" || w == "heavy") {
            st.weight = 900;
        } else if (w == "regular" || w == "roman" || w == "book" || w == "normal" || w == "plain") {
            st.weight = st.weight == 0 ? 400 : st.weight;
        } else if (w == "italic" || w == "oblique" || w == "it" || w == "ital" || w == "slanted" ||
                   w == "inclined" || w == "boldit") {
            st.italic = true;
            if (w == "boldit") {
                st.weight = 700;
            }
        } else if (w == "narrow" || w == "condensed" || w == "cond" || w == "wide" || w == "expanded" ||
                   w == "extended" || w == "mt" || w == "ps") {
            // ширина и хвосты производителя на вес не влияют
        } else {
            return Style{};
        }
        modifier = 0;
    }
    st.ok = true;
    return st;
}

constexpr std::array<std::string_view, 5> kBase14Families = {"Courier", "Helvetica", "Times", "Symbol",
                                                             "ZapfDingbats"};

std::string familyWords(const std::string& part) {
    const std::string stripped = stripVendorSuffix(part);
    for (std::string_view b : kBase14Families) {
        if (stripped == b) {
            return stripped;
        }
    }
    if (stripped.find(' ') != std::string::npos) {
        return stripped;
    }
    return joinWords(camelWords(stripped));
}

bool isWhitespace(char32_t u) {
    const int cat = ucdn_get_general_category(static_cast<std::uint32_t>(u));
    return cat == UCDN_GENERAL_CATEGORY_ZS || cat == UCDN_GENERAL_CATEGORY_ZL ||
           cat == UCDN_GENERAL_CATEGORY_ZP || cat == UCDN_GENERAL_CATEGORY_CC ||
           cat == UCDN_GENERAL_CATEGORY_CF;
}

std::string codeBytes(std::uint32_t code, int n) {
    std::string out(static_cast<size_t>(n), '\0');
    for (int i = n - 1; i >= 0; --i) {
        out[static_cast<size_t>(i)] = static_cast<char>(code & 0xFF);
        code >>= 8;
    }
    return out;
}

FontFile fileKind(fz_context* ctx, pdf_obj* descriptor) {
    if (pdf_dict_get(ctx, descriptor, PDF_NAME(FontFile)) != nullptr) {
        return FontFile::Type1;
    }
    if (pdf_dict_get(ctx, descriptor, PDF_NAME(FontFile2)) != nullptr) {
        return FontFile::TrueType;
    }
    pdf_obj* f3 = pdf_dict_get(ctx, descriptor, PDF_NAME(FontFile3));
    if (f3 == nullptr) {
        return FontFile::None;
    }
    pdf_obj* sub = pdf_dict_get(ctx, f3, PDF_NAME(Subtype));
    if (pdf_name_eq(ctx, sub, PDF_NAME(CIDFontType0C))) {
        return FontFile::CidCff;
    }
    if (pdf_name_eq(ctx, sub, PDF_NAME(OpenType))) {
        return FontFile::OpenType;
    }
    return FontFile::Cff;
}

std::string encodingName(fz_context* ctx, pdf_obj* enc, bool& differences) {
    differences = false;
    if (enc == nullptr) {
        return {};
    }
    if (pdf_is_name(ctx, enc)) {
        return pdf_to_name(ctx, enc);
    }
    if (pdf_is_stream(ctx, enc)) {
        const char* name = pdf_to_name(ctx, pdf_dict_gets(ctx, enc, "CMapName"));
        return *name != '\0' ? name : "stream";
    }
    if (pdf_is_dict(ctx, enc)) {
        std::string out;
        if (const char* base = pdf_dict_get_name(ctx, enc, PDF_NAME(BaseEncoding)); base != nullptr) {
            out = base;
        }
        if (pdf_dict_get(ctx, enc, PDF_NAME(Differences)) != nullptr) {
            differences = true;
            out += "+Differences";
        }
        return out;
    }
    return {};
}

}  // namespace font_info_detail

namespace fid = font_info_detail;

const char* fontFileCode(FontFile file) noexcept {
    switch (file) {
        case FontFile::None:
            return "";
        case FontFile::Type1:
            return "Type1";
        case FontFile::TrueType:
            return "TrueType";
        case FontFile::Cff:
            return "CFF";
        case FontFile::CidCff:
            return "CIDFontType0C";
        case FontFile::OpenType:
            return "OpenType";
    }
    return "?";
}

bool hasSubsetTag(std::string_view name) {
    if (name.size() < 8 || name[6] != '+') {
        return false;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (!fid::isUpper(name[i])) {
            return false;
        }
    }
    return true;
}

std::string stripSubsetTag(std::string_view name) {
    return std::string(hasSubsetTag(name) ? name.substr(7) : name);
}

FontName parseFontName(std::string_view baseFont) {
    FontName n;
    n.postscript = stripSubsetTag(baseFont);
    const std::string& ps = n.postscript;
    std::string familyPart = ps;
    fid::Style style;
    if (const size_t comma = ps.find(','); comma != std::string::npos) {
        // Word без встраивания: «Arial,BoldItalic».
        familyPart = ps.substr(0, comma);
        style = fid::parseStyle(ps.substr(comma + 1));
    } else if (const size_t dash = ps.rfind('-'); dash != std::string::npos && dash > 0) {
        fid::Style st = fid::parseStyle(fid::stripVendorSuffix(ps.substr(dash + 1)));
        if (st.ok) {
            familyPart = ps.substr(0, dash);
            style = st;
        } else {
            std::replace(familyPart.begin(), familyPart.end(), '-', ' ');
        }
    }
    n.family = fid::familyWords(familyPart);
    n.weight = style.weight;
    n.italic = style.italic;
    return n;
}

int weightFromStemV(float stemV) noexcept {
    if (stemV <= 0) {
        return 0;
    }
    if (stemV < 110) {
        return 400;
    }
    if (stemV < 180) {
        return 700;
    }
    return 900;
}

bool isSerifFamily(std::string_view family) noexcept {
    static constexpr std::array<std::string_view, 22> kSerif = {
        "times",    "georgia",  "cambria",    "garamond",        "book antiqua", "palatino",
        "century",  "bookman",  "constantia", "baskerville",     "minion",       "serif",
        "roman",    "cochin",   "didot",      "bodoni",          "caslon",       "sylfaen",
        "perpetua", "rockwell", "courier",    "liberation serif"};
    const std::string f = fid::lower(family);
    if (f.find("sans") != std::string::npos) {
        return false;
    }
    for (std::string_view s : kSerif) {
        if (f.find(s) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool isMonospaceFamily(std::string_view family) noexcept {
    static constexpr std::array<std::string_view, 7> kMono = {
        "courier", "consolas", "mono", "lucida console", "lucida sans typewriter", "letter gothic", "ocr"};
    const std::string f = fid::lower(family);
    for (std::string_view s : kMono) {
        if (f.find(s) != std::string::npos) {
            return true;
        }
    }
    return false;
}

FontProbe::~FontProbe() = default;

std::unique_ptr<FontProbe> FontProbe::load(fz_context* ctx, pdf_document* doc, pdf_obj* resources,
                                           pdf_obj* fontDict, std::string resource) {
    std::unique_ptr<FontProbe> probe(new FontProbe());
    probe->glyphs_ = GlyphMap::load(ctx, doc, resources, fontDict, std::move(resource));
    const FontInfo& fi = probe->glyphs_->info();
    FontDetails& d = probe->details_;
    d.resource = fi.resource;
    d.objnum = fi.objnum;
    d.name = fi.name;
    d.subtype = fi.subtype;
    d.embedded = fi.embedded;
    d.subset = fi.subset;
    d.base14 = fi.base14;
    d.toUnicode = fi.toUnicode;
    d.twoByte = fi.twoByte;
    d.vertical = fi.vertical;
    d.type3 = fi.type3;
    d.substitute = fi.substitute;

    pdf_font_desc* fd = probe->glyphs_->desc();
    std::string fontFamily;
    if (!fi.substitute && fontDict != nullptr) {
        mu::call(ctx, [&] {
            if (const char* base = pdf_dict_get_name(ctx, fontDict, PDF_NAME(BaseFont)); base != nullptr) {
                d.baseFont = base;
            }
            pdf_obj* descriptorOwner = fontDict;
            if (pdf_name_eq(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(Subtype)), PDF_NAME(Type0))) {
                pdf_obj* descendant =
                    pdf_array_get(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(DescendantFonts)), 0);
                if (const char* sub = pdf_dict_get_name(ctx, descendant, PDF_NAME(Subtype)); sub != nullptr) {
                    d.cidSubtype = sub;
                }
                descriptorOwner = descendant;
            }
            d.encoding =
                fid::encodingName(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(Encoding)), d.differences);
            pdf_obj* desc = pdf_dict_get(ctx, descriptorOwner, PDF_NAME(FontDescriptor));
            if (pdf_is_dict(ctx, desc)) {
                d.flags = pdf_dict_get_int(ctx, desc, PDF_NAME(Flags));
                d.italicAngle = pdf_dict_get_real(ctx, desc, PDF_NAME(ItalicAngle));
                d.stemV = pdf_dict_get_real(ctx, desc, PDF_NAME(StemV));
                d.fontWeight = pdf_to_int(ctx, pdf_dict_gets(ctx, desc, "FontWeight"));
                d.file = fid::fileKind(ctx, desc);
                if (pdf_obj* fam = pdf_dict_gets(ctx, desc, "FontFamily"); pdf_is_string(ctx, fam)) {
                    fontFamily = pdf_to_text_string(ctx, fam);
                }
            }
        });
    }
    d.ascent = fd->ascent != 0 ? fd->ascent / 1000.f : fi.ascender;
    d.descent = fd->descent != 0 ? fd->descent / 1000.f : fi.descender;
    d.capHeight = fd->cap_height / 1000.f;
    d.xHeight = fd->x_height / 1000.f;
    d.missingWidth = fd->missing_width / 1000.f;
    d.glyphCount = fd->font != nullptr && !d.type3 ? fd->font->glyph_count : 0;

    const FontName parsed = parseFontName(d.baseFont.empty() ? d.name : d.baseFont);
    d.family = !fontFamily.empty() ? fontFamily : parsed.family;
    bool programBold = false;
    bool programItalic = false;
    bool programSerif = false;
    bool programMono = false;
    if (fd->font != nullptr && d.embedded && !d.type3) {
        mu::call(ctx, [&] {
            programBold = fz_font_is_bold(ctx, fd->font) != 0;
            programItalic = fz_font_is_italic(ctx, fd->font) != 0;
            programSerif = fz_font_is_serif(ctx, fd->font) != 0;
            programMono = fz_font_is_monospaced(ctx, fd->font) != 0;
        });
    }
    int weight = d.fontWeight >= 100 && d.fontWeight <= 900 ? d.fontWeight : 0;
    if (weight == 0) {
        weight = parsed.weight;
    }
    if (weight == 0 && (d.flags & PDF_FD_FORCE_BOLD) != 0) {
        weight = 700;
    }
    if (weight == 0) {
        weight = weightFromStemV(d.stemV);
    }
    if (weight == 0 && programBold) {
        weight = 700;
    }
    d.weight = weight == 0 ? 400 : weight;
    d.bold = d.weight >= 600;
    d.italic =
        parsed.italic || (d.flags & PDF_FD_ITALIC) != 0 || std::fabs(d.italicAngle) > 0.01f || programItalic;
    d.serif = (d.flags & PDF_FD_SERIF) != 0 || isSerifFamily(d.family) ||
              (d.flags == 0 && programSerif && !d.base14);
    d.fixedPitch = (d.flags & PDF_FD_FIXED_PITCH) != 0 || isMonospaceFamily(d.family) || programMono;
    d.symbolic = (d.flags & PDF_FD_SYMBOLIC) != 0 && (d.flags & PDF_FD_NONSYMBOLIC) == 0;
    return probe;
}

std::unique_ptr<FontProbe> FontProbe::forPage(const Document& doc, int page, const std::string& resource) {
    if (doc.pdf() == nullptr) {
        return nullptr;
    }
    fz_context* ctx = doc.context().get();
    mu::PageHandle p(ctx);
    pdf_obj* resources = nullptr;
    pdf_obj* fontDict = nullptr;
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        resources = pdf_page_resources(ctx, pdf_page_from_fz_page(ctx, p));
        fontDict = pdf_dict_gets(ctx, pdf_dict_get(ctx, resources, PDF_NAME(Font)), resource.c_str());
    });
    if (fontDict == nullptr) {
        return nullptr;
    }
    return load(ctx, doc.pdf(), resources, fontDict, resource);
}

bool FontProbe::glyphPresent(int gid, char32_t u) const {
    fz_font* font = glyphs_->desc()->font;
    if (font == nullptr || gid <= 0 || (font->glyph_count > 0 && gid >= font->glyph_count)) {
        return false;
    }
    if (fid::isWhitespace(u)) {
        return true;
    }
    fz_context* ctx = glyphs_->context();
    fz_rect r{};
    mu::call(ctx, [&] { r = fz_bound_glyph(ctx, font, gid, fz_identity); });
    return (r.x1 - r.x0) > 1e-4f || (r.y1 - r.y0) > 1e-4f;
}

void FontProbe::buildReverse() {
    reverseBuilt_ = true;
    if (details_.type3) {
        return;
    }
    // Диапазоны кодов — codespace кодировки (или её usecmap); без диапазонов pdf_decode_cmap читает по байту.
    const pdf_cmap* cmap = glyphs_->desc()->encoding;
    for (int depth = 0; cmap != nullptr && cmap->codespace_len == 0 && depth < 8; ++depth) {
        cmap = cmap->usecmap;
    }
    struct Range {
        int n;
        std::uint32_t low, high;
    };
    std::vector<Range> ranges;
    if (cmap == nullptr || cmap->codespace_len == 0) {
        ranges.push_back({1, 0, 255});
    } else {
        for (int i = 0; i < cmap->codespace_len; ++i) {
            ranges.push_back({cmap->codespace[i].n, cmap->codespace[i].low, cmap->codespace[i].high});
        }
    }
    constexpr std::uint32_t kPerRange = 0x10000;
    for (const Range& r : ranges) {
        const std::uint32_t last = r.high - r.low >= kPerRange ? r.low + kPerRange - 1 : r.high;
        for (std::uint32_t code = r.low;; ++code) {
            const GlyphMap::Code c = glyphs_->compute(code, r.n);
            if (c.cid >= 0 && c.unicode.size() == 1 && c.unicode[0] != 0xFFFD &&
                reverse_.count(c.unicode[0]) == 0 && glyphPresent(c.gid, c.unicode[0])) {
                reverse_.emplace(c.unicode[0],
                                 EncodedChar{fid::codeBytes(code, r.n), code, c.cid, c.gid, c.w0});
            }
            if (code == last) {
                break;
            }
        }
    }
}

std::optional<EncodedChar> FontProbe::encode(char32_t u) {
    if (!reverseBuilt_) {
        buildReverse();
    }
    if (auto it = reverse_.find(u); it != reverse_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<char32_t> FontProbe::chars() {
    if (!reverseBuilt_) {
        buildReverse();
    }
    std::vector<char32_t> out;
    out.reserve(reverse_.size());
    for (const auto& [u, e] : reverse_) {
        out.push_back(u);
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<FontDetails> inspectPageFonts(const Document& doc, int page) {
    std::vector<FontDetails> out;
    if (doc.pdf() == nullptr) {
        return out;
    }
    fz_context* ctx = doc.context().get();
    mu::PageHandle p(ctx);
    pdf_obj* resources = nullptr;
    std::vector<std::pair<std::string, pdf_obj*>> fonts;
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        resources = pdf_page_resources(ctx, pdf_page_from_fz_page(ctx, p));
        pdf_obj* dict = pdf_dict_get(ctx, resources, PDF_NAME(Font));
        const int n = pdf_dict_len(ctx, dict);
        for (int i = 0; i < n; ++i) {
            const char* key = pdf_to_name(ctx, pdf_dict_get_key(ctx, dict, i));
            fonts.emplace_back(key != nullptr ? key : "", pdf_dict_get_val(ctx, dict, i));
        }
    });
    std::sort(fonts.begin(), fonts.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [name, dict] : fonts) {
        out.push_back(FontProbe::load(ctx, doc.pdf(), resources, dict, name)->details());
    }
    return out;
}

}  // namespace ot::edit
