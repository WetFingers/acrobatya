// Умолчания комментариев (план 5.1; эталон 3.4 «Ядро», 3.18 «Комментирование»; comment-08, comment-24): автор
// из «Identity» (имя, должность, организация; «всегда имя входа» — имя пользователя Windows, GetUserNameW),
// тема /Subj по типу (английские имена Acrobat — у Acrobat их видит Reader в любой локали), стиль нового
// комментария по инструменту — цвет, непрозрачность, толщина, иконка, шрифт. Значения без MuPDF; ui хранит
// свой стиль в settings.ini (commenting/…) и кладёт его поверх этих умолчаний.
#pragma once

#include <string>

#include "core/annot/annots.h"

namespace ot::annot {

// «Identity» Установок (Комментирование).
struct Identity {
    std::string name;
    std::string title;            // должность
    std::string organization;     // организация
    bool alwaysLoginName = true;  // «Всегда использовать имя входа как имя автора»
};

// Имя пользователя Windows (GetUserNameW) в UTF-8; не вышло — "User".
std::string loginName();
// Автор /T новых комментариев: имя входа (alwaysLoginName или пустое имя) или имя из Identity.
std::string authorName(const Identity& identity);

// /Subj по умолчанию: "Sticky Note", "Highlight", "Pencil"…
std::string defaultSubject(AnnotType type);

// Стиль нового комментария типа type (цвет, непрозрачность, толщина, иконка записки, шрифт FreeText) — как
// у Acrobat: записка жёлтая «Comment», выделение жёлтое, подчёркивание зелёное, зачёркивание красное,
// волнистая синяя, вставка синяя, карандаш синий 1 пт, фигуры красные 1 пт, текст 12 пт чёрный.
AnnotProps toolDefaults(AnnotType type);

// Иконки записки Acrobat (/Name у Text) по порядку меню.
inline constexpr const char* kNoteIcons[] = {"Comment",      "Key",       "Note",  "Help",
                                             "NewParagraph", "Paragraph", "Insert"};

}  // namespace ot::annot
