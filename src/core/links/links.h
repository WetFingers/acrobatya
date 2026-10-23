// Ссылки страницы (план 2.2b; эталон 3.1 «Навигация», 7.4; view-13): рамки и действия для вида — переход по
// щелчку, курсор-указатель, Tab по ссылкам. Наружу — только значения (LinkInfo, LinkAction); ни один fz_link
// не покидает core.
//
// Рамки и адреса — fz_load_links (MuPDF превращает /Dest и /A в URI: «#page=3&view=XYZ,72,142,0»,
// «#nameddest=…», «https://…», «file:other.pdf#page=2»), назначение внутри документа — fz_resolve_link_dest
// (страница и точка в координатах страницы MuPDF, y вниз). Вид действия MuPDF не сохраняет (/Launch и /GoToR
// без назначения дают одинаковый «file:…», /JavaScript — ничего) — у PDF он читается из словаря /Link
// (/Dest; /A, иначе /AA /U или /D — как у MuPDF), ссылка MuPDF сопоставляется по рамке. Ссылка без адреса у
// MuPDF (/JavaScript, неизвестный /Named) остаётся в списке видом Unsupported: курсор-указатель есть, переход
// — нет (ui сообщает). Не PDF (XPS, EPUB) — только fz_load_links: внешний адрес — Uri, «#…» — GoTo.
//
// Что делать с действием, решает ui (2.4/2.5): GoTo и Named — переход в виде, GoToR — спросить и открыть,
// Uri — подтверждение (эталон 7.4), Launch — никогда (эталон 7.6).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/util/geometry.h"

namespace ot {

class Document;

namespace links {

enum class LinkKind {
    GoTo,         // назначение в этом документе (/Dest, /GoTo)
    GoToR,        // другой PDF (/GoToR): file + page или destName
    Uri,          // адрес (/URI): uri
    Named,        // /Named (NextPage, PrevPage, FirstPage, LastPage): name и page назначения
    Launch,       // /Launch: file — запуск программ запрещён всегда
    Unsupported,  // остальное (/JavaScript, /SubmitForm…): name — значение /S
};

std::string_view linkKindName(LinkKind kind) noexcept;

// Вид назначения (ISO 32000 12.3.2.2).
enum class DestFit { None, XYZ, Fit, FitH, FitV, FitR, FitB, FitBH, FitBV };

struct LinkAction {
    LinkKind kind = LinkKind::Unsupported;
    std::string uri;       // адрес MuPDF (fz_link.uri); у Unsupported — пусто
    std::string file;      // GoToR, Launch — путь из /F (UTF-8, как в файле)
    std::string name;      // Named — /N; Unsupported — /S
    std::string destName;  // именованное назначение (GoTo по имени, GoToR /D (имя))
    int page = -1;         // GoTo, Named — страница назначения (0…); GoToR — страница другого файла; -1 — нет
    DestFit fit = DestFit::None;
    std::optional<PointF> point;  // GoTo — левый верхний угол вида (координаты страницы назначения)
    float zoom = 0;               // /XYZ: масштаб (1 — 100 %), 0 — не менять

    bool internal() const noexcept { return kind == LinkKind::GoTo || kind == LinkKind::Named; }
};

struct LinkInfo {
    int page = -1;   // страница ссылки
    int objnum = 0;  // номер объекта аннотации /Link (0 — прямой словарь или не PDF)
    RectF rect;      // рамка в координатах страницы MuPDF
    LinkAction action;
};

// Ссылки страницы в порядке /Annots (не PDF — в порядке fz_load_links). Страница вне диапазона — MuError.
std::vector<LinkInfo> pageLinks(Document& doc, int page);

// Верхняя ссылка под точкой (последняя в списке, чья рамка ближе tolerance пт); нет — nullopt.
std::optional<LinkInfo> linkAt(const std::vector<LinkInfo>& links, PointF point, float tolerance = 0);

// Действие по адресу MuPDF (закладки, назначения 2.4): «#…» — GoTo с разрешённым назначением (страница −1 —
// не нашлось), «file:…» — GoToR (file — путь, page — из «#page=N»), прочий внешний — Uri; пусто —
// Unsupported.
LinkAction actionFromUri(Document& doc, std::string_view uri);

}  // namespace links
}  // namespace ot
