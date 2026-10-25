// Штампы (план 5.2, п. 4; эталон 3.4 «Инструмент»: Штамп (K) — палитра категорий, из буфера (comment-41), из
// файла PNG/JPEG/PDF; «Acrobat»: штампы с именами Acrobat; 1.4 — comment-13 частично; comment-11,
// comment-12):
//   * источник штампа — страница PDF (свои штампы assets/stamps/<категория>/<имя>.pdf, пользовательские из
//     %LOCALAPPDATA%\AcroBatya\stamps\<категория>\): страница → display list → /AP /N аннотации /Stamp
//     (pdf_set_annot_appearance_from_display_list — графика и картинки страницы векторно, /BBox — размер
//     страницы, /Rect — рамка постановки, вписывание — 12.5.5 PDF); /Name — имя Acrobat («#Approved»,
//     «#Draft»…) или своё; MuPDF такой вид не пересинтезирует (нестандартное имя с /AP);
//   * динамический штамп — строки-шаблоны (StampLine) с подстановками {name} {title} {org} {date} {time} из
//     Identity (StampValues; без JavaScript): строки ложатся в тот же display list контурами глифов
//     (fz_outline_glyph, Helvetica MuPDF с запасными шрифтами для кириллицы) — шрифт в файл не встраивается;
//   * картинка (PNG/JPEG, буфер обмена — comment-41) — pdf_set_annot_stamp_image: вид MuPDF «/I Do»;
//     imageStampPdf — та же картинка одностраничным PDF (так хранятся пользовательские штампы из картинок);
//     pageStampPdf — одна страница чужого PDF отдельным файлом;
//   * поворот — /Matrix вида вокруг центра /BBox и /Rect по повёрнутой рамке (центр прежний), /Rotate —
//     угол (Acrobat), непрозрачность — /CA.
// Всё — операциями журнала (шаг «Add stamp» / «Rotate stamp»), в потоке документа.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/annot/annots.h"

namespace ot {
class Document;
}

namespace ot::annot {

// Строка динамического штампа в координатах страницы штампа (пункты, y вниз): x — по align (0 — левый край,
// 1 — центр, 2 — правый край строки), y — базовая линия.
struct StampLine {
    std::string text;  // шаблон: «{name}, {date} {time}»
    float x = 0;
    float y = 0;
    float size = 9;
    int align = 1;
    std::uint32_t color = 0x000000;  // 0xRRGGBB
};

// Значения подстановок (Identity из Установок; дата dd.MM.yyyy и время HH:mm готовит службы).
struct StampValues {
    std::string name;
    std::string title;
    std::string org;
    std::string date;
    std::string time;
};

// Подставить {name} {title} {org} {date} {time}; незнакомое — как есть. Пустое значение — пусто (и запятая с
// пробелом перед ним не остаётся: «{name}, {date}» без имени — «25.10.2026»).
std::string expandStampText(std::string_view text, const StampValues& values);

// Источник штампа: страница PDF и строки шаблона.
struct StampSource {
    std::string pdf;  // байты PDF
    int page = 0;
    std::string name;  // /Name: «#Approved», «#Draft», своё
    std::vector<StampLine> lines;
};

// Размер в пунктах.
struct StampSize {
    float width = 0;
    float height = 0;
};

// Размер страницы штампа (пункты, с /Rotate страницы); не PDF или нет страницы — MuError.
StampSize stampPageSize(const std::string& pdf, int page = 0);

// Поставить штамп: props — рамка rect (координаты страницы), непрозрачность, автор, текст, /Subj (пусто —
// «Stamp»), флаги; цвет и icon не используются. Операция «Add stamp».
AnnotRef createStamp(Document& doc, int page, const StampSource& source, const StampValues& values,
                     AnnotProps props);
// Штамп-картинка (PNG/JPEG байтами): вид MuPDF с картинкой; name — /Name (пусто — «Image»).
AnnotRef createImageStamp(Document& doc, int page, const std::string& image, AnnotProps props,
                          const std::string& name = {});
// PNG картинки штампа-картинки (5.3, подпись Fill & Sign): XObject /I вида (или первая картинка ресурсов /AP
// /N) с альфой из /SMask; не штамп или без картинки — MuError.
std::string stampImage(Document& doc, const AnnotRef& ref);
// Заменить картинку штампа-картинки (PNG/JPEG байтами; рамка прежняя, вид — заново): цвет подписи Fill &
// Sign. Операция operation.
void setStampImage(Document& doc, const AnnotRef& ref, const std::string& image,
                   const std::string& operation = "Set stamp image");
// Размер картинки в пунктах (96 dpi, как вставка картинки Acrobat; нет dpi — 96); не картинка — MuError.
StampSize imageStampSize(const std::string& image);

// Одностраничный PDF с картинкой на всю страницу (пользовательский штамп из PNG/JPEG).
std::string imageStampPdf(const std::string& image);
// Одна страница page из PDF pdf — отдельным PDF (пользовательский штамп со страницы).
std::string pageStampPdf(const std::string& pdf, int page);

// Повернуть штамп на degrees (по часовой на экране, от исходного положения): /Matrix вида, /Rect по
// повёрнутой рамке с тем же центром, /Rotate. Операция «Rotate stamp». Не штамп или нет вида — MuError.
void setStampRotation(Document& doc, const AnnotRef& ref, float degrees);
// Текущий угол штампа (/Rotate; нет — 0).
float stampRotation(Document& doc, const AnnotRef& ref);

}  // namespace ot::annot
