// GlyphMap — шрифт текстового объекта для модели контент-потока (эталон 3.9 «Модель», план 3.1): коды строки
// Tj/TJ → глифы с Unicode и шириной так же, как их показывает MuPDF (pdf-op-run.c show_string/pdf_show_char):
// pdf_decode_cmap + pdf_lookup_cmap (код → CID), pdf_lookup_cmap_full по /ToUnicode, запасная cid_to_ucs по
// именам глифов кодировки, ширина — pdf_lookup_hmtx (/Widths, /W, /DW или программа шрифта у base14) или
// pdf_lookup_vmtx у вертикального. Сведения о шрифте (FontInfo) — значения без MuPDF: их держит ContentModel.
//
// Живёт только на время построения модели в потоке документа (держит pdf_font_desc и контекст документа).
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::edit {

// Почему текст нельзя править (эталон 3.9: блоки «только чтение» с причиной). Порядок — приоритет причины.
enum class ReadOnly : std::uint8_t {
    None,
    Hidden,       // текст в выключенном слое (/OC): MuPDF его не показывает и не извлекает
    Form,         // текст XObject-формы: её поток могут делить другие страницы
    Syntax,       // ошибка разбора в текстовом объекте
    Split,        // операнды оператора показа — в предыдущем потоке массива /Contents
    Type3,        // шрифт Type3 (глифы — процедуры)
    Vertical,     // вертикальное письмо (WMode 1)
    Rotated,      // повёрнутый, наклонный или зеркальный текст
    FontMissing,  // шрифт не встроен, без /ToUnicode и не из стандартных 14 (или ресурса нет вовсе)
    NoWidths,     // простой шрифт без /Widths и не из стандартных 14
    NoUnicode,    // глиф без Unicode (U+FFFD)
    Pattern,      // заливка или обводка текста узором (/Pattern)
    Clip,         // Tr 4–7: текст добавляется к обтравке
};

// Код причины для журнала, тестов и ожиданий JSON: "" — можно править, иначе "form", "rotated", "clip"…
const char* readOnlyCode(ReadOnly reason) noexcept;

// Шрифт ресурса страницы (или формы) — значения без MuPDF.
struct FontInfo {
    std::string resource;    // имя в /Font ресурсов ("F1"); у шрифта из /ExtGState — "gs:<имя>"
    std::string name;        // /BaseFont без префикса подмножества «ABCDEF+»
    std::string subtype;     // /Subtype: Type0, TrueType, Type1, MMType1, Type3
    int objnum = 0;          // номер объекта словаря шрифта (0 — прямой объект)
    bool embedded = false;   // программа шрифта в файле (FontFile*, у Type0 — у потомка)
    bool subset = false;     // префикс подмножества
    bool toUnicode = false;  // есть /ToUnicode
    bool widths = false;     // есть /Widths (простой) или CID-шрифт (/W, /DW по умолчанию 1000)
    bool base14 = false;     // одно из 14 стандартных имён
    bool twoByte = false;    // коды строк — не однобайтовые (Type0 без однобайтовых диапазонов CMap)
    bool vertical = false;   // WMode 1
    bool type3 = false;
    bool substitute = false;  // ресурса нет или шрифт не загрузился — MuPDF подставил запасной (hail mary)
    float ascender = 0.8f;    // в долях кегля (fz_font_ascender — как рамки символов fz_stext)
    float descender = -0.2f;
    ReadOnly reason = ReadOnly::None;  // причина уровня шрифта (Type3, Vertical, FontMissing, NoWidths)
};

class GlyphMap {
public:
    // Код строки, разобранный как в MuPDF.
    struct Code {
        std::uint32_t code = 0;  // код (байты строки, старший первый)
        std::uint8_t bytes = 1;  // длина кода в байтах
        int cid = -1;            // −1 — кодировка не знает кода: глифа нет, но Tw у кода 32 действует
        int gid = 0;
        float w0 = 0;            // ширина в долях кегля (горизонтально — w/1000, вертикально — w1/1000)
        float vx = 0, vy = 0;    // вертикальное письмо: смещение начала глифа (v.x, v.y)/1000
        std::u32string unicode;  // один или несколько символов; неизвестный — U+FFFD
        bool wordSpace() const noexcept { return bytes == 1 && code == 32; }
    };

    // Загрузить шрифт словаря fontDict (pdf_load_font; ошибка или fontDict == nullptr — запасной шрифт MuPDF,
    // как pdf_try_load_font интерпретатора). resources — ресурсы, где найден шрифт (Type3 читает свои).
    static std::unique_ptr<GlyphMap> load(fz_context* ctx, pdf_document* doc, pdf_obj* resources,
                                          pdf_obj* fontDict, std::string resource);
    ~GlyphMap();
    GlyphMap(const GlyphMap&) = delete;
    GlyphMap& operator=(const GlyphMap&) = delete;

    const FontInfo& info() const noexcept { return info_; }
    bool vertical() const noexcept { return info_.vertical; }

    // Разобрать байты строки на коды (как show_string): каждый код — запись в out (добавляются в конец).
    void decode(std::string_view bytes, std::vector<Code>& out);
    // Один код (с кешем) и без кеша — для обхода всех кодов кодировки (core/edit/font_info.h).
    Code lookup(std::uint32_t code, int bytes);
    Code compute(std::uint32_t code, int bytes) const;

    // Шрифт MuPDF (живёт, пока жив GlyphMap): кодировка, ToUnicode, метрики, программа шрифта.
    pdf_font_desc* desc() const noexcept { return font_; }
    fz_context* context() const noexcept { return ctx_; }

    static bool isBase14(std::string_view name) noexcept;

private:
    GlyphMap(fz_context* ctx, pdf_font_desc* font);

    fz_context* ctx_;
    pdf_font_desc* font_;
    FontInfo info_;
    std::unordered_map<std::uint64_t, Code> cache_;  // ключ — (байты << 32) | код
};

}  // namespace ot::edit
