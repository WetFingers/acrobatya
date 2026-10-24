// Начальный вид документа (план 2.4 — чтение при открытии; 2.5 — запись в «Свойства → Начальный вид» и Base
// URL; эталон 3.1 «Acrobat»: /ViewerPreferences и /OpenAction уважаются, наши настройки в файл не пишутся;
// view-47): /PageMode (какая панель открыта: закладки, миниатюры, слои, вложения, полный экран), /PageLayout,
// /OpenAction (переход: страница и вид — как у ссылок; JavaScript и прочие действия не исполняются),
// основные /ViewerPreferences и заголовок /Info /Title для DisplayDocTitle. Значения — как в файле (имена
// PDF).
#pragma once

#include <optional>
#include <string>

#include "core/links/links.h"

namespace ot {

class Document;

struct InitialView {
    // /PageMode: UseNone, UseOutlines, UseThumbs, FullScreen, UseOC, UseAttachments; пусто — нет ключа.
    std::string pageMode;
    std::string pageLayout;                       // SinglePage, OneColumn, TwoColumnLeft…; пусто — нет
    std::optional<links::LinkAction> openAction;  // GoTo со страницей; иначе — нет
    // /ViewerPreferences
    bool hideToolbar = false;
    bool hideMenubar = false;
    bool hideWindowUI = false;
    bool fitWindow = false;
    bool centerWindow = false;
    bool displayDocTitle = false;
    std::string nonFullScreenPageMode;  // режим панелей после выхода из /PageMode /FullScreen
    std::string direction;              // L2R, R2L; пусто — нет
    std::string title;                  // /Info /Title (UTF-8)
};

// Не PDF — пусто. Испорченные ключи пропускаются (значение по умолчанию).
InitialView readInitialView(Document& doc);

// Записать начальный вид (2.5, «Свойства → Начальный вид»; операция журнала «Initial view» — лёгкая правка,
// сохраняется инкрементально): /PageMode, /PageLayout (пусто — ключ удаляется), /OpenAction — GoTo на
// страницу с видом /Fit, /FitH, /FitV, /FitB, /FitBH, /FitBV или /XYZ с масштабом (zoom 1 — 100 %, 0 — как
// есть; нет действия или страница вне документа — ключ удаляется), основные /ViewerPreferences (false и пусто
// — ключ удаляется, прочие ключи словаря остаются; пустой словарь уходит). title не пишется — это /Info
// (props.h). Не PDF — std::logic_error.
void writeInitialView(Document& doc, const InitialView& view);
// В каталоге есть /ViewerPreferences.
bool hasViewerPreferences(Document& doc);

// Base URL (/URI /Base каталога; эталон 3.13, view-45): основа относительных адресов ссылок /URI. Пусто —
// нет.
std::string baseUrl(Document& doc);
// Записать (операция журнала «Base URL»); пусто — удалить /Base (и пустой /URI). Не PDF — std::logic_error.
void setBaseUrl(Document& doc, const std::string& url);

}  // namespace ot
