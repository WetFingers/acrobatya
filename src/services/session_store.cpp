#include "services/session_store.h"

#include <sqlite3.h>

#include <mutex>

#include <spdlog/spdlog.h>

#include "services/state_db.h"

namespace ot::session {

namespace {

bool exec(sqlite3* db, const char* sql) {
    return sqlite3_exec(db, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

}  // namespace

bool save(StateDb& db, const SessionState& state) {
    if (!db.isOpen()) {
        return false;
    }
    std::lock_guard lock(db.mutex());
    sqlite3* h = db.handle();
    if (!exec(h, "BEGIN IMMEDIATE")) {
        return false;
    }
    bool ok = exec(h, "DELETE FROM session_tabs");
    sqlite3_stmt* st = nullptr;
    ok = ok &&
         sqlite3_prepare_v2(h, "INSERT INTO session_tabs(position, path, page, current) VALUES(?, ?, ?, ?)",
                            -1, &st, nullptr) == SQLITE_OK;
    for (int i = 0; ok && i < state.tabs.size(); ++i) {
        const QByteArray path = state.tabs[i].path.toUtf8();
        sqlite3_bind_int(st, 1, i);
        sqlite3_bind_text(st, 2, path.constData(), static_cast<int>(path.size()), SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 3, state.tabs[i].page);
        sqlite3_bind_int(st, 4, i == state.current ? 1 : 0);
        ok = sqlite3_step(st) == SQLITE_DONE;
        sqlite3_reset(st);
    }
    sqlite3_finalize(st);
    if (!ok || !exec(h, "COMMIT")) {
        spdlog::warn("session: cannot save open tabs: {}", sqlite3_errmsg(h));
        exec(h, "ROLLBACK");
        return false;
    }
    return true;
}

SessionState load(StateDb& db) {
    SessionState state;
    if (!db.isOpen()) {
        return state;
    }
    std::lock_guard lock(db.mutex());
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db.handle(), "SELECT path, page, current FROM session_tabs ORDER BY position", -1,
                           &st, nullptr) != SQLITE_OK) {
        return state;
    }
    while (sqlite3_step(st) == SQLITE_ROW) {
        SessionTab tab;
        tab.path = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st, 0)),
                                     sqlite3_column_bytes(st, 0));
        tab.page = sqlite3_column_int(st, 1);
        if (sqlite3_column_int(st, 2) != 0) {
            state.current = static_cast<int>(state.tabs.size());
        }
        state.tabs << tab;
    }
    sqlite3_finalize(st);
    return state;
}

bool save(const SessionState& state) { return save(StateDb::shared(), state); }

SessionState load() { return load(StateDb::shared()); }

}  // namespace ot::session
