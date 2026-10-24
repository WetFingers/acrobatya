// ContentModel — модель контент-потока страницы для редактора текста (эталон 3.9 «Модель», план 3.1;
// закрывает edit-01 — модель). Строится собственным токенизатором поверх pdf_lex: до и после каждого токена —
// fz_tell, так у каждой операции есть смещение и длина в ДЕКОДИРОВАННОМ потоке (pdf_load_stream каждого
// элемента /Contents; у операции — индекс потока). Байты операции — от конца предыдущей до конца её оператора
// (с пробелами и комментариями перед ней): операции потока подряд и «хвост» после последней дают поток байт в
// байт.
//
// Графическое состояние считается по операторам (q/Q/cm/gs, BT…ET,
// Tf/Td/TD/Tm/T*/TL/Tc/Tw/Tz/Ts/Tr/Tj/TJ/'/", Do, BI…EI, BX/EX, BMC/BDC/EMC, цвета заливки и обводки) по
// правилам интерпретатора MuPDF (pdf-interpret.c, pdf-op-run.c): лишние операнды не мешают (берутся первые),
// недостающие — нули, ошибка разбора сбрасывает операнды, endstream завершает поток, операнды переходят из
// потока в поток массива /Contents. Текст XObject-форм входит в модель (Do разворачивается на месте), но
// формы не редактируются — «только чтение».
//
// Глифы (core/edit/glyph_map.h): Unicode и ширины как у MuPDF; геометрия — Tsm × Tm × CTM (с Tz, Tc, Tw
// только у однобайтового кода 32, Ts, TJ-сдвиги), координаты страницы MuPDF (как TextPage: y вниз, с
// /Rotate), рамка символа — как у fz_stext без ACCURATE_BBOXES (восходящий и нисходящий элементы шрифта по
// ширине глифа).
// Выключенные слои (BDC /OC, форма с /OC) — как у интерпретатора (pdf_is_ocg_hidden, назначение View): текст
// в модели есть, но «только чтение» (Hidden); форма с выключенным /OC не разворачивается. crossCheck —
// перекрёстная проверка через pdf_new_run_processor + fz_stext по индексу символа (скрытый текст fz_stext не
// видит — он пропускается; строки справа налево fz_stext переставляет — они расходятся).
//
// Модель — значения без MuPDF: строится в потоке документа (build), читается из любого потока.
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/edit/glyph_map.h"
#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::edit {

struct TextBlock;

// Операнд оператора — своё значение (без pdf_obj).
struct Operand {
    enum class Kind : std::uint8_t { Number, Name, String, Array, Dict, Bool, Null, Keyword };
    Kind kind = Kind::Null;
    double number = 0;           // Number; Bool — 0 или 1
    std::string text;            // Name (без «/»), String (байты), Keyword (R внутри массива и т. п.)
    std::vector<Operand> items;  // Array — элементы; Dict — ключ (Name), значение, ключ, значение…

    bool isNumber() const noexcept { return kind == Kind::Number; }
    // Значение ключа словаря (Dict) или nullptr.
    const Operand* get(std::string_view key) const noexcept;
};

enum class OpKind : std::uint8_t {
    Unknown,  // оператор, который модель не толкует (пути, тени, d0…), или незнакомый
    Save,
    Restore,
    Concat,              // cm
    ExtGState,           // gs
    BeginText,           // BT
    EndText,             // ET
    Font,                // Tf
    Move,                // Td
    MoveLeading,         // TD
    TextMatrix,          // Tm
    NextLine,            // T*
    Leading,             // TL
    CharSpace,           // Tc
    WordSpace,           // Tw
    Scale,               // Tz
    Rise,                // Ts
    Render,              // Tr
    ShowText,            // Tj
    ShowArray,           // TJ
    NextLineShow,        // '
    NextLineShowSpaced,  // "
    XObject,             // Do
    InlineImage,         // BI … ID … EI одной операцией
    BeginCompat,         // BX
    EndCompat,           // EX
    BeginMarked,         // BMC, BDC
    EndMarked,           // EMC
    MarkPoint,           // MP, DP
    FillColor,           // g, rg, k, cs, sc, scn
    StrokeColor,         // G, RG, K, CS, SC, SCN
    Garbage,             // байты, которые не разобрались (ошибка лексера или операнды без оператора)
};

bool isShowOp(OpKind kind) noexcept;

struct Operation {
    OpKind kind = OpKind::Unknown;
    std::string name;  // ключевое слово как в потоке ("Tj", "BI" у inline image, "" у Garbage)
    std::vector<Operand> operands;
    int stream = 0;          // индекс в ContentModel::streams()
    std::size_t offset = 0;  // байты операции — [offset, offset + length) декодированного потока
    std::size_t length = 0;
    std::size_t tokenOffset = 0;  // первый токен операции (после пробелов и комментариев)
    bool split = false;           // операнды начались в предыдущем потоке массива /Contents
    int textObject = -1;          // BT…ET, в котором операция (−1 — вне текста)
    int span = -1;                // оператор показа — его спан
};

struct ContentStream {
    int objnum = 0;  // объект потока (0 — неизвестен)
    int gen = 0;
    bool form = false;     // поток XObject-формы (не редактируется)
    int parentOp = -1;     // форма — операция Do, которая её показала
    int depth = 0;         // вложенность форм (0 — поток страницы)
    std::string bytes;     // декодированный поток
    std::vector<int> ops;  // операции потока по порядку
    std::size_t tail = 0;  // байты после последней операции — [tail, size)
};

struct TextObject {
    int begin = -1;  // операция BT
    int end = -1;    // операция ET (−1 — не закрыт)
    std::vector<int> spans;
    bool syntaxError = false;
};

struct Glyph {
    std::u32string unicode;  // один или несколько символов (U+FFFD — неизвестный)
    std::uint32_t code = 0;  // код в строке
    std::uint8_t codeBytes = 1;
    int cid = 0;
    int gid = 0;
    float w0 = 0;   // ширина в долях кегля
    float x = 0;    // сдвиг начала глифа от Tm спана в пространстве текста (до Ts)
    PointF origin;  // начало глифа на базовой линии (с Ts), координаты страницы
    QuadF quad;     // рамка как у fz_stext без ACCURATE_BBOXES, координаты страницы
    int span = -1;
    std::uint16_t item = 0;        // элемент массива TJ (у Tj — 0)
    std::uint32_t byteOffset = 0;  // смещение кода в строке
    bool wordSpace() const noexcept { return codeBytes == 1 && code == 32; }
};

// Спан — глифы одного оператора показа (Tj, TJ, ', ") в одном состоянии.
struct TextSpan {
    int op = -1;
    int textObject = -1;
    int font = -1;  // индекс в ContentModel::fonts()
    int firstGlyph = 0;
    int glyphCount = 0;
    float size = 0;  // кегль Tf
    float charSpace = 0, wordSpace = 0, scale = 1, leading = 0, rise = 0;
    int render = 0;
    std::uint32_t fill = 0xFF000000;  // sRGB 0xAARRGGBB (приближённо: Gray/RGB/CMYK по числу компонент)
    std::uint32_t stroke = 0xFF000000;
    Matrix tm;   // матрица текста перед первым глифом (после T* у ' и ")
    Matrix tlm;  // матрица строки в этот момент
    Matrix ctm;  // CTM пространства пользователя (без преобразования страницы)
    float advance =
        0;            // сдвиг всего оператора по x пространства текста: Tm после = translate(advance, 0) × tm
    PointF origin;    // начало первого глифа (с Ts), координаты страницы
    PointF baseline;  // то же без Ts — по нему строки
    PointF end;       // конец последнего глифа (начало + ширина × кегль × Tz, без Tc/Tw)
    PointF dir{1, 0};    // направление базовой линии на странице (единичный вектор)
    float angle = 0;     // угол базовой линии в пространстве пользователя, градусы (0 — горизонтально)
    float fontSize = 0;  // кегль на странице (растяжение Trm × CTM)
    RectF bbox;          // объединение рамок глифов, координаты страницы
    int stream = 0;
    bool form = false;
    int mcid = -1;        // ближайший BDC с /MCID (−1 — нет)
    bool hidden = false;  // в выключенном слое (BDC /OC, форма с /OC) — как proc->hidden интерпретатора
    ReadOnly reason = ReadOnly::None;
    bool editable() const noexcept { return reason == ReadOnly::None; }
};

// Итог пересборки абзаца (rebuildParagraph).
struct RebuildResult {
    bool ok = false;  // блок можно править, операторы заменены
    int ops = 0;      // заменённых операторов показа
    int glyphs = 0;
};

// Итог перекрёстной проверки геометрии (crossCheck).
struct CrossCheck {
    int modelChars = 0;  // символов модели (глиф с несколькими Unicode — несколько символов)
    int stextChars = 0;  // символов fz_stext
    int compared = 0;
    int mismatches = 0;  // другой символ или угол рамки дальше допуска
    float maxDelta = 0;  // наибольшее расхождение углов рамок, пт
    std::string firstMismatch;
};

class ContentModel {
public:
    // Модель страницы page документа doc (PDF) в потоке документа. cookie: abort = 1 прерывает
    // (MuError Aborted). Не PDF — std::logic_error; MuPDF — MuError.
    static ContentModel build(const Document& doc, int page, fz_cookie* cookie = nullptr);

    int page() const noexcept { return page_; }
    const RectF& pageBounds() const noexcept { return bounds_; }
    const Matrix& pageMatrix() const noexcept { return pageCtm_; }  // пространство PDF → координаты страницы

    const std::vector<ContentStream>& streams() const noexcept { return streams_; }
    const std::vector<Operation>& operations() const noexcept { return ops_; }
    const std::vector<TextObject>& textObjects() const noexcept { return texts_; }
    const std::vector<TextSpan>& spans() const noexcept { return spans_; }
    const std::vector<Glyph>& glyphs() const noexcept { return glyphs_; }
    const std::vector<FontInfo>& fonts() const noexcept { return fonts_; }
    int syntaxErrors() const noexcept { return syntaxErrors_; }

    // Исходные байты операции (с пробелами перед ней).
    std::string_view raw(int op) const;
    // Декодированный поток stream заново: байты операций по порядку и хвост. Без правок — байт в байт как
    // ContentStream::bytes (массив /Contents — по элементу, inline image и BX/EX — как были).
    std::string serialize(int stream) const;
    // Поток изменён пересборкой (есть заменённые операции).
    bool modified(int stream) const;
    // Заменённые операции: индекс → новые байты (с пробелами перед ней, как у исходной).
    const std::map<int, std::string>& replacements() const noexcept { return replaced_; }

    // «Пустая правка» (эталон 3.9 «Стабильность»): операторы показа абзаца block (Tj, TJ, ', ") заменяются
    // новыми байтами без изменения текста — явная матрица текста (Tm) и TJ с кодами глифов и сдвигами по их
    // позициям в модели; ' и " — своим T* (и Tw/Tc); Tm и Tlm после оператора — как были (Tlm возвращается
    // Tm, перо — сдвигом TJ). Совпадение рендера и fz_stext после записи — проверка того, что модель поняла
    // позиции так же, как MuPDF. Блок «только чтение» не трогается (ok = false). Операции блока заменяются
    // целиком.
    RebuildResult rebuildParagraph(const TextBlock& block);
    // Записать изменённые потоки страницы в документ одной операцией журнала (operation — её имя): поток
    // сжимается FlateDecode, pdf_update_stream(…, compressed = 1), /DecodeParms снимается. Потоки форм не
    // пишутся (формы не редактируются). Возвращает число записанных потоков. Поток, общий с другими
    // страницами, меняется и у них.
    int apply(Document& doc, const std::string& operation = "Edit text") const;
    // Текст спана в UTF-8 (только глифы потока, без добавленных пробелов).
    std::string spanText(int span) const;

    // Перекрёстная проверка: та же страница через pdf_new_run_processor в fz_stext (содержимое без пометок,
    // без добавленных пробелов и без ActualText) — символ за символом по индексу: Unicode и углы рамок
    // в пределах tolerance пт.
    static CrossCheck crossCheck(const Document& doc, int page, const ContentModel& model,
                                 float tolerance = 0.5f);

private:
    friend class ModelBuilder;

    int page_ = 0;
    RectF bounds_;
    Matrix pageCtm_;
    std::vector<ContentStream> streams_;
    std::vector<Operation> ops_;
    std::vector<TextObject> texts_;
    std::vector<TextSpan> spans_;
    std::vector<Glyph> glyphs_;
    std::vector<FontInfo> fonts_;
    int syntaxErrors_ = 0;
    std::map<int, std::string> replaced_;
};

}  // namespace ot::edit
