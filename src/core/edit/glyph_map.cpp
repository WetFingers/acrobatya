#include "core/edit/glyph_map.h"

#include <array>
#include <cstring>
#include <utility>

#include "core/mu/call.h"

namespace ot::edit {

namespace {

constexpr char32_t kReplacement = 0xFFFD;  // FZ_REPLACEMENT_CHARACTER

std::string stripSubset(const char* name) {
    std::string n = name != nullptr ? name : "";
    if (n.size() > 7 && n[6] == '+') {
        bool tag = true;
        for (int i = 0; i < 6; ++i) {
            tag = tag && n[static_cast<size_t>(i)] >= 'A' && n[static_cast<size_t>(i)] <= 'Z';
        }
        if (tag) {
            return n.substr(7);
        }
    }
    return n;
}

bool hasSubsetTag(const char* name) {
    return name != nullptr && stripSubset(name).size() != std::strlen(name);
}

// Однобайтовый диапазон кодов в CMap кодировки (или в её usecmap).
bool hasOneByteRange(const pdf_cmap* cmap) {
    for (int depth = 0; cmap != nullptr && depth < 8; ++depth, cmap = cmap->usecmap) {
        if (cmap->codespace_len == 0) {
            continue;
        }
        for (int i = 0; i < cmap->codespace_len; ++i) {
            if (cmap->codespace[i].n == 1) {
                return true;
            }
        }
        return false;
    }
    return true;  // без диапазонов pdf_decode_cmap читает по байту
}

}  // namespace

const char* readOnlyCode(ReadOnly reason) noexcept {
    switch (reason) {
        case ReadOnly::None:
            return "";
        case ReadOnly::Hidden:
            return "hidden";
        case ReadOnly::Form:
            return "form";
        case ReadOnly::Syntax:
            return "syntax";
        case ReadOnly::Split:
            return "split";
        case ReadOnly::Type3:
            return "type3";
        case ReadOnly::Vertical:
            return "vertical";
        case ReadOnly::Rotated:
            return "rotated";
        case ReadOnly::FontMissing:
            return "fontMissing";
        case ReadOnly::NoWidths:
            return "noWidths";
        case ReadOnly::NoUnicode:
            return "noUnicode";
        case ReadOnly::Pattern:
            return "pattern";
        case ReadOnly::Clip:
            return "clip";
    }
    return "?";
}

bool GlyphMap::isBase14(std::string_view name) noexcept {
    static constexpr std::array<std::string_view, 14> kNames = {
        "Courier",     "Courier-Bold",   "Courier-Oblique",   "Courier-BoldOblique",
        "Helvetica",   "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique",
        "Times-Roman", "Times-Bold",     "Times-Italic",      "Times-BoldItalic",
        "Symbol",      "ZapfDingbats"};
    for (std::string_view n : kNames) {
        if (n == name) {
            return true;
        }
    }
    return false;
}

GlyphMap::GlyphMap(fz_context* ctx, pdf_font_desc* font) : ctx_(ctx), font_(font) {}

GlyphMap::~GlyphMap() {
    if (font_ != nullptr) {
        pdf_drop_font(ctx_, font_);
    }
}

std::unique_ptr<GlyphMap> GlyphMap::load(fz_context* ctx, pdf_document* doc, pdf_obj* resources,
                                         pdf_obj* fontDict, std::string resource) {
    pdf_font_desc* font = nullptr;
    bool substitute = fontDict == nullptr;
    if (fontDict != nullptr) {
        pdf_resource_stack rdb{nullptr, resources};
        try {
            mu::call(ctx, [&] { font = pdf_load_font(ctx, doc, &rdb, fontDict); });
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System) {
                throw;
            }
            font = nullptr;
        }
        substitute = font == nullptr;
    }
    if (font == nullptr) {
        mu::call(ctx, [&] { font = pdf_load_hail_mary_font(ctx, doc); });
    }
    std::unique_ptr<GlyphMap> map(new GlyphMap(ctx, font));
    FontInfo& info = map->info_;
    info.resource = std::move(resource);
    info.substitute = substitute;

    const char* baseFont = nullptr;
    const char* subtype = nullptr;
    int objnum = 0;
    int toUnicode = 0;
    int widths = 0;
    pdf_obj* descendant = nullptr;
    float ascender = 0.8f;
    float descender = -0.2f;
    if (!substitute) {
        mu::call(ctx, [&] {
            objnum = pdf_to_num(ctx, fontDict);
            baseFont = pdf_to_name(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(BaseFont)));
            subtype = pdf_to_name(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(Subtype)));
            toUnicode = pdf_dict_get(ctx, fontDict, PDF_NAME(ToUnicode)) != nullptr ? 1 : 0;
            widths = pdf_array_len(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(Widths))) > 0 ? 1 : 0;
            descendant = pdf_array_get(ctx, pdf_dict_get(ctx, fontDict, PDF_NAME(DescendantFonts)), 0);
        });
    }
    mu::call(ctx, [&] {
        ascender = fz_font_ascender(ctx, font->font);
        descender = fz_font_descender(ctx, font->font);
    });
    info.name = stripSubset(baseFont);
    info.subset = hasSubsetTag(baseFont);
    info.subtype = subtype != nullptr ? subtype : "";
    info.objnum = objnum;
    info.toUnicode = toUnicode != 0;
    info.embedded = font->is_embedded != 0;
    info.vertical = font->wmode != 0;
    info.type3 = font->font != nullptr && font->font->t3procs != nullptr;
    info.base14 = isBase14(info.name);
    info.twoByte = !hasOneByteRange(font->encoding);
    info.widths = widths != 0 || descendant != nullptr || info.type3;
    info.ascender = ascender;
    info.descender = descender;

    if (info.type3) {
        info.reason = ReadOnly::Type3;
    } else if (info.vertical) {
        info.reason = ReadOnly::Vertical;
    } else if (info.substitute || (!info.embedded && !info.toUnicode && !info.base14)) {
        info.reason = ReadOnly::FontMissing;
    } else if (!info.widths && !info.base14) {
        info.reason = ReadOnly::NoWidths;
    }
    return map;
}

GlyphMap::Code GlyphMap::lookup(std::uint32_t code, int bytes) {
    const std::uint64_t key = (static_cast<std::uint64_t>(bytes) << 32) | code;
    if (auto it = cache_.find(key); it != cache_.end()) {
        return it->second;
    }
    Code c = compute(code, bytes);
    cache_.emplace(key, c);
    return c;
}

GlyphMap::Code GlyphMap::compute(std::uint32_t code, int bytes) const {
    Code c;
    c.code = code;
    c.bytes = static_cast<std::uint8_t>(bytes);
    c.cid = pdf_lookup_cmap(font_->encoding, code);
    if (c.cid >= 0) {
        // Как pdf_show_char: /ToUnicode, управляющие 8–13 → пробел, явно неверные значения — запасная
        // cid_to_ucs.
        int ucs[PDF_MRANGE_CAP];
        int len = 0;
        if (font_->to_unicode != nullptr) {
            len = pdf_lookup_cmap_full(font_->to_unicode, static_cast<unsigned>(c.cid), ucs);
        }
        if (len == 1 && ucs[0] >= 8 && ucs[0] <= 13) {
            ucs[0] = ' ';
        }
        if (len == 1 && (ucs[0] < 32 || (ucs[0] >= 127 && ucs[0] < 160))) {
            len = 0;
        }
        if (len == 0 && static_cast<size_t>(c.cid) < font_->cid_to_ucs_len) {
            ucs[0] = font_->cid_to_ucs[c.cid];
            len = 1;
        }
        if (len == 0 || (len == 1 && ucs[0] == 0)) {
            ucs[0] = static_cast<int>(kReplacement);
            len = 1;
        }
        for (int i = 0; i < len; ++i) {
            c.unicode.push_back(static_cast<char32_t>(ucs[i]));
        }
        c.gid = pdf_font_cid_to_gid(ctx_, font_, c.cid);
        if (font_->wmode == 0) {
            const pdf_hmtx h = pdf_lookup_hmtx(ctx_, font_, c.cid);
            c.w0 = static_cast<float>(h.w) * 0.001f;
        } else {
            const pdf_vmtx v = pdf_lookup_vmtx(ctx_, font_, c.cid);
            c.w0 = static_cast<float>(v.w) * 0.001f;
            c.vx = static_cast<float>(v.x) * 0.001f;
            c.vy = static_cast<float>(v.y) * 0.001f;
        }
    }
    return c;
}

void GlyphMap::decode(std::string_view bytes, std::vector<Code>& out) {
    // pdf_decode_cmap не меняет байты, а только читает: const_cast безопасен.
    auto* s = reinterpret_cast<unsigned char*>(const_cast<char*>(bytes.data()));
    unsigned char* const e = s + bytes.size();
    while (s < e) {
        unsigned int cpt = 0;
        const int w = pdf_decode_cmap(font_->encoding, s, e, &cpt);
        s += w;
        out.push_back(lookup(cpt, w));
    }
}

}  // namespace ot::edit
