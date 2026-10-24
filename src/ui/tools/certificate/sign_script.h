// Действия --script подписей (план 4.6, п. 4; эталон 2 — сценарии 4 и 5; рецепт «Новое действие --script»;
// ими пользуется рубеж Р2): sign.sign, sign.certify, sign.timestamp, sign.verifyAll, sign.trust —
// регистрирует registerBaseActions (ui/app/script_actions.cpp).
#pragma once

namespace ot {
class ScriptRunner;
}

namespace ot::ui {

class MainWindow;

void registerSignScriptActions(ScriptRunner& runner, MainWindow& window);

}  // namespace ot::ui
