// Подписи шагов истории для меню «Отменить: …» / «Вернуть: …» (эталон 4.3: «Отменить: записка»; план 5.1):
// ядро и службы называют шаги английскими кодами («Add note», «Erase ink»), подпись по-русски — здесь.
// Незнакомый код — пусто (меню — просто «Отменить»).
#pragma once

#include <QString>

namespace ot::ui {

QString undoStepTitle(const QString& code);

}  // namespace ot::ui
