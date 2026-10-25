// Текстовое поле и выноска (план 5.2, п. 1; эталон 3.4 «Ядро»: «/DA для FreeText с кириллицей через
// встроенный TrueType», «Acrobat»: FreeText с нашим /DA и встроенным шрифтом — без «шрифт не найден»;
// comment-04, comment-05, comment-09). Внешний вид FreeText пишет ядро, а не синтез MuPDF (тот берёт base14
// Helvetica и для кириллицы — простой шрифт «HelvCYR» без описателя, который не принимает строгий pdfcpu):
//   * шрифт /DA — одно из четырёх семейств списка «Шрифт» (Segoe UI, Arial, Times New Roman, Courier New) —
//   файл
//     %WINDIR%\Fonts, встроенный Type0 / Identity-H с /FontFile2 и /ToUnicode (core/edit/font_embed.h),
//     урезанный до ASCII, Latin-1, русского алфавита и знаков (плюс знаки текста вне них); лежит в /AcroForm
//     /DR /Font под именем /DA (Acrobat находит шрифт поля при правке) и в ресурсах вида; поля одного
//     семейства делят его, пока хватает глифов;
//   * вид: заливка — /C (как у синтеза MuPDF и Acrobat), рамка /BS (ширина, пунктир) и линия выноски — цветом
//     текста /DA, непрозрачность /CA, перенос строк по ширине поля (wrapText), выравнивание /Q, текст обрезан
//     рамкой поля; страница с /Rotate — /Rotate поля (текст читается на повёрнутой странице, как у Acrobat);
//   * выноска — /IT /FreeTextCallout, /CL (конец стрелки, колено, точка у поля), /LE — имя окончания у конца
//     стрелки; /Rect охватывает поле и линию, /RD — отступы поля внутри /Rect («рамка дизайна» — поле).
// Свойства AnnotProps FreeText: fontName — семейство (readProps возвращает семейство по имени /DA: Helv →
// Arial), fontSize, textColor, align, color (заливка), borderWidth, dash, opacity, intent, callout,
// lineStartStyle.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct fz_context;

namespace ot {
class Document;
}

namespace ot::annot {

// Шрифт текстового поля: семейство (список «Шрифт» ui), имя ресурса в /DA и /DR /Font, файл в %WINDIR%\Fonts
// и PostScript-имя грани.
struct TextFont {
    const char* family;
    const char* resource;
    const char* file;
    const char* postscript;
};

inline constexpr TextFont kTextFonts[] = {
    {"Segoe UI", "SegoeUI", "segoeui.ttf", "SegoeUI"},
    {"Arial", "Arial", "arial.ttf", "ArialMT"},
    {"Times New Roman", "TimesNewRoman", "times.ttf", "TimesNewRomanPSMT"},
    {"Courier New", "CourierNew", "cour.ttf", "CourierNewPSMT"},
};
inline constexpr const char* kDefaultTextFont = "Arial";
// Шрифт отметок Fill & Sign (5.3; эталон 3.5: ✓ ✕ ● — ▭ — FreeText с символом из Segoe UI Symbol как глиф, не
// картинка): в список «Шрифт» не входит; урезается только до знаков текста.
inline constexpr TextFont kSymbolFont = {"Segoe UI Symbol", "SegoeUISymbol", "seguisym.ttf", "SegoeUISymbol"};

// Шрифт по семейству или имени ресурса (без учёта регистра; и шрифт отметок); незнакомое — Arial.
const TextFont& textFont(std::string_view name);
// Семейство по имени шрифта /DA: наши ресурсы и семейства, Helv/Helvetica — Arial, TiRo/Times — Times New
// Roman, Cour/Courier — Courier New; незнакомое — Arial.
std::string textFontFamily(std::string_view name);

// Метрики шрифта поля в долях кегля (fz_font_ascender/descender — как у вида FreeText): по ним Fill & Sign
// ставит базовую линию первой строки на линию бланка. Файла нет — Arial-подобные 0,905 / −0,212.
struct FontMetrics {
    float ascender = 0.905f;
    float descender = -0.212f;
};
FontMetrics fontMetrics(std::string_view family);
// Глиф символа c шрифта family в долях кегля: ширина и рамка (y вверх от базовой линии); нет глифа — found
// false.
struct GlyphMetrics {
    bool found = false;
    float advance = 0;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
GlyphMetrics glyphMetrics(std::string_view family, char32_t c);

// Перенос строк: абзацы по \n (\r\n, \r), внутри — по словам жадно в ширину width (advance — ширина символа в
// тех же единицах); слово шире строки режется по символам; пробелы в конце строки отбрасываются. Пустой текст
// — одна пустая строка.
std::vector<std::u32string> wrapText(std::u32string_view text, float width,
                                     const std::function<float(char32_t)>& advance);

// Встроенный шрифт текста поля формы (план 6.1; эталон 3.6 «поля без /DA для кириллицы — /DA со встроенным
// TrueType»): тот же шрифт, что у FreeText, — семейство kTextFonts из %WINDIR%\Fonts, Type0 Identity-H с
// /ToUnicode в /AcroForm /DR /Font под именем ресурса (поля одного семейства делят его, пока хватает глифов).
// Поток — поток документа (контекст ctx).
class FieldFont {
public:
    // Шрифт семейства family (имя ресурса или семейства; незнакомое — Arial); nullptr — файла нет или
    // встраивание запрещено.
    static std::unique_ptr<FieldFont> open(fz_context* ctx, std::string_view family);
    ~FieldFont();
    FieldFont(const FieldFont&) = delete;
    FieldFont& operator=(const FieldFont&) = delete;

    const TextFont& font() const noexcept;
    // Все знаки text (кроме переводов строк) есть в шрифте.
    bool covers(std::u32string_view text) const;
    float advance(char32_t c) const;  // доли кегля; глифа нет — 0
    float ascender() const noexcept;
    float descender() const noexcept;
    // Шрифт в /AcroForm /DR /Font с глифами text: номер объекта словаря. Внутри операции вызывающего, вне
    // mu::call.
    int install(Document& doc, std::u32string_view text);
    // Строка в коде Identity-H (GID двумя байтами, шестнадцатерично).
    std::string hex(std::u32string_view text) const;

private:
    struct Impl;
    explicit FieldFont(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace ot::annot
