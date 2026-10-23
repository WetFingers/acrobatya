// Сеанс — вкладки окна при закрытии (эталон 3.18 «Общие → при запуске», план 2.3; view-31): state.sqlite,
// таблица session_tabs (миграция 2). Окно пишет список при каждом закрытии; при запуске без файлов в
// командной строке открывает его снова, если включена настройка general/restoreSession (выкл. по умолчанию,
// как в Acrobat). Путь, страница вкладки и какая была текущей; несуществующие файлы при восстановлении
// пропускаются.
// Поток: любой (запросы — под StateDb::mutex()).
#pragma once

#include <QList>
#include <QString>

namespace ot {

class StateDb;

struct SessionTab {
    QString path;
    int page = 0;  // с нуля
    bool operator==(const SessionTab&) const = default;
};

struct SessionState {
    QList<SessionTab> tabs;
    int current = -1;  // индекс текущей вкладки; -1 — Главная
    bool operator==(const SessionState&) const = default;
};

namespace session {

// Ключ settings.ini «Открывать документы прошлого сеанса» (сессия 2.4b переносит его в settings_keys.h).
inline constexpr char kRestoreKey[] = "general/restoreSession";

bool save(StateDb& db, const SessionState& state);
SessionState load(StateDb& db);
bool save(const SessionState& state);  // StateDb::shared()
SessionState load();

}  // namespace session

}  // namespace ot
