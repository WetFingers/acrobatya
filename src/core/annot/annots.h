// Аннотации страницы — минимальное ядро (план 2.2; эталон 3.4, 4.1): список с подтипами и рамками, хит-тест,
// рамка (/Rect) и удаление как операции журнала. Сессия 5.1 расширяет этот модуль (28 типов, сеттеры свойств,
// JSON), а не создаёт заново.
//
// Наружу — только значения: AnnotRef{page, objnum} (номер косвенного объекта аннотации в /Annots страницы) и
// AnnotInfo; ни один pdf_annot* не покидает core (эталон 4.1, правило 1.2), UI хранит AnnotRef. Рамки — в
// координатах страницы MuPDF (пункты, начало — левый верхний угол, y вниз; с /CropBox и /Rotate — как
// fz_bound_page и pdf_bound_annot): в них же работают рендер (pageMatrix из core/render/tiles.h), выделение и
// хит-тест. Список идёт по массиву /Annots целиком — с Link, Popup и Widget, которых нет в списке pdf_annot
// MuPDF (pdf_first_annot пропускает Link и Popup, виджеты держит отдельно); рамка таких — /Rect через матрицу
// страницы (pdf_page_transform), остальных — pdf_bound_annot (флаги NoZoom/NoRotate учтены MuPDF).
//
// Правка — Journal::Operation (внутри DocumentSession::edit — вложенная, шаг и имя — внешние); поток
// документа. setRect пишет /Rect и не пересоздаёт внешний вид: /AP вписывается в новую рамку (алгоритм 12.5.5
// PDF — чужой штамп не заменяется рисунком MuPDF); у линий, ломаных и Ink вершины не двигаются — их перенос и
// свой хит-тест разметки по квадам — сессия 5.1. remove у pdf_annot — pdf_delete_annot (вместе с его /Popup,
// виджет — и из /AcroForm /Fields); Link и Popup — из /Annots (у родителя Popup снимается ключ /Popup).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/geometry.h"

namespace ot {

class Document;

namespace annot {

struct AnnotRef {
    int page = -1;   // индекс страницы (0…)
    int objnum = 0;  // номер косвенного объекта аннотации; 0 — прямой словарь в /Annots (не адресуется)

    bool valid() const noexcept { return page >= 0 && objnum > 0; }
    bool operator==(const AnnotRef&) const = default;
    auto operator<=>(const AnnotRef&) const = default;
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

struct AnnotInfo {
    AnnotRef ref;
    AnnotType type = AnnotType::Unknown;
    std::string subtype;  // /Subtype как в файле (у Unknown — что там записано)
    RectF rect;           // рамка в координатах страницы (см. выше)
    int flags = 0;        // /F

    // Не видна на экране: Hidden или NoView (рендер MuPDF её пропускает).
    bool hiddenOnScreen() const noexcept { return (flags & (kAnnotHidden | kAnnotNoView)) != 0; }
};

// Аннотации страницы в порядке /Annots (последняя рисуется поверх). Не PDF — пусто; страница вне диапазона —
// MuError.
std::vector<AnnotInfo> listAnnots(Document& doc, int page);
// Одна аннотация; нет на странице — nullopt.
std::optional<AnnotInfo> findAnnot(Document& doc, const AnnotRef& ref);

// Верхняя аннотация под точкой (координаты страницы): последняя в /Annots, чья рамка ближе tolerance пт.
// Пропускаются невидимые на экране (Hidden, NoView) и Popup (MuPDF их не рисует; окно записки — 5.1).
std::optional<AnnotInfo> annotAt(Document& doc, int page, PointF point, float tolerance = 0);

// Новая рамка в координатах страницы (как AnnotInfo::rect): /Rect = рамка через обратную матрицу страницы.
// Операция журнала «Set annotation rectangle». Не PDF — std::logic_error; нет аннотации, пустая рамка —
// MuError(FZ_ERROR_ARGUMENT).
void setRect(Document& doc, const AnnotRef& ref, const RectF& rect);

// Удалить аннотации (одна операция журнала «Delete annotation»; страницы могут быть разными). Не PDF —
// std::logic_error; хоть одной нет — MuError(FZ_ERROR_ARGUMENT), документ как был.
void remove(Document& doc, const std::vector<AnnotRef>& refs);
inline void remove(Document& doc, const AnnotRef& ref) { remove(doc, std::vector<AnnotRef>{ref}); }

}  // namespace annot
}  // namespace ot
