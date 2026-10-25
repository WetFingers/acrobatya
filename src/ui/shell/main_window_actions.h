// Меню и действия окна (MainWindow::Actions): определение — в заголовке, а не в menus.cpp, — конструктор окна
// (main_window.cpp) уничтожает их при исключении, и unity-группа без menus.cpp не видела полный тип (5.4).
#pragma once

#include <QAction>
#include <QHash>
#include <QList>
#include <QMenu>
#include <QString>

#include "ui/shell/main_window.h"

namespace ot::ui {

struct MainWindow::Actions {
    QHash<QString, QMenu*> menus;  // "file", "view/zoom"…
    QHash<QString, QAction*> byId;
    QMenu* main = nullptr;   // «Меню ▾»
    QMenu* share = nullptr;  // «Поделиться ▾»
    QList<QAction*> tabActions;
};

}  // namespace ot::ui
