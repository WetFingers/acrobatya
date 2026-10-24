// Действия --script редактора текста (план 3.4, п. 4): edit.insertText, edit.replaceAll — регистрирует
// registerBaseActions (ui/app/script_actions.cpp) последней строкой.
#pragma once

namespace ot {
class ScriptRunner;
}

namespace ot::ui {

class MainWindow;

void registerEditScriptActions(ScriptRunner& runner, MainWindow& window);

}  // namespace ot::ui
