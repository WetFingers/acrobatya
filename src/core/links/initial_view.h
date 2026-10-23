// Начальный вид документа (план 2.4 — чтение при открытии; 2.5 — запись в «Свойства → Начальный вид»; эталон
// 3.1 «Acrobat»: /ViewerPreferences и /OpenAction уважаются, наши настройки в файл не пишутся; view-47):
// /PageMode (какая панель открыта: закладки, миниатюры, слои, вложения, полный экран), /PageLayout,
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

}  // namespace ot
