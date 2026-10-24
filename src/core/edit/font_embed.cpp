#include "core/edit/font_embed.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>

#include <spdlog/spdlog.h>

extern "C" {
#include <mupdf/pdf.h>
}

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/mu/call.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"

namespace ot::edit {

namespace font_embed_detail {

// Таблицы sfnt, которых нет в API MuPDF: наклон и моноширинность (post), вес, класс семейства, высоты (OS/2).
struct Sfnt {
    int unitsPerEm = 1000;
    float italicAngle = 0;
    bool fixedPitch = false;
    int weight = 0;
    int familyClass = 0;  // старший байт sFamilyClass: 1–5, 7 — с засечками, 8 — без
    int capHeight = 0;    // единицы шрифта
    int xHeight = 0;
};

std::uint32_t be32(const unsigned char* p) {
    return (std::uint32_t{p[0]} << 24) | (std::uint32_t{p[1]} << 16) | (std::uint32_t{p[2]} << 8) | p[3];
}
std::uint16_t be16(const unsigned char* p) { return static_cast<std::uint16_t>((p[0] << 8) | p[1]); }

Sfnt readSfnt(fz_context* ctx, fz_font* font) {
    Sfnt s;
    if (font == nullptr || font->buffer == nullptr) {
        return s;
    }
    unsigned char* data = nullptr;
    const std::size_t len = fz_buffer_storage(ctx, font->buffer, &data);
    std::size_t dir = 0;
    if (len >= 16 && std::memcmp(data, "ttcf", 4) == 0) {
        const std::uint32_t count = be32(data + 8);
        if (font->subfont < 0 || static_cast<std::uint32_t>(font->subfont) >= count ||
            12 + 4 * static_cast<std::size_t>(font->subfont) + 4 > len) {
            return s;
        }
        dir = be32(data + 12 + 4 * static_cast<std::size_t>(font->subfont));
    }
    if (dir + 12 > len) {
        return s;
    }
    const std::uint16_t tables = be16(data + dir + 4);
    for (std::uint16_t i = 0; i < tables; ++i) {
        const std::size_t rec = dir + 12 + 16 * static_cast<std::size_t>(i);
        if (rec + 16 > len) {
            break;
        }
        const unsigned char* tag = data + rec;
        const std::size_t off = be32(data + rec + 8);
        const std::size_t size = be32(data + rec + 12);
        if (off + size > len) {
            continue;
        }
        const unsigned char* t = data + off;
        if (std::memcmp(tag, "head", 4) == 0 && size >= 20) {
            s.unitsPerEm = std::max<int>(16, be16(t + 18));
        } else if (std::memcmp(tag, "post", 4) == 0 && size >= 16) {
            s.italicAngle = static_cast<float>(static_cast<std::int32_t>(be32(t + 4))) / 65536.f;
            s.fixedPitch = be32(t + 12) != 0;
        } else if (std::memcmp(tag, "OS/2", 4) == 0 && size >= 32) {
            s.weight = be16(t + 4);
            s.familyClass = static_cast<std::int16_t>(be16(t + 30)) >> 8;
            if (be16(t) >= 2 && size >= 90) {
                s.xHeight = static_cast<std::int16_t>(be16(t + 86));
                s.capHeight = static_cast<std::int16_t>(be16(t + 88));
            }
        }
    }
    return s;
}

// /ToUnicode: глиф → наименьший код Unicode, который на него указывает (MuPDF берёт последний).
std::map<int, char32_t> reverseCmap(fz_context* ctx, fz_font* font) {
    std::map<int, char32_t> out;
    const int glyphs = font->glyph_count;
    int gids[256] = {};
    auto block = [&](char32_t first) {
        mu::call(ctx, [&] {
            for (int i = 0; i < 256; ++i) {
                gids[i] = fz_encode_character(ctx, font, static_cast<int>(first) + i);
            }
        });
        for (int i = 0; i < 256; ++i) {
            const char32_t u = first + static_cast<char32_t>(i);
            if (u >= 0x20 && (u < 0xD800 || u > 0xDFFF) && gids[i] > 0 && (glyphs <= 0 || gids[i] < glyphs)) {
                out.emplace(gids[i], u);  // по возрастанию u: первый — наименьший
            }
        }
    };
    for (char32_t b = 0; b < 0x10000; b += 256) {
        block(b);
    }
    for (char32_t b = 0x1F000; b < 0x1FB00; b += 256) {  // символы и пиктограммы вне BMP (Segoe UI Symbol)
        block(b);
    }
    return out;
}

void appendUtf16Hex(std::string& out, char32_t u) {
    char buf[16];
    if (u < 0x10000) {
        std::snprintf(buf, sizeof buf, "%04X", static_cast<unsigned>(u));
    } else {
        const char32_t v = u - 0x10000;
        std::snprintf(buf, sizeof buf, "%04X%04X", static_cast<unsigned>(0xD800 + (v >> 10)),
                      static_cast<unsigned>(0xDC00 + (v & 0x3FF)));
    }
    out += buf;
}

std::string toUnicodeCmap(const std::map<int, char32_t>& map) {
    std::string s =
        "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n";
    auto it = map.begin();
    while (it != map.end()) {
        const std::size_t left = static_cast<std::size_t>(std::distance(it, map.end()));
        const std::size_t n = std::min<std::size_t>(100, left);
        s += std::to_string(n) + " beginbfchar\n";
        for (std::size_t i = 0; i < n; ++i, ++it) {
            char gid[16];
            std::snprintf(gid, sizeof gid, "<%04X> <", static_cast<unsigned>(it->first));
            s += gid;
            appendUtf16Hex(s, it->second);
            s += ">\n";
        }
        s += "endbfchar\n";
    }
    s += "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
    return s;
}

fz_buffer* newBuffer(fz_context* ctx, const std::string& s) {
    return fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(s.data()), s.size());
}

// Дескриптор и имена нового шрифта: MuPDF пишет /Flags Nonsymbolic, /ItalicAngle 0, /StemV 80 без /CapHeight.
// Вызывается внутри mu::call.
void writeDescriptor(fz_context* ctx, pdf_obj* fontRef, const Sfnt& s, const SystemFace& face, EmbedKind kind,
                     const char* name) {
    pdf_obj* descendant = kind == EmbedKind::Cid
                              ? pdf_array_get(ctx, pdf_dict_get(ctx, fontRef, PDF_NAME(DescendantFonts)), 0)
                              : nullptr;
    pdf_obj* desc = pdf_dict_get(ctx, descendant != nullptr ? descendant : fontRef, PDF_NAME(FontDescriptor));
    pdf_dict_put_name(ctx, fontRef, PDF_NAME(BaseFont), name);
    if (descendant != nullptr) {
        pdf_dict_put_name(ctx, descendant, PDF_NAME(BaseFont), name);
    }
    if (desc == nullptr) {
        return;
    }
    const bool italic = face.italic || std::fabs(s.italicAngle) > 0.01f;
    const bool serif = (s.familyClass >= 1 && s.familyClass <= 5) || s.familyClass == 7;
    int flags = PDF_FD_NONSYMBOLIC;
    flags |= serif ? PDF_FD_SERIF : 0;
    flags |= s.fixedPitch ? PDF_FD_FIXED_PITCH : 0;
    flags |= italic ? PDF_FD_ITALIC : 0;
    const int weight = s.weight > 0 ? s.weight : face.weight;
    const double stem = 50.0 + std::pow(weight / 65.0, 2.0);
    const float scale = 1000.f / static_cast<float>(s.unitsPerEm);
    pdf_dict_put_name(ctx, desc, PDF_NAME(FontName), name);
    pdf_dict_put_int(ctx, desc, PDF_NAME(Flags), flags);
    pdf_dict_put_real(ctx, desc, PDF_NAME(ItalicAngle), s.italicAngle);
    pdf_dict_put_int(ctx, desc, PDF_NAME(StemV), static_cast<int>(std::lround(stem)));
    const float ascent = pdf_dict_get_real(ctx, desc, PDF_NAME(Ascent));
    pdf_dict_put_int(
        ctx, desc, PDF_NAME(CapHeight),
        s.capHeight > 0 ? std::lround(static_cast<float>(s.capHeight) * scale) : std::lround(ascent * 0.7f));
    if (s.xHeight > 0) {
        pdf_dict_put_int(ctx, desc, PDF_NAME(XHeight), std::lround(static_cast<float>(s.xHeight) * scale));
    }
}

// Поток /ToUnicode шрифта заменить буфером buf (нет потока — новый). Внутри mu::call.
void putToUnicode(fz_context* ctx, pdf_document* pdf, pdf_obj* fontRef, fz_buffer* buf) {
    if (pdf_obj* tu = pdf_dict_get(ctx, fontRef, PDF_NAME(ToUnicode)); pdf_is_stream(ctx, tu)) {
        pdf_update_stream(ctx, pdf, tu, buf, 0);
    } else {
        pdf_dict_put_drop(ctx, fontRef, PDF_NAME(ToUnicode), pdf_add_stream(ctx, pdf, buf, nullptr, 0));
    }
}

// Ширины /W CID-шрифта: CID → ширина (форматы «c [w …]» и «c1 c2 w»).
std::unordered_map<int, int> readW(fz_context* ctx, pdf_obj* w) {
    std::unordered_map<int, int> out;
    const int n = pdf_array_len(ctx, w);
    for (int i = 0; i < n;) {
        const int first = pdf_array_get_int(ctx, w, i);
        pdf_obj* next = pdf_array_get(ctx, w, i + 1);
        if (pdf_is_array(ctx, next)) {
            const int m = pdf_array_len(ctx, next);
            for (int k = 0; k < m; ++k) {
                out[first + k] = pdf_array_get_int(ctx, next, k);
            }
            i += 2;
        } else {
            const int last = pdf_to_int(ctx, next);
            const int width = pdf_array_get_int(ctx, w, i + 2);
            for (int c = first; c <= last && c - first < 0x10000; ++c) {
                out[c] = width;
            }
            i += 3;
        }
    }
    return out;
}

std::string subsetTag(fz_context* ctx, fz_buffer* buf) {
    unsigned char digest[16];
    fz_md5_buffer(ctx, buf, digest);
    std::uint32_t v = 0;
    for (int i = 0; i < 16; i += 4) {
        v ^= be32(digest + i);
    }
    std::string tag(6, 'A');
    for (char& c : tag) {
        c = static_cast<char>('A' + v % 26);
        v /= 26;
    }
    return tag;
}

bool aborts(const MuError& e) { return e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System; }

}  // namespace font_embed_detail

namespace fed = font_embed_detail;

const char* embedKindCode(EmbedKind kind) noexcept { return kind == EmbedKind::Cid ? "cid" : "winansi"; }

// --- FontLibrary
// ---------------------------------------------------------------------------------------------

FontLibrary::FontLibrary(fz_context* ctx) : ctx_(ctx) {}

FontLibrary::~FontLibrary() {
    for (auto& [key, font] : fonts_) {
        if (font != nullptr) {
            fz_drop_font(ctx_, font);
        }
    }
}

fz_font* FontLibrary::font(const SystemFace& face) {
    if (face.file.empty()) {
        return nullptr;
    }
    const auto key = std::make_pair(face.file, face.index);
    if (auto it = fonts_.find(key); it != fonts_.end()) {
        return it->second;
    }
    fz_font* f = nullptr;
    mu::StreamHandle stm(ctx_);
    mu::BufferHandle buf(ctx_);
    try {
        mu::call(ctx_, [&] {
            stm.reset(fz_open_file_w(ctx_, face.file.c_str()));
            buf.reset(fz_read_all(ctx_, stm, 0));
            f = fz_new_font_from_buffer(ctx_, face.postscript.empty() ? nullptr : face.postscript.c_str(),
                                        buf, face.index, 1);
        });
    } catch (const MuError& e) {
        if (fed::aborts(e)) {
            throw;
        }
        spdlog::warn("fonts: cannot load {} #{}: {}", paths::toUtf8(face.file), face.index, e.what());
        f = nullptr;
    }
    fonts_.emplace(key, f);
    return f;
}

int FontLibrary::gid(const SystemFace& face, char32_t u) {
    fz_font* f = font(face);
    if (f == nullptr) {
        return 0;
    }
    int g = 0;
    mu::call(ctx_, [&] { g = fz_encode_character(ctx_, f, static_cast<int>(u)); });
    return g;
}

float FontLibrary::advance(const SystemFace& face, int gid) {
    fz_font* f = font(face);
    if (f == nullptr) {
        return 0;
    }
    float a = 0;
    mu::call(ctx_, [&] { a = fz_advance_glyph(ctx_, f, gid, 0); });
    return a;
}

bool FontLibrary::embeddable(const SystemFace& face) {
    fz_font* f = font(face);
    if (f == nullptr) {
        return false;
    }
    int ok = 0;
    mu::call(ctx_, [&] { ok = pdf_font_writing_supported(ctx_, f); });
    return ok != 0;
}

// --- FontEmbedder
// --------------------------------------------------------------------------------------------

FontEmbedder::FontEmbedder(Document& doc, FontLibrary& library) : doc_(doc), lib_(library) {}

bool FontEmbedder::alive(const Entry& e) const {
    if (e.done || e.objnum <= 0) {
        return false;
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = doc_.pdf();
    int exists = 0;
    mu::call(ctx, [&] { exists = pdf_object_exists(ctx, pdf, e.objnum); });
    if (exists == 0) {
        return false;  // снят undo (без ошибки MuPDF «cannot find object in xref»)
    }
    mu::PdfObjHandle ref(ctx);
    const char* base = nullptr;
    mu::call(ctx, [&] {
        ref.reset(pdf_new_indirect(ctx, pdf, e.objnum, 0));
        pdf_obj* dict = pdf_resolve_indirect(ctx, ref);
        if (pdf_is_dict(ctx, dict) &&
            pdf_name_eq(ctx, pdf_dict_get(ctx, dict, PDF_NAME(Type)), PDF_NAME(Font))) {
            base = pdf_dict_get_name(ctx, dict, PDF_NAME(BaseFont));
        }
    });
    return base != nullptr && e.baseFont == base;
}

int FontEmbedder::fontObject(const SystemFace& face, EmbedKind kind) {
    return entries_[entryFor(face, kind)].objnum;
}

std::size_t FontEmbedder::entryFor(const SystemFace& face, EmbedKind kind) {
    fz_font* font = lib_.font(face);
    if (font == nullptr) {
        throw std::runtime_error("font file is not readable: " + paths::toUtf8(face.file));
    }
    if (!lib_.embeddable(face)) {
        throw std::runtime_error("font cannot be embedded: " + paths::toUtf8(face.file));
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = doc_.pdf();
    if (pdf == nullptr) {
        throw std::logic_error("FontEmbedder: not a PDF document");
    }

    std::size_t index = entries_.size();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        const Entry& e = entries_[i];
        if (e.kind == kind && e.face.sameFile(face) && alive(e)) {
            index = i;
            break;
        }
    }
    if (index == entries_.size()) {
        Entry e;
        e.face = face;
        e.kind = kind;
        e.baseFont = !face.postscript.empty() ? face.postscript : std::string(font->name);
        const fed::Sfnt sfnt = fed::readSfnt(ctx, font);
        mu::BufferHandle toUnicode(ctx);
        if (kind == EmbedKind::Cid) {
            const std::string cmap = fed::toUnicodeCmap(fed::reverseCmap(ctx, font));
            mu::call(ctx, [&] { toUnicode.reset(fed::newBuffer(ctx, cmap)); });
        }
        Journal::Operation op(doc_, "Add font");
        mu::PdfObjHandle ref(ctx);
        const char* baseFont = e.baseFont.c_str();
        int objnum = 0;
        mu::call(ctx, [&] {
            // Кеш ресурсов MuPDF не откатывается undo и после подмножества указывает на урезанный файл.
            pdf_font_resource_key key;
            const int type = kind == EmbedKind::Cid ? PDF_CID_FONT_RESOURCE : PDF_SIMPLE_FONT_RESOURCE;
            const int encoding = kind == EmbedKind::Cid ? 0 : PDF_SIMPLE_ENCODING_LATIN;
            if (pdf_obj* cached = pdf_find_font_resource(ctx, pdf, type, encoding, font, &key);
                cached != nullptr) {
                pdf_drop_obj(ctx, cached);
                fz_hash_remove(ctx, pdf->resources.fonts, &key);
            }
            ref.reset(kind == EmbedKind::Cid
                          ? pdf_add_cid_font(ctx, pdf, font)
                          : pdf_add_simple_font(ctx, pdf, font, PDF_SIMPLE_ENCODING_LATIN));
            objnum = pdf_to_num(ctx, ref);
            fed::writeDescriptor(ctx, ref, sfnt, face, kind, baseFont);
            if (toUnicode) {
                fed::putToUnicode(ctx, pdf, ref, toUnicode);
            }
        });
        op.markHeavy();
        op.commit();
        e.objnum = objnum;
        entries_.push_back(std::move(e));
    }
    return index;
}

EmbeddedFont FontEmbedder::add(int page, const SystemFace& face, EmbedKind kind) {
    const std::size_t index = entryFor(face, kind);
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = doc_.pdf();
    const Entry& entry = entries_[index];
    EmbeddedFont out;
    out.objnum = entry.objnum;
    out.kind = entry.kind;
    out.face = entry.face;
    const auto key = std::make_pair(page, index);
    // Ресурс страницы: прежнее имя, если оно ещё указывает на этот шрифт, или любое имя этого шрифта.
    const auto cached = resources_.find(key);
    const char* previous = cached != resources_.end() ? cached->second.c_str() : nullptr;
    const char* found = nullptr;
    const int objnum = entry.objnum;
    mu::call(ctx, [&] {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
        pdf_obj* fonts =
            pdf_dict_get(ctx, pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources)), PDF_NAME(Font));
        if (previous != nullptr && pdf_to_num(ctx, pdf_dict_gets(ctx, fonts, previous)) == objnum) {
            found = previous;
        }
        const int n = pdf_dict_len(ctx, fonts);
        for (int i = 0; i < n && found == nullptr; ++i) {
            if (pdf_to_num(ctx, pdf_dict_get_val(ctx, fonts, i)) == objnum) {
                found = pdf_to_name(ctx, pdf_dict_get_key(ctx, fonts, i));
            }
        }
    });
    std::string name = found != nullptr ? std::string(found) : std::string();
    if (name.empty()) {
        char fresh[16] = {};
        Journal::Operation op(doc_, "Add font");
        mu::call(ctx, [&] {
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
            pdf_obj* res = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources));
            if (!pdf_is_dict(ctx, res)) {
                res = pdf_dict_put_dict(ctx, pageObj, PDF_NAME(Resources), 2);
            }
            pdf_obj* fonts = pdf_dict_get(ctx, res, PDF_NAME(Font));
            if (!pdf_is_dict(ctx, fonts)) {
                fonts = pdf_dict_put_dict(ctx, res, PDF_NAME(Font), 4);
            }
            for (int i = 1; fresh[0] == '\0'; ++i) {
                char cand[16];
                std::snprintf(cand, sizeof cand, "OtF%d", i);
                if (pdf_dict_gets(ctx, fonts, cand) == nullptr) {
                    std::memcpy(fresh, cand, sizeof cand);
                }
            }
            pdf_dict_puts_drop(ctx, fonts, fresh, pdf_new_indirect(ctx, pdf, objnum, 0));
        });
        op.invalidate(page);
        op.commit();
        name = fresh;
    }
    resources_[key] = name;
    out.resource = name;
    return out;
}

std::string FontEmbedder::encode(const EmbeddedFont& font, char32_t u) {
    const int gid = lib_.gid(font.face, u);
    if (gid <= 0) {
        return {};
    }
    if (font.kind == EmbedKind::Cid) {
        return {static_cast<char>((gid >> 8) & 0xFF), static_cast<char>(gid & 0xFF)};
    }
    if (u < 0x20 || u > 0xFFFF) {
        return {};
    }
    const int code = fz_windows_1252_from_unicode(static_cast<int>(u));
    if (code < 0x20 || code > 0xFF) {
        return {};
    }
    return std::string(1, static_cast<char>(code));
}

float FontEmbedder::width(const EmbeddedFont& font, char32_t u) {
    const int gid = lib_.gid(font.face, u);
    if (gid <= 0) {
        return 0;
    }
    // Как pdf_add_cid_font_widths / pdf_add_simple_font_widths: целое от fz_advance_glyph · 1000
    // (отбрасывание).
    const int w = static_cast<int>(lib_.advance(font.face, gid) * 1000.f);
    return static_cast<float>(w) / 1000.f;
}

std::vector<int> FontEmbedder::pending() const {
    std::vector<int> out;
    for (const Entry& e : entries_) {
        if (alive(e)) {
            out.push_back(e.objnum);
        }
    }
    return out;
}

SubsetReport FontEmbedder::subset() {
    const SubsetReport r = subsetFonts(doc_, pending());
    for (Entry& e : entries_) {
        if (!alive(e)) {
            e.done = true;  // урезан (имя с префиксом) или снят undo
        }
    }
    resources_.clear();
    return r;
}

// --- subsetFonts
// ---------------------------------------------------------------------------------------------

namespace font_embed_detail {

// Урезать шрифт num до глифов usedGids (0 добавляется) внутри операции вызывающего: программа, /W и
// /ToUnicode — только они, имя — с префиксом подмножества. Не TrueType или ошибка MuPDF — false (шрифт
// полный, причина в r.errors); прерывание и нехватка памяти — дальше.
bool subsetOne(Document& doc, int num, const std::set<int>& usedGids, SubsetReport& r) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    std::vector<int> gids(usedGids.begin(), usedGids.end());
    if (gids.empty() || gids.front() != 0) {
        gids.insert(gids.begin(), 0);
    }
    mu::PdfObjHandle ref(ctx);
    mu::BufferHandle original(ctx);
    mu::BufferHandle subset(ctx);
    pdf_obj* font = nullptr;
    pdf_obj* descendant = nullptr;
    pdf_obj* desc = nullptr;
    pdf_obj* file = nullptr;
    bool cid = false;
    try {
        mu::call(ctx, [&] {
            ref.reset(pdf_new_indirect(ctx, pdf, num, 0));
            font = pdf_resolve_indirect(ctx, ref);
            cid = pdf_name_eq(ctx, pdf_dict_get(ctx, font, PDF_NAME(Subtype)), PDF_NAME(Type0)) != 0;
            descendant =
                cid ? pdf_array_get(ctx, pdf_dict_get(ctx, font, PDF_NAME(DescendantFonts)), 0) : nullptr;
            desc = pdf_dict_get(ctx, cid ? descendant : font, PDF_NAME(FontDescriptor));
            file = pdf_dict_get(ctx, desc, PDF_NAME(FontFile2));
            if (file == nullptr) {
                file = pdf_dict_get(ctx, desc, PDF_NAME(FontFile3));
            }
            if (!pdf_is_stream(ctx, file)) {
                fz_throw(ctx, FZ_ERROR_FORMAT, "font %d has no TrueType/OpenType program", num);
            }
            original.reset(pdf_load_stream(ctx, file));
            subset.reset(fz_subset_ttf_for_gids(ctx, original, gids.data(), static_cast<int>(gids.size()), 0,
                                                cid ? 1 : 0));
        });
    } catch (const MuError& e) {
        if (fed::aborts(e)) {
            throw;
        }
        ++r.failed;
        r.errors.push_back("font " + std::to_string(num) + ": " + e.what());
        spdlog::warn("fonts: subsetting font {} failed, kept full: {}", num, e.what());
        return false;
    }
    // Что записать — до правки: имя, /W и /ToUnicode использованных глифов (C++ — вне mu::call).
    const char* oldName = nullptr;
    pdf_obj* oldW = nullptr;
    pdf_obj* tu = nullptr;
    pdf_cmap* cmap = nullptr;
    mu::call(ctx, [&] {
        oldName = pdf_dict_get_name(ctx, font, PDF_NAME(BaseFont));
        if (cid) {
            oldW = pdf_dict_get(ctx, descendant, PDF_NAME(W));
            tu = pdf_dict_get(ctx, font, PDF_NAME(ToUnicode));
            if (pdf_is_stream(ctx, tu)) {
                cmap = pdf_load_embedded_cmap(ctx, pdf, tu);
            }
        }
    });
    const std::string name = fed::subsetTag(ctx, subset) + "+" + (oldName != nullptr ? oldName : "Anonymous");
    std::unordered_map<int, int> widths;
    mu::BufferHandle toUnicode(ctx);
    if (cid) {
        widths = fed::readW(ctx, oldW);
        if (cmap != nullptr) {
            std::map<int, char32_t> map;
            for (int g : usedGids) {
                int out[PDF_MRANGE_CAP];
                if (pdf_lookup_cmap_full(cmap, static_cast<unsigned>(g), out) == 1 && out[0] > 0) {
                    map.emplace(g, static_cast<char32_t>(out[0]));
                }
            }
            pdf_drop_cmap(ctx, cmap);
            const std::string text = fed::toUnicodeCmap(map);
            mu::call(ctx, [&] { toUnicode.reset(fed::newBuffer(ctx, text)); });
        }
    }
    const std::size_t before = fz_buffer_storage(ctx, original, nullptr);
    const std::size_t after = fz_buffer_storage(ctx, subset, nullptr);
    const char* newName = name.c_str();
    // Программа — сжатой (инкрементальная запись потоки не сжимает, полная — сжимает, но не шрифты).
    mu::BufferHandle deflated(ctx);
    mu::call(ctx, [&] {
        std::size_t packed = 0;
        unsigned char* data = fz_new_deflated_data_from_buffer(ctx, &packed, subset, FZ_DEFLATE_DEFAULT);
        deflated.reset(fz_new_buffer_from_data(ctx, data, packed));
    });
    mu::call(ctx, [&] {
        pdf_update_stream(ctx, pdf, file, deflated, 1);
        pdf_dict_put(ctx, file, PDF_NAME(Filter), PDF_NAME(FlateDecode));
        pdf_dict_del(ctx, file, PDF_NAME(DecodeParms));
        pdf_dict_put_int(ctx, file, PDF_NAME(Length1), static_cast<std::int64_t>(after));
        pdf_dict_put_name(ctx, font, PDF_NAME(BaseFont), newName);
        if (descendant != nullptr) {
            pdf_dict_put_name(ctx, descendant, PDF_NAME(BaseFont), newName);
        }
        pdf_dict_put_name(ctx, desc, PDF_NAME(FontName), newName);
        if (cid) {
            pdf_obj* w = pdf_dict_put_array(ctx, descendant, PDF_NAME(W), 8);
            pdf_obj* run = nullptr;
            int prev = -2;
            for (int g : usedGids) {
                const auto it = widths.find(g);
                if (it == widths.end()) {
                    continue;
                }
                if (run == nullptr || g != prev + 1) {
                    pdf_array_push_int(ctx, w, g);
                    run = pdf_array_push_array(ctx, w, 4);
                }
                pdf_array_push_int(ctx, run, it->second);
                prev = g;
            }
            if (toUnicode) {
                pdf_update_stream(ctx, pdf, tu, toUnicode, 0);
            }
        }
        pdf_purge_object_from_store(ctx, pdf, num);
    });
    r.bytesBefore += before;
    r.bytesAfter += after;
    ++r.subset;
    return true;
}

}  // namespace font_embed_detail

SubsetReport subsetFonts(Document& doc, const std::vector<int>& fontObjnums) {
    SubsetReport r;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr || fontObjnums.empty()) {
        return r;
    }
    struct Use {
        std::set<int> gids;
        std::vector<std::pair<int, std::string>> resources;  // страница, имя ресурса
    };
    std::map<int, Use> uses;
    for (int num : fontObjnums) {
        uses[num];
    }
    r.fonts = static_cast<int>(uses.size());

    const int pages = doc.pageCount();
    for (int p = 0; p < pages; ++p) {
        bool referenced = false;
        mu::call(ctx, [&] {
            pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, p);
            pdf_obj* fonts = pdf_dict_get(ctx, pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources)),
                                          PDF_NAME(Font));
            const int n = pdf_dict_len(ctx, fonts);
            for (int i = 0; i < n; ++i) {
                const int num = pdf_to_num(ctx, pdf_dict_get_val(ctx, fonts, i));
                if (auto it = uses.find(num); it != uses.end()) {
                    it->second.resources.emplace_back(p, pdf_to_name(ctx, pdf_dict_get_key(ctx, fonts, i)));
                    referenced = true;
                }
            }
        });
        if (!referenced) {
            continue;
        }
        const ContentModel model = ContentModel::build(doc, p);
        for (const Glyph& g : model.glyphs()) {
            const TextSpan& span = model.spans()[static_cast<std::size_t>(g.span)];
            if (span.font < 0) {
                continue;
            }
            if (auto it = uses.find(model.fonts()[static_cast<std::size_t>(span.font)].objnum);
                it != uses.end()) {
                it->second.gids.insert(g.gid);
            }
        }
    }

    try {
        Journal::Operation op(doc, "Subset fonts");
        for (auto& [num, use] : uses) {
            if (use.gids.empty()) {
                // Шрифт не нужен ни одной странице: ресурсы сняты, объект уйдёт при полной записи.
                mu::call(ctx, [&] {
                    for (const auto& [p, name] : use.resources) {
                        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, p);
                        pdf_obj* fonts = pdf_dict_get(
                            ctx, pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources)), PDF_NAME(Font));
                        pdf_dict_dels(ctx, fonts, name.c_str());
                    }
                });
                ++r.unused;
                continue;
            }
            fed::subsetOne(doc, num, use.gids, r);
        }
        op.markHeavy();
        op.commit();
    } catch (const MuError& e) {
        if (fed::aborts(e)) {
            throw;
        }
        // Операция откатана целиком: шрифты остаются полными.
        spdlog::error("fonts: subsetting rolled back: {}", e.what());
        r.errors.push_back(e.what());
        r.failed = r.fonts;
        r.subset = 0;
        r.unused = 0;
        r.bytesAfter = r.bytesBefore;
    }
    return r;
}

bool subsetFont(Document& doc, int objnum, const std::set<int>& gids) {
    if (doc.pdf() == nullptr || objnum <= 0) {
        return false;
    }
    SubsetReport r;
    Journal::Operation op(doc, "Subset fonts");
    const bool done = fed::subsetOne(doc, objnum, gids, r);
    op.markHeavy();
    op.commit();
    return done;
}

}  // namespace ot::edit
