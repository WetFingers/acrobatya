// Действия --script «Заполнить и подписать» (план 5.3, п. 4; ими пользуются tests/scripts/fillsign.json —
// сценарий 3 эталона — и рубеж Р3): fillsign.placeText, fillsign.placeMark, fillsign.placeSignature,
// fillsign.flatCopy — регистрирует registerBaseActions (ui/app/script_actions.cpp).
#pragma once

namespace ot {
class ScriptRunner;
}

namespace ot::ui {

class MainWindow;

void registerFillSignScriptActions(ScriptRunner& runner, MainWindow& window);

}  // namespace ot::ui
