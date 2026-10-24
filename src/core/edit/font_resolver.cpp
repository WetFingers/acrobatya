#include "core/edit/font_resolver.h"

#include <cstdio>
#include <utility>

#include <mupdf/ucdn.h>

#include "core/edit/font_info.h"

namespace ot::edit {

namespace font_resolver_detail {

bool isSpace(char32_t u) {
    return ucdn_get_general_category(static_cast<std::uint32_t>(u)) == UCDN_GENERAL_CATEGORY_ZS;
}

// Символ есть в WinAnsi (cp1252) — латиница правила 4 простым TrueType.
bool winAnsi(char32_t u) {
    if (u < 0x20 || u > 0xFFFF) {
        return false;
    }
    const int code = fz_windows_1252_from_unicode(static_cast<int>(u));
    return code >= 0x20 && code <= 0xFF;
}

bool sameFamily(std::string_view a, std::string_view b) { return lowerAscii(a) == lowerAscii(b); }

}  // namespace font_resolver_detail

namespace frd = font_resolver_detail;

const char* fontRuleCode(FontRule rule) noexcept {
    switch (rule) {
        case FontRule::Original:
            return "original";
        case FontRule::Extend:
            return "extend";
        case FontRule::Fallback:
            return "fallback";
        case FontRule::Replace:
            return "replace";
    }
    return "?";
}

const char* fontNoticeCode(FontNoticeKind kind) noexcept {
    switch (kind) {
        case FontNoticeKind::SubsetExtended:
            return "subsetExtended";
        case FontNoticeKind::Substituted:
            return "substituted";
        case FontNoticeKind::Synthesized:
            return "synthesized";
        case FontNoticeKind::MissingGlyph:
            return "missingGlyph";
    }
    return "?";
}

bool FontChoice::sameFont(const FontChoice& o) const {
    if (newFont != o.newFont) {
        return false;
    }
    if (!newFont) {
        return rule == o.rule;
    }
    return face.sameFile(o.face) && embed == o.embed && syntheticBold == o.syntheticBold &&
           syntheticItalic == o.syntheticItalic;
}

FontResolver::FontResolver(FontLibrary& library, SystemFonts& system, FallbackFonts fallback)
    : lib_(library), system_(system), fallback_(std::move(fallback)) {}

void FontResolver::setFallback(FallbackFonts fallback) { fallback_ = std::move(fallback); }

std::optional<SystemFace> FontResolver::find(const FontDetails& d, int weight, bool italic) {
    const std::string name = d.baseFont.empty() ? d.name : d.baseFont;
    const std::string key =
        "f|" + name + "|" + d.family + "|" + std::to_string(weight) + "|" + (italic ? "i" : "n");
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }
    FontQuery q;
    q.name = name;
    q.family = d.family;
    q.weight = weight;
    q.italic = italic;
    q.metricSubstitute = true;
    std::optional<SystemFace> face = findSystemFont(system_, q);
    cache_.emplace(key, face);
    return face;
}

std::optional<SystemFace> FontResolver::family(const std::string& family, int weight, bool italic) {
    const std::string key = "s|" + family + "|" + std::to_string(weight) + "|" + (italic ? "i" : "n");
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }
    std::optional<SystemFace> face = family.empty() ? std::nullopt : system_.face(family, weight, italic);
    cache_.emplace(key, face);
    return face;
}

bool FontResolver::usable(const SystemFace& face, char32_t ch, EmbedKind kind) {
    if (kind == EmbedKind::SimpleWinAnsi && !frd::winAnsi(ch)) {
        return false;
    }
    return lib_.embeddable(face) && lib_.hasGlyph(face, ch);
}

void FontResolver::note(FontNoticeKind kind, const FontDetails& d, const std::string& replacement,
                        char32_t ch) {
    for (FontNotice& n : notices_) {
        if (n.kind == kind && n.font == d.name && n.replacement == replacement) {
            if (ch != 0 && n.chars.find(ch) == std::u32string::npos) {
                n.chars.push_back(ch);
            }
            return;
        }
    }
    FontNotice n;
    n.kind = kind;
    n.font = d.name;
    n.replacement = replacement;
    if (ch != 0) {
        n.chars.push_back(ch);
    }
    notices_.push_back(std::move(n));
}

void FontResolver::synthetic(FontChoice& c, const FontDetails& d, char32_t ch) {
    c.syntheticBold = c.face.simulatedBold;
    c.syntheticItalic = c.face.simulatedItalic;
    if (c.syntheticBold || c.syntheticItalic) {
        note(FontNoticeKind::Synthesized, d, c.face.family, ch);
    }
}

FontChoice FontResolver::fallbackFor(const FontDetails& d, char32_t ch, int weight, bool italic) {
    std::vector<std::string> families;
    if (!fallback_.family.empty()) {
        families.push_back(fallback_.family);
    }
    families.push_back(d.fixedPitch ? fallback_.mono : d.serif ? fallback_.serif : fallback_.sans);
    families.push_back(fallback_.symbol);
    FontChoice c;
    c.rule = FontRule::Fallback;
    std::optional<SystemFace> first;
    for (const std::string& fam : families) {
        const std::optional<SystemFace> face = family(fam, weight, italic);
        if (!face || !lib_.embeddable(*face)) {
            continue;
        }
        if (!first) {
            first = face;
        }
        if (lib_.hasGlyph(*face, ch)) {
            c.newFont = true;
            c.embed = EmbedKind::Cid;
            c.face = *face;
            synthetic(c, d, ch);
            note(FontNoticeKind::Substituted, d, face->family, ch);
            return c;
        }
    }
    // Глифа нет нигде: запасной шрифт покажет .notdef, человек видит «нет глифа».
    c.missing = true;
    if (first) {
        c.newFont = true;
        c.embed = EmbedKind::Cid;
        c.face = *first;
    }
    note(FontNoticeKind::MissingGlyph, d, first ? first->family : std::string(), ch);
    return c;
}

FontChoice FontResolver::resolve(FontProbe& run, char32_t ch, const StyleRequest& style) {
    const FontDetails& d = run.details();
    const int weight = style.weight > 0 ? style.weight : d.weight;
    const bool italic = style.italic >= 0 ? style.italic != 0 : d.italic;
    const bool sameStyle = (weight >= 600) == d.bold && italic == d.italic;
    const bool embedded = d.embedded && !d.substitute && !d.type3;

    FontChoice c;
    if (sameStyle && embedded && run.encode(ch).has_value()) {
        return c;  // правило 1
    }
    const std::optional<SystemFace> face = find(d, weight, italic);
    if (embedded || d.type3) {
        // Правило 2: тот же системный шрифт (или грань того же семейства для другого начертания).
        if (face && usable(*face, ch, EmbedKind::Cid)) {
            c.rule = FontRule::Extend;
            c.newFont = true;
            c.embed = EmbedKind::Cid;
            c.face = *face;
            synthetic(c, d, ch);
            if (sameStyle) {
                note(FontNoticeKind::SubsetExtended, d, face->family, ch);
            }
            return c;
        }
        return fallbackFor(d, ch, weight, italic);
    }
    // Правило 4: шрифт не встроен — метрически совместимый системный.
    if (face) {
        const EmbedKind kind = frd::winAnsi(ch) ? EmbedKind::SimpleWinAnsi : EmbedKind::Cid;
        if (usable(*face, ch, kind)) {
            c.rule = FontRule::Replace;
            c.newFont = true;
            c.embed = kind;
            c.face = *face;
            synthetic(c, d, ch);
            if (!frd::sameFamily(face->family, d.family)) {
                note(FontNoticeKind::Substituted, d, face->family, ch);
            }
            return c;
        }
    }
    return fallbackFor(d, ch, weight, italic);
}

bool FontResolver::canEncode(const FontChoice& choice, FontProbe& run, char32_t ch) {
    if (!choice.newFont) {
        return choice.rule == FontRule::Original && run.encode(ch).has_value();
    }
    return usable(choice.face, ch, choice.embed);
}

std::vector<ResolvedRun> FontResolver::runs(FontProbe& run, std::u32string_view text,
                                            const StyleRequest& style) {
    std::vector<ResolvedRun> out;
    for (char32_t ch : text) {
        FontChoice c;
        if (!out.empty() && frd::isSpace(ch) && canEncode(out.back().choice, run, ch)) {
            c = out.back().choice;
        } else {
            c = resolve(run, ch, style);
        }
        if (!out.empty() && out.back().choice.sameFont(c)) {
            out.back().text.push_back(ch);
        } else {
            out.push_back({std::move(c), std::u32string(1, ch)});
        }
    }
    return out;
}

SyntheticStyle syntheticStyle(const FontChoice& choice, float fontSize) {
    SyntheticStyle s;
    if (choice.syntheticBold) {
        s.render = 2;
        s.lineWidth = fontSize * 0.03f;
    }
    if (choice.syntheticItalic) {
        s.skew = 0.2f;
    }
    return s;
}

Matrix syntheticTextMatrix(const Matrix& tm, const SyntheticStyle& style) {
    if (style.skew == 0) {
        return tm;
    }
    return concat(Matrix{1, 0, style.skew, 1, 0, 0}, tm);
}

std::string syntheticOps(const SyntheticStyle& style) {
    if (style.render == 0) {
        return {};
    }
    char buf[48];
    std::snprintf(buf, sizeof buf, "%d Tr %.4g w", style.render, static_cast<double>(style.lineWidth));
    return buf;
}

}  // namespace ot::edit
