// Действия --script комментариев (план 5.1, п. 1 и 5; ими пользуются рубеж Р3 и
// tests/scripts/annots_forms.json): annot.create — регистрирует registerBaseActions
// (ui/app/script_actions.cpp).
#pragma once

namespace ot {
class ScriptRunner;
}

namespace ot::ui {

class MainWindow;

void registerCommentScriptActions(ScriptRunner& runner, MainWindow& window);

}  // namespace ot::ui
