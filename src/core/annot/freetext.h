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
#include <string>
#include <string_view>
#include <vector>

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

// Шрифт по семейству или имени ресурса (без учёта регистра); незнакомое — Arial.
const TextFont& textFont(std::string_view name);
// Семейство по имени шрифта /DA: наши ресурсы и семейства, Helv/Helvetica — Arial, TiRo/Times — Times New
// Roman, Cour/Courier — Courier New; незнакомое — Arial.
std::string textFontFamily(std::string_view name);

// Перенос строк: абзацы по \n (\r\n, \r), внутри — по словам жадно в ширину width (advance — ширина символа в
// тех же единицах); слово шире строки режется по символам; пробелы в конце строки отбрасываются. Пустой текст
// — одна пустая строка.
std::vector<std::u32string> wrapText(std::u32string_view text, float width,
                                     const std::function<float(char32_t)>& advance);

}  // namespace ot::annot
