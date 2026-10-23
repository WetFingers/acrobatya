// Значения текстового слоя (эталон 3.2) без MuPDF: опции извлечения и то, что TextPage отдаёт наружу —
// блоки, строки, символы с рамками и стилем, векторы, шрифты, узлы структуры, слова. Координаты — пункты
// страницы MuPDF (начало в левом верхнем углу, y вниз, с учётом /Rotate и /CropBox — как fz_bound_page).
// Заголовок можно включать в services и ui (MuPDF нет); сам TextPage — core/text/stext.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/util/geometry.h"

namespace ot {

// Опции извлечения (план 1.4): по умолчанию — PRESERVE_LIGATURES | PRESERVE_WHITESPACE | ACCURATE_BBOXES |
// PARAGRAPH_BREAK | SEGMENT; флажки добавляют COLLECT_VECTORS (линии и рамки — 5.3, 5.5, 6.4), TABLE_HUNT
// (таблицы — 7.3), COLLECT_STRUCTURE (дерево тегов — 8.3, 8.4), DEHYPHENATE (перенос в конце строки —
// мягкий). readingOrder = false убирает SEGMENT и PARAGRAPH_BREAK: порядок — как в потоке страницы. Так
// строит слои поиск (SearchSession::textOptions): сегментация — почти всё время извлечения (PDF32000, 756
// страниц, Release mutool: 23,7 с с SEGMENT против 3,3 с без), а совпадениям порядок блоков не важен.
// USE_CID_FOR_UNKNOWN_UNICODE не включается никогда: символ без Unicode остаётся U+FFFD, а не CID.
struct TextOptions {
    bool collectVectors = false;
    bool tableHunt = false;
    bool collectStructure = false;
    bool dehyphenate = false;
    bool readingOrder = true;
    bool images = false;  // PRESERVE_IMAGES: блоки картинок (экспорт HTML/XHTML — картинки base64)

    bool operator==(const TextOptions&) const = default;
};

enum class TextBlockKind { Text, Image, Vector, Grid };

// Флаги символа — как fz_stext_char.flags (FZ_STEXT_*).
enum TextCharFlag : std::uint16_t {
    kCharStrikeout = 1,
    kCharUnderline = 2,
    kCharSynthetic = 4,  // пробел, добавленный по расстоянию между символами (в потоке его нет)
    kCharBold = 8,       // жирный шрифт или «поддельный» жирный обводкой
    kCharFilled = 16,
    kCharStroked = 32,
    kCharClipped = 64,
    kCharHighlight = 1024,
};

struct TextChar {
    char32_t c = 0;
    QuadF quad;               // у ACCURATE_BBOXES: по y — чернила глифа, по x — от пера до пера
    PointF origin;            // точка пера на базовой линии
    float size = 0;           // кегль, пт
    std::uint32_t argb = 0;   // цвет sRGB 0xAARRGGBB
    std::uint16_t flags = 0;  // TextCharFlag
    int font = -1;            // индекс в TextPage::fonts()
    int line = -1;            // индекс в TextPage::lines()
};

struct TextLine {
    RectF bbox;
    PointF dir{1, 0};  // направление базовой линии (единичный вектор)
    bool vertical = false;
    int block = -1;  // индекс в TextPage::blocks()
    int firstChar = 0;
    int charCount = 0;
};

struct TextBlock {
    TextBlockKind kind = TextBlockKind::Text;
    RectF bbox;
    int firstLine = 0;  // Text: строки [firstLine, firstLine + lineCount)
    int lineCount = 0;
    int vector = -1;      // Vector: индекс в TextPage::vectors()
    int structNode = -1;  // ближайший узел дерева структуры (collectStructure), −1 — вне структуры
    int sourcePage = -1;  // страница документа, откуда блок (fz_stext_page_details), −1 — неизвестно
};

// Векторный элемент (collectVectors): путь или его отрезок рамкой. Отрезок линии — «прямоугольник» толщиной в
// линию (rectangle = true): так MuPDF отличает подчёркивания и линейки форм от кривых.
struct TextVector {
    RectF bbox;
    std::uint32_t argb = 0;
    bool stroked = false;    // обводка (иначе заливка)
    bool rectangle = false;  // осевой прямоугольник или отрезок
    bool continues = false;  // дальше — следующий отрезок того же пути
    bool underline = false;  // MuPDF узнал подчёркивание текста
    bool strikeout = false;
    bool highlight = false;
};

struct TextFont {
    std::string name;  // без префикса подмножества «ABCDEF+»
    bool bold = false;
    bool italic = false;
    bool serif = false;
    bool monospaced = false;
};

// Узел дерева структуры (collectStructure): стандартный тип («P», «H1», «Table»…) и имя из файла.
struct TextStructNode {
    std::string type;
    std::string raw;
    int parent = -1;
    int index = 0;  // номер среди детей родителя в полном дереве
    std::vector<int> children;
};

// Слово: символы [firstChar, firstChar + charCount) одной строки.
struct TextWord {
    int firstChar = 0;
    int charCount = 0;
    RectF bbox;
    std::string text;  // UTF-8
};

}  // namespace ot
