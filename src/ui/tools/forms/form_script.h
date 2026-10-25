// Действия --script форм (план 6.1; рецепт «Новое действие --script»): form.setValue{name, value} — значение
// поля по полному имени (текст и список — строка, список с мультивыбором — массив строк, флажок — true/false
// или имя состояния); отказ (проверка сценарием, только для чтения, нет варианта) — ошибка шага.
// Регистрирует registerBaseActions (ui/app/script_actions.cpp).
#pragma once

namespace ot {
class ScriptRunner;
}

namespace ot::ui {

class MainWindow;

void registerFormScriptActions(ScriptRunner& runner, MainWindow& window);

}  // namespace ot::ui
