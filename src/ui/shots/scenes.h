// Сцены для снимков без экрана (эталон 6.10): --shots <имя|all> → <out>/<имя>.png.
// Новая сцена — строка в таблице scenes() в scenes.cpp; закон вида — эталон 6, не снимок.
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

namespace ot::ui {

QStringList sceneNames();
// Снять сцены (names может содержать "all"); код возврата для main: 0 — все сняты.
int runShots(const QStringList& names, const QString& outDir, QSize size, const QString& theme);

// Тема этапа 0: Fusion + светлая или тёмная палитра (токены — сессия 2.1).
void applyBaseTheme(const QString& theme);

}  // namespace ot::ui
