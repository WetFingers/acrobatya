// Ядро аннотаций (план 2.2 — минимум, 5.1 — 28 типов; эталон 3.4 «Ядро», 4.1): список с подтипами, рамками
// и свойствами, хит-тест по типу, создание 14 типов, сеттеры, флаги, пунктир, перемещение с геометрией,
// ластик, копия в буфер и вставка, JSON — всё операциями журнала.
//
// Наружу — только значения: AnnotRef{page, objnum, nm} (номер косвенного объекта аннотации в /Annots страницы
// и её /NM) и AnnotInfo/AnnotProps; ни один pdf_annot* не покидает core (эталон 4.1, правило 1.2), UI хранит
// AnnotRef и перечитывает аннотацию по нему (pdf_first_annot/next) в каждой операции. Рамки и геометрия — в
// координатах страницы MuPDF (пункты, начало — левый верхний угол, y вниз; с /CropBox и /Rotate — как
// fz_bound_page и pdf_bound_annot): в них же работают рендер (pageMatrix из core/render/tiles.h), выделение и
// хит-тест. Список идёт по массиву /Annots целиком — с Link, Popup и Widget, которых нет в списке pdf_annot
// MuPDF (pdf_first_annot пропускает Link и Popup, виджеты держит отдельно); рамка таких — /Rect через матрицу
// страницы (pdf_page_transform), остальных — pdf_bound_annot (флаги NoZoom/NoRotate учтены MuPDF).
//
// Правка — Journal::Operation (внутри DocumentSession::edit — вложенная, шаг и имя — внешние); поток
// документа. Создание и сеттеры синтезируют внешний вид MuPDF (pdf_update_annot — 18 типов) внутри той же
// операции: /AP попадает в журнал и в файл. setRect без изменения размера — сдвиг всей геометрии (/Rect,
// /QuadPoints, /InkList, /Vertices, /L, /CL, рамка /Popup) без пересоздания /AP; с изменением размера
// геометрия масштабируется, а внешний вид линий, фигур, Ink и FreeText синтезируется заново (иконки и чужие
// штампы — вписываются в новую рамку по алгоритму 12.5.5 PDF). remove у pdf_annot — pdf_delete_annot (вместе
// с его /Popup, виджет — и из /AcroForm /Fields); Link и Popup — из /Annots (у родителя Popup снимается ключ
// /Popup).
#pragma once

#include <array>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/annot/ink_math.h"
#include "core/util/geometry.h"

namespace ot {

class Document;

namespace annot {

struct AnnotRef {
    int page = -1;   // индекс страницы (0…)
    int objnum = 0;  // номер косвенного объекта аннотации; 0 — прямой словарь в /Annots (не адресуется)
    std::string
        nm;  // /NM (если есть) — переживает полную запись, где номера объектов другие; в сравнении нет

    bool valid() const noexcept { return page >= 0 && objnum > 0; }
    bool operator==(const AnnotRef& other) const noexcept {
        return page == other.page && objnum == other.objnum;
    }
    std::strong_ordering operator<=>(const AnnotRef& other) const noexcept {
        if (const auto c = page <=> other.page; c != 0) {
            return c;
        }
        return objnum <=> other.objnum;
    }
};

// Подтипы — в порядке enum pdf_annot_type MuPDF 1.28.5 (значения совпадают).
enum class AnnotType : int {
    Unknown = -1,
    Text = 0,
    Link,
    FreeText,
    Line,
    Square,
    Circle,
    Polygon,
    PolyLine,
    Highlight,
    Underline,
    Squiggly,
    StrikeOut,
    Redact,
    Stamp,
    Caret,
    Ink,
    Popup,
    FileAttachment,
    Sound,
    Movie,
    RichMedia,
    Widget,
    Screen,
    PrinterMark,
    TrapNet,
    Watermark,
    ThreeD,  // /Subtype /3D
    Projection,
};

inline constexpr int kAnnotTypeCount = 28;

// Имя /Subtype ("Text", "3D"…); Unknown — пусто.
std::string_view annotTypeName(AnnotType type) noexcept;
// По имени /Subtype (с учётом регистра); незнакомое — Unknown.
AnnotType annotTypeFromName(std::string_view name) noexcept;
// Разметка текста (/QuadPoints): Highlight, Underline, Squiggly, StrikeOut.
bool isTextMarkup(AnnotType type) noexcept;
// Тип создаётся ядром (эталон 3.4: Text, FreeText, Line, Square, Circle, Polygon, PolyLine, Highlight,
// Underline, Squiggly, StrikeOut, Stamp, Caret, Ink, FileAttachment).
bool canCreate(AnnotType type) noexcept;
// Комментарий для панели и показа/скрытия (5.1, 5.4): пометка рецензента — не Link, Popup, Widget и не
// служебные (Screen, PrinterMark, TrapNet, Watermark, 3D, Projection, Movie, RichMedia, Sound).
bool isComment(AnnotType type) noexcept;

// Флаги /F (ISO 32000 12.5.3).
enum AnnotFlag : int {
    kAnnotInvisible = 1 << 0,
    kAnnotHidden = 1 << 1,
    kAnnotPrint = 1 << 2,
    kAnnotNoZoom = 1 << 3,
    kAnnotNoRotate = 1 << 4,
    kAnnotNoView = 1 << 5,
    kAnnotReadOnly = 1 << 6,
    kAnnotLocked = 1 << 7,
    kAnnotToggleNoView = 1 << 8,
    kAnnotLockedContents = 1 << 9,
};

// Цвет /C, /IC: 0 компонент — нет (прозрачный), 1 — серый, 3 — RGB, 4 — CMYK (как pdf_annot_color).
struct AnnotColor {
    int n = 0;
    std::array<float, 4> c{};

    static AnnotColor gray(float g) noexcept { return {1, {g, 0, 0, 0}}; }
    static AnnotColor rgb(float r, float g, float b) noexcept { return {3, {r, g, b, 0}}; }
    // 0xRRGGBB.
    static AnnotColor fromRgb(std::uint32_t rgb) noexcept;
    // В 0xRRGGBB (серый и CMYK — переводом); пустой — 0.
    std::uint32_t toRgb() const noexcept;
    bool empty() const noexcept { return n == 0; }
    bool operator==(const AnnotColor&) const = default;
};

// /BS /S — как enum pdf_border_style.
enum class BorderStyle { Solid, Dashed, Beveled, Inset, Underline };
// /LE — как enum pdf_line_ending.
enum class LineEnding {
    None,
    Square,
    Circle,
    Diamond,
    OpenArrow,
    ClosedArrow,
    Butt,
    ROpenArrow,
    RClosedArrow,
    Slash
};

// Кратко о каждой записи /Annots — для списка, выбора, панели «Комментарии» и оверлея.
struct AnnotInfo {
    AnnotRef ref;
    AnnotType type = AnnotType::Unknown;
    std::string subtype;  // /Subtype как в файле (у Unknown — что там записано)
    RectF rect;           // рамка в координатах страницы (см. выше)
    int flags = 0;        // /F
    std::string author;   // /T
    std::string contents;
    AnnotColor color;            // /C
    std::int64_t modified = 0;   // /M — секунды Unix (0 — нет или не разобрано)
    int inReplyTo = 0;           // /IRT — номер объекта (0 — нет)
    std::string replyType;       // /RT: "R" (ответ, по умолчанию) или "Group"
    int popup = 0;               // /Popup — номер объекта (0 — нет)
    bool open = false;           // окно записки открыто (/Open у Popup или Text)
    bool hasAppearance = false;  // есть /AP /N

    // Не видна на экране: Hidden или NoView (рендер MuPDF её пропускает).
    bool hiddenOnScreen() const noexcept { return (flags & (kAnnotHidden | kAnnotNoView)) != 0; }
    bool locked() const noexcept { return (flags & kAnnotLocked) != 0; }
};

// Все свойства одной аннотации: читаются readProps, создаются create, переносятся JSON (буфер, XFDF 5.4).
// Геометрия — в координатах страницы. При создании пустые nm, даты и subject заполняет ядро.
struct AnnotProps {
    AnnotType type = AnnotType::Unknown;
    RectF rect;  // рамка «дизайна» (pdf_annot_rect — без /RD); у разметки, Ink и вершин — из геометрии
    int flags = kAnnotPrint;
    std::string nm;
    std::string contents;
    std::string richContents;  // /RC (XHTML)
    std::string author;        // /T
    std::string subject;       // /Subj
    // /Name: Text — Comment, Key, Note, Help, NewParagraph, Paragraph, Insert; Stamp — Approved, Draft…;
    // FileAttachment — PushPin, Paperclip, Graph, Tag.
    std::string icon;
    AnnotColor color;          // /C
    AnnotColor interiorColor;  // /IC
    float opacity = 1.0f;      // /CA
    float borderWidth = 1.0f;  // /BS /W
    BorderStyle borderStyle = BorderStyle::Solid;
    std::vector<float> dash;  // /BS /D (пусто — сплошная)
    bool cloudy = false;      // /BE /S /C — облако
    float cloudIntensity = 1.0f;
    std::vector<QuadF> quads;  // /QuadPoints — разметка
    std::vector<Stroke> ink;   // /InkList
    std::vector<PointF> vertices;
    PointF lineStart, lineEnd;  // /L
    LineEnding lineStartStyle = LineEnding::None;
    LineEnding lineEndStyle = LineEnding::None;
    std::string intent;  // /IT как имя (FreeTextCallout, LineArrow, StrikeOutTextEdit…)
    // FreeText: /DA и /Q.
    std::string fontName = "Helv";
    float fontSize = 12.0f;
    AnnotColor textColor = AnnotColor::gray(0);
    int align = 0;                // 0 — влево, 1 — по центру, 2 — вправо
    std::vector<PointF> callout;  // /CL (2 или 3 точки)
    // Окно записки (/Popup): рамка в координатах страницы; пусто — нет окна.
    RectF popupRect = RectF::none();
    bool open = false;
    // Ответ: /IRT (номер объекта) и /RT.
    int inReplyTo = 0;
    std::string replyType;
    std::int64_t created = 0;   // /CreationDate, секунды Unix
    std::int64_t modified = 0;  // /M
    // FileAttachment: имя и содержимое вложения (только для create; чтение — вне ядра аннотаций).
    std::string fileName;
    std::string fileData;

    bool hasPopup() const noexcept { return popupRect.valid() && !popupRect.empty(); }
};

// Изменяемые свойства (update): заданные поля пишутся, остальные остаются.
struct AnnotPatch {
    std::optional<std::string> contents;
    std::optional<std::string> richContents;  // 5.4: /RC (пусто — снять)
    std::optional<std::string> author;
    std::optional<std::string> subject;
    std::optional<std::string> icon;
    std::optional<AnnotColor> color;
    std::optional<AnnotColor> interiorColor;
    std::optional<float> opacity;
    std::optional<float> borderWidth;
    std::optional<BorderStyle> borderStyle;
    std::optional<std::vector<float>> dash;
    std::optional<bool> cloudy;
    std::optional<int> flags;
    std::optional<bool> open;
    std::optional<RectF> popupRect;
    std::optional<std::vector<QuadF>> quads;
    std::optional<std::vector<Stroke>> ink;
    std::optional<std::vector<PointF>> vertices;
    std::optional<float> fontSize;
    std::optional<AnnotColor> textColor;
    std::optional<int> align;
    // 5.2: шрифт FreeText (семейство или имя /DA), линия выноски /CL, концы линии и выноски /LE, точки /L,
    // интенсивность облака /BE /I.
    std::optional<std::string> fontName;
    std::optional<std::vector<PointF>> callout;
    std::optional<LineEnding> lineStartStyle;
    std::optional<LineEnding> lineEndStyle;
    std::optional<PointF> lineStart;
    std::optional<PointF> lineEnd;
    std::optional<float> cloudIntensity;
    // Поле FreeText (координаты страницы; /Rect и /RD — заново по полю и выноске). Прочие типы — setRect.
    std::optional<RectF> rect;

    bool empty() const noexcept;
};

// Аннотации страницы в порядке /Annots (последняя рисуется поверх). Не PDF — пусто; страница вне диапазона —
// MuError. 5 000 на странице — один проход по спискам MuPDF.
std::vector<AnnotInfo> listAnnots(Document& doc, int page);
// Одна аннотация; нет на странице — nullopt.
std::optional<AnnotInfo> findAnnot(Document& doc, const AnnotRef& ref);
// Все свойства; нет — nullopt.
std::optional<AnnotProps> readProps(Document& doc, const AnnotRef& ref);

// Верхняя аннотация под точкой (координаты страницы) по типу: разметка — по квадам, Ink — расстояние до
// штриха, Line/PolyLine — до линии, Polygon — внутри или у границы, остальные — по рамке; сначала попадание
// (последняя в /Annots), затем ближайшая не дальше tolerance пт. Пропускаются невидимые на экране (Hidden,
// NoView) и Popup (окно записки рисует ui).
std::optional<AnnotInfo> annotAt(Document& doc, int page, PointF point, float tolerance = 0);

// Новая рамка в координатах страницы (как AnnotInfo::rect): тот же размер — сдвиг всей геометрии, другой —
// масштаб (см. выше). Операция журнала «Set annotation rectangle». Не PDF — std::logic_error; нет аннотации,
// пустая рамка — MuError(FZ_ERROR_ARGUMENT).
void setRect(Document& doc, const AnnotRef& ref, const RectF& rect);

// Удалить аннотации (одна операция журнала «Delete annotation»; страницы могут быть разными). Не PDF —
// std::logic_error; хоть одной нет — MuError(FZ_ERROR_ARGUMENT), документ как был.
void remove(Document& doc, const std::vector<AnnotRef>& refs);
inline void remove(Document& doc, const AnnotRef& ref) { remove(doc, std::vector<AnnotRef>{ref}); }

// Код шага журнала создания типа: "Add note", "Add highlight", "Add ink"… (подпись меню выбирает ui).
std::string createStepName(AnnotType type);

// Создать аннотацию по props: тип, геометрия (rect / quads / ink / vertices / line), стиль и тексты. Пустой
// nm — UUID, created/modified 0 — сейчас, пустой subject — английское имя типа Acrobat; Text получает окно
// (/Popup) по popupRect или справа от значка. Внешний вид — синтез MuPDF в той же операции. Операция журнала
// operation (пусто — createStepName). Тип вне canCreate, пустая геометрия, страница вне диапазона —
// MuError(FZ_ERROR_ARGUMENT).
AnnotRef create(Document& doc, int page, const AnnotProps& props, const std::string& operation = {});
// Группа одной операцией (эталон 3.4 «Заменить текст»: Caret + StrikeOut): первая — главная, остальные
// получают /IRT на неё и /RT /Group.
std::vector<AnnotRef> createGroup(Document& doc, int page, const std::vector<AnnotProps>& items,
                                  const std::string& operation);

// Записать заданные поля patch; /M — сейчас; внешний вид — заново. Операция «Set annotation properties».
void update(Document& doc, const AnnotRef& ref, const AnnotPatch& patch,
            const std::string& operation = "Set annotation properties");
// Флаги /F у нескольких: биты set — поставить, clear — снять (одна операция).
void setFlags(Document& doc, const std::vector<AnnotRef>& refs, int set, int clear,
              const std::string& operation = "Set annotation flags");
// Сдвиг на (dx, dy) пунктов страницы со всей геометрией; внешний вид не пересоздаётся (одна операция).
void translate(Document& doc, const std::vector<AnnotRef>& refs, float dx, float dy,
               const std::string& operation = "Move annotation");

// Ластик (эталон 3.4 «Рисовать» и «Ластик»): стереть у Ink части штрихов в кругах radius пт вокруг точек пути
// (ink_math.h eraseStrokes) и записать /InkList заново (pdf_set_annot_ink_list); ничего не осталось —
// аннотация удаляется. Операция «Erase ink»; не задето — false и шага нет.
bool eraseInk(Document& doc, const AnnotRef& ref, const std::vector<PointF>& path, float radius);

// Буфер обмена (comment-43): pdf — одностраничный PDF в памяти с копиями аннотаций (pdf_graft: внешний вид и
// его ресурсы, вложения — вместе), json — их свойства (toJson) и матрица исходной страницы. Вставка — в
// исходные координаты страницы (Acrobat), со сдвигом offset; новые /NM; ответы и /IRT не переносятся.
struct AnnotClip {
    std::string pdf;
    std::string json;
    int count = 0;
    bool empty() const noexcept { return count == 0; }
};
AnnotClip copy(Document& doc, const std::vector<AnnotRef>& refs);
// Вставить на страницу page одной операцией «Paste annotations». Пустой или чужой буфер — MuError.
std::vector<AnnotRef> paste(Document& doc, int page, const AnnotClip& clip, PointF offset = {});

// JSON свойств (буфер, XFDF 5.4, --script): ключи — имена ключей PDF без «/» (subtype, rect, C, IC, CA, BS…).
std::string toJson(const AnnotProps& props);
// Обратно; неверный JSON — std::invalid_argument.
AnnotProps propsFromJson(std::string_view json);

// Новый /NM: UUID v4 строчными буквами.
std::string newAnnotName();

}  // namespace annot
}  // namespace ot
