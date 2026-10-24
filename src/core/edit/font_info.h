// Инспекция шрифтов PDF для редактора текста (план 3.2, п. 1; эталон 3.9 «FontResolver»): что за шрифт у
// пробега (тип, подмножество, кодировка, ToUnicode, программа шрифта, метрики /FontDescriptor, /Flags,
// /ItalicAngle, /StemV, вес и начертание) и есть ли в нём глиф для символа U+xxxx — то есть код в кодировке
// шрифта, который MuPDF покажет этим Unicode, и непустой контур глифа (подмножества Word и наших генераторов
// оставляют слоты неиспользованных глифов пустыми). Обратная карта Unicode → код строится по кодировке шрифта
// так же, как GlyphMap читает коды (core/edit/glyph_map.h): правило 1 FontResolver — «шрифт пробега содержит
// глиф».
//
// FontProbe живёт в потоке документа (держит pdf_font_desc); FontDetails — значения без MuPDF.
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/edit/glyph_map.h"

namespace ot {
class Document;
}

namespace ot::edit {

// Программа шрифта в файле: /FontFile (Type1), /FontFile2 (TrueType), /FontFile3 (/Subtype Type1C,
// CIDFontType0C, OpenType).
enum class FontFile : std::uint8_t { None, Type1, TrueType, Cff, CidCff, OpenType };
// "" (нет), "Type1", "TrueType", "CFF", "CIDFontType0C", "OpenType".
const char* fontFileCode(FontFile file) noexcept;

// Имя шрифта PDF, разобранное на семейство и стиль: «ABCDEF+Arial-BoldMT», «Arial,BoldItalic» (Word без
// встраивания), «TimesNewRomanPS-ItalicMT», «Times-Roman». Семейство — без «MT»/«PS», CamelCase — словами
// («Times New Roman», «Segoe UI»); суффикс после «-» или «,» — стиль, только если он весь из слов стиля
// («MS-Gothic» — семейство «MS Gothic»).
struct FontName {
    std::string postscript;  // без префикса подмножества
    std::string family;
    int weight = 0;  // по словам стиля (Light — 300, Regular — 400, Bold — 700…); 0 — имя о весе молчит
    bool italic = false;
};
FontName parseFontName(std::string_view baseFont);
std::string stripSubsetTag(std::string_view name);
bool hasSubsetTag(std::string_view name);
// Вес по /StemV: 0 — не задан; < 110 — 400, < 180 — 700, дальше — 900 (Helvetica 88 и Helvetica-Bold 140,
// Times-Roman 84 и Times-Bold 139, Word пишет 44 у обычного).
int weightFromStemV(float stemV) noexcept;
// Семейство с засечками по имени (Times, Georgia, Cambria, Garamond… и «Serif» в имени).
bool isSerifFamily(std::string_view family) noexcept;
// Моноширинное семейство по имени (Courier, Consolas, Lucida Console… и «Mono» в имени).
bool isMonospaceFamily(std::string_view family) noexcept;

struct FontDetails {
    std::string resource;    // имя в /Resources/Font ("F1")
    int objnum = 0;          // словарь шрифта (0 — прямой объект)
    std::string baseFont;    // /BaseFont как в файле (с префиксом подмножества)
    std::string name;        // без префикса
    std::string family;      // /FontFamily дескриптора или из имени
    std::string subtype;     // Type0, TrueType, Type1, MMType1, Type3
    std::string cidSubtype;  // у Type0: CIDFontType2, CIDFontType0
    std::string encoding;  // "Identity-H", "WinAnsiEncoding", "WinAnsiEncoding+Differences", "+Differences",
                           // "" (встроенная кодировка шрифта), у CMap-потока — его /CMapName
    FontFile file = FontFile::None;
    bool embedded = false;
    bool subset = false;
    bool base14 = false;
    bool toUnicode = false;
    bool differences = false;
    bool twoByte = false;
    bool vertical = false;
    bool type3 = false;
    bool substitute = false;  // ресурса нет или не загрузился — запасной шрифт MuPDF
    // /FontDescriptor (у Type0 — потомка); нет дескриптора — нули.
    int flags = 0;
    float italicAngle = 0;
    float stemV = 0;
    int fontWeight = 0;                                         // /FontWeight (PDF 1.5), 0 — нет
    float ascent = 0, descent = 0, capHeight = 0, xHeight = 0;  // доли кегля (как fz_font_ascender у ascent)
    float missingWidth = 0;                                     // доли кегля
    int glyphCount = 0;                                         // глифов в программе шрифта
    // Итог стиля: /FontWeight → слова имени → ForceBold → /StemV → программа шрифта; курсив — наклон, флаг
    // Italic, имя или программа; засечки — флаг Serif или семейство; моноширинный — флаг или семейство.
    int weight = 400;
    bool bold = false;  // weight ≥ 600
    bool italic = false;
    bool serif = false;
    bool fixedPitch = false;
    bool symbolic = false;  // флаг Symbolic без Nonsymbolic
};

// Символ в кодировке шрифта.
struct EncodedChar {
    std::string bytes;       // код строки (старший байт первым)
    std::uint32_t code = 0;  // тот же код числом
    int cid = 0;
    int gid = 0;
    float w0 = 0;  // ширина в долях кегля (как GlyphMap::Code::w0)
};

class FontProbe {
public:
    // Шрифт словаря fontDict ресурсов resources (как GlyphMap::load: ошибка — запасной MuPDF, substitute).
    static std::unique_ptr<FontProbe> load(fz_context* ctx, pdf_document* doc, pdf_obj* resources,
                                           pdf_obj* fontDict, std::string resource = {});
    // Шрифт ресурса resource в /Resources/Font страницы page; нет такого — nullptr.
    static std::unique_ptr<FontProbe> forPage(const Document& doc, int page, const std::string& resource);
    ~FontProbe();
    FontProbe(const FontProbe&) = delete;
    FontProbe& operator=(const FontProbe&) = delete;

    const FontDetails& details() const noexcept { return details_; }
    GlyphMap& glyphs() noexcept { return *glyphs_; }

    // Код символа u в кодировке шрифта, если MuPDF покажет по нему глиф с этим Unicode (первый по возрастанию
    // кода); nullopt — правило 1 не подходит. Type3 — всегда nullopt (такой текст только для чтения).
    std::optional<EncodedChar> encode(char32_t u);
    bool hasGlyph(char32_t u) { return encode(u).has_value(); }
    // Все символы с кодом и глифом, по возрастанию.
    std::vector<char32_t> chars();

private:
    FontProbe() = default;
    void buildReverse();
    bool glyphPresent(int gid, char32_t u) const;

    std::unique_ptr<GlyphMap> glyphs_;
    FontDetails details_;
    bool reverseBuilt_ = false;
    std::unordered_map<char32_t, EncodedChar> reverse_;
};

// Шрифты /Resources/Font страницы (без форм) по имени ресурса.
std::vector<FontDetails> inspectPageFonts(const Document& doc, int page);

}  // namespace ot::edit
