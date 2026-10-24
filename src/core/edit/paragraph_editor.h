// ParagraphEditor — модель правки абзаца (эталон 3.9 «Модель»: правка → перевёрстка Reflow → Rewriter; план
// 3.3, п. 1; закрывает edit-01 — модель правки). Абзац — логическая строка символов (UTF-32) с пробегами
// стилей: у каждого символа — стиль (шрифт исходного пробега или семейство, выбранное человеком, кегль, цвет,
// жирный, курсив, подчёркивание, подъём Ts, межбуквенный Tc, масштаб Tz) и, если символ пришёл из файла, —
// код его глифа и ширина (Rewriter пишет нетронутые символы теми же кодами). Операции: insert, erase,
// replace, applyStyle, setAlignment, setLineSpacing; курсорные позиции ↔ координаты страницы — по раскладке:
// сначала по позициям глифов модели, после правки — по раскладке перевёрстки (setLayout).
//
// Строка — как TextBlock::text (core/edit/text_blocks.h): строки через пробел, после дефиса — без пробела,
// пробел между спанами, разделёнными промежутком, «тень» и «ложный жирный» — один раз, пробелы по краям
// сняты. Раскладка — в пространстве пользователя страницы с y вниз (как BlockLine::x0/x1/y): на странице с
// /Rotate абзац тот же; наружу (курсор, выделение) — координаты страницы MuPDF, как у TextPage.
//
// Значения без MuPDF: строится в потоке документа (fromBlock), правится и читается в любом потоке.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/edit/edit_types.h"
#include "core/util/geometry.h"

namespace ot::edit {

class ContentModel;
struct TextBlock;

// Стиль символа. Размеры — пункты страницы (пространство пользователя), не единицы пространства текста.
struct CharStyle {
    int font = -1;             // шрифт исходного пробега — индекс ContentModel::fonts() (−1 — нет)
    std::string fontResource;  // его имя в /Resources/Font страницы ("F1")
    std::string fontName;      // его /BaseFont без префикса подмножества (для панели и журнала)
    std::string family;        // семейство, выбранное человеком (системный шрифт); пусто — шрифт пробега
    float size = 12;           // кегль, пт
    std::uint32_t color = 0xFF000000;  // sRGB 0xAARRGGBB
    std::string colorOps;   // исходные операторы цвета заливки («/CS0 cs 0.2 scn»); пусто — color через rg
    std::string strokeOps;  // исходные операторы цвета обводки (Tr 1, 2); пусто — как заливка
    bool bold = false;
    bool italic = false;
    bool underline = false;
    float rise = 0;         // подъём над базовой линией (Ts), пт
    float charSpacing = 0;  // межбуквенный интервал (Tc) — добавка после каждого символа, пт
    float wordSpacing = 0;  // межсловный (Tw) — добавка после пробела, пт (у абзаца по ширине — 0: растяжку
                            // считает перевёрстка)
    float hscale =
        1;  // горизонтальный масштаб (Tz / 100; у матрицы текста с разными масштабами — их отношение)
    float skew = 0;  // наклон исходной матрицы текста (x += skew · y)
    int render = 0;  // Tr 0–3 (заливка, обводка, оба, невидимый)

    bool operator==(const CharStyle&) const = default;
};

// Изменение стиля (applyStyle): заданные поля меняются, остальные — как были у каждого символа.
struct StyleChange {
    std::optional<std::string> family;
    std::optional<float> size;
    std::optional<std::uint32_t> color;
    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underline;
    std::optional<float> rise;
    std::optional<float> charSpacing;
    std::optional<float> wordSpacing;
    std::optional<float> hscale;
    std::optional<int> render;

    CharStyle applied(CharStyle style) const;
};

// Положение символа относительно строки (панель FORMAT TEXT: верхний и нижний индекс).
enum class Script : std::uint8_t { Normal, Super, Sub };
constexpr float kScriptScale = 0.65f;  // кегль индекса — доля кегля строки
constexpr float kSuperRise = 0.35f;    // подъём верхнего индекса — доля кегля строки
constexpr float kSubRise = -0.15f;     // нижнего
// По подъёму Ts: выше базовой линии — верхний, ниже — нижний.
Script scriptOf(const CharStyle& style) noexcept;
// Изменение стиля style до положения script: индекс — кегль · 0,65 и подъём от кегля строки; обычный —
// кегль строки обратно и подъём 0.
StyleChange scriptChange(const CharStyle& style, Script script);

struct EditChar {
    char32_t ch = 0;
    int style = 0;     // индекс в ParagraphEditor::styles()
    std::string code;  // код исходного глифа в шрифте style.font (у первого символа глифа); пусто — набран
    float w0 = 0;      // ширина исходного глифа, доли кегля
    std::uint8_t group = 1;  // символов в исходном глифе (лигатура «fi» — 2) — у первого; 0 — продолжение
};

struct TextRange {
    int begin = 0;
    int end = 0;  // не включительно
    bool empty() const noexcept { return end <= begin; }
    int size() const noexcept { return end > begin ? end - begin : 0; }
    bool operator==(const TextRange&) const = default;
};

// Пробег — подряд идущие символы одного стиля.
struct StyledRun {
    int begin = 0;
    int end = 0;
    int style = 0;
};

// Строка раскладки (пространство раскладки — пользователь страницы, y вниз).
struct LayoutLine {
    int begin = 0;  // символы [begin, end) — с пробелами в конце строки
    int end = 0;
    float x0 = 0;            // перо у первого символа
    float x1 = 0;            // правый край последнего видимого символа (пробелы в конце не считаются)
    float baseline = 0;      // y базовой линии (без Ts)
    float ascent = 0;        // пт над базовой линией
    float descent = 0;       // пт под базовой линией (≥ 0)
    float wordSpace = 0;     // добавка на пробел (выравнивание по ширине), пт
    bool hardBreak = false;  // строка кончается переводом строки
};

struct TextLayout {
    std::vector<LayoutLine> lines;
    std::vector<float> x;          // перо у каждого символа
    std::vector<float> advance;    // ширина символа (с Tc, масштабом и добавкой по ширине)
    RectF bounds = RectF::none();  // строки целиком: от верха первой до низа последней (раскладка)

    bool empty() const noexcept { return lines.empty(); }
};

// Курсор: отрезок от верха до низа строки в позиции (координаты страницы).
struct Caret {
    int line = 0;
    float x = 0;  // раскладка
    PointF top;
    PointF bottom;
};

// Геометрия набора (раскладка).
struct ParagraphGeometry {
    float left = 0;           // левый край строк (у первой — со своим отступом)
    float width = 0;          // ширина набора: перенос по ней
    float firstBaseline = 0;  // базовая линия первой строки (без Ts) — абзац растёт вниз от неё
    float firstIndent = 0;    // отступ первой строки от left (отрицательный — выступ, как у пункта списка)
    float leading = 0;        // исходный межстрочный интервал, пт (0 — одна строка)
    float fontSize = 0;       // кегль абзаца (медиана по символам), пт
    Matrix toPage;            // раскладка → координаты страницы
};

// Откуда абзац: операторы показа модели, по которой построен редактор (Rewriter заменяет их).
struct ParagraphSource {
    int page = 0;
    std::vector<int> ops;  // операторы показа по порядку потока
    std::string text;      // их текст (ContentModel::spanText) — та же ли модель
    RectF bbox;            // рамка блока (координаты страницы)
};

class ParagraphEditor {
public:
    // Межстрочный по умолчанию у абзаца из одной строки: интервал = кегль · 1,2.
    static constexpr float kDefaultLineSpacing = 1.2f;

    ParagraphEditor() = default;

    // Абзац block модели model. pageBlocks — все блоки страницы (blocksForPage): у абзаца из одной строки
    // ширина набора — до края колонки или до поля страницы, зеркального левому. Блок «только чтение» —
    // std::invalid_argument.
    static ParagraphEditor fromBlock(const ContentModel& model, const TextBlock& block,
                                     const std::vector<TextBlock>& pageBlocks);
    static ParagraphEditor fromBlock(const ContentModel& model, const TextBlock& block);

    // Текст.
    int size() const noexcept { return static_cast<int>(chars_.size()); }
    bool empty() const noexcept { return chars_.empty(); }
    std::u32string text() const;
    std::string utf8() const;
    std::u32string text(TextRange range) const;
    const std::vector<EditChar>& chars() const noexcept { return chars_; }
    const std::vector<CharStyle>& styles() const noexcept { return styles_; }
    const CharStyle& styleAt(int pos) const;  // символа pos (за концом — последнего; пустой — по умолчанию)
    std::vector<StyledRun> runs() const;
    // Стиль, которым наберётся текст в позиции pos: символа перед ней (в начале — первого).
    const CharStyle& typingStyle(int pos) const;

    // Правка. Позиции зажимаются в [0, size()]; возвращают позицию курсора после правки. Табуляция — пробел,
    // «\r\n» и «\r» — «\n» (перевод строки внутри абзаца).
    int insert(int pos, std::u32string_view text);
    int insert(int pos, std::u32string_view text, const CharStyle& style);
    int erase(TextRange range);
    int replace(TextRange range, std::u32string_view text);  // стиль — первого заменяемого символа
    void applyStyle(TextRange range, const StyleChange& change);
    void setAlignment(Align align);
    Align alignment() const noexcept { return align_; }
    // Межстрочный интервал как доля кегля строки (исходный — leading / кегль абзаца).
    void setLineSpacing(float factor);
    float lineSpacing() const noexcept { return lineSpacing_; }
    // Изменён после построения или последнего markWritten.
    bool modified() const noexcept { return modified_; }

    const ParagraphGeometry& geometry() const noexcept { return geometry_; }
    void setGeometry(const ParagraphGeometry& g) { geometry_ = g; }
    const ParagraphSource& source() const noexcept { return source_; }
    // После записи (Rewriter): абзац теперь — эти операторы новой модели (коды символов прежние: исходный
    // шрифт тот же, новые шрифты выбираются заново тем же правилом).
    void markWritten(ParagraphSource source);

    // Раскладка и курсор.
    const TextLayout& layout() const noexcept { return layout_; }
    // Раскладка совпадает с текстом (после правки — нет, пока не придёт новая от перевёрстки; до того
    // позиции правки сдвинуты приблизительно).
    bool layoutValid() const noexcept { return layoutValid_; }
    void setLayout(TextLayout layout);
    int lineCount() const noexcept { return static_cast<int>(layout_.lines.size()); }
    int lineOf(int pos) const;
    Caret caret(int pos) const;
    // Ближайшая позиция курсора к точке страницы.
    int positionAt(PointF pagePoint) const;
    // Позиция на соседней строке под тем же x (delta — строк вниз; за краем — начало/конец текста).
    int moveLine(int pos, int delta) const;
    // Слово вокруг позиции (буквы, цифры, метки); вне слова — пусто в pos.
    TextRange wordAt(int pos) const;
    // Рамки выделения по строкам (координаты страницы).
    std::vector<QuadF> selectionQuads(TextRange range) const;

private:
    int styleIndex(const CharStyle& style);
    TextRange clamp(TextRange r) const;
    float xAt(int pos, const LayoutLine& line) const;
    void shiftLayout(int pos, int delta);
    PointF toPage(float x, float y) const;
    PointF toLayout(PointF page) const;

    std::vector<EditChar> chars_;
    std::vector<CharStyle> styles_;
    Align align_ = Align::Left;
    float lineSpacing_ = kDefaultLineSpacing;
    ParagraphGeometry geometry_;
    ParagraphSource source_;
    TextLayout layout_;
    bool layoutValid_ = false;
    bool modified_ = false;
};

}  // namespace ot::edit
