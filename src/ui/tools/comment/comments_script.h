// Действия --script ответов, статусов, обмена и сводки (план 5.4, п. 4; сценарий 2 эталона — tests/scripts/
// comments.json; ими пользуется рубеж Р3):
//   comment.reply{nm, text, author} — ответ на комментарий (корень или ответ) с /NM nm;
//   comment.setStatus{nm, state, author} — статус: None/Accepted/Rejected/Cancelled/Completed (Review),
//     Marked/Unmarked («галочка»);
//   comments.export{path, format, nm} — все комментарии (или ветка nm) в FDF/XFDF;
//   comments.import{path, expect} — импорт без вопроса (сценарий сам подтверждает); чужой документ — ошибка
//     шага; expect{created, updated, skipped} — сверка итога;
//   comments.summary{path, layout, fontSize} — сводка в файл PDF (dpc|comments|separate).
// Регистрирует registerBaseActions (ui/app/script_actions.cpp).
#pragma once

namespace ot {
class ScriptRunner;
}

namespace ot::ui {

class MainWindow;

void registerCommentsScriptActions(ScriptRunner& runner, MainWindow& window);

}  // namespace ot::ui
