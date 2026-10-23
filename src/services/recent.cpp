#include "services/recent.h"

#include <sqlite3.h>

#include <algorithm>
#include <mutex>

#include <spdlog/spdlog.h>

#include "services/document_registry.h"
#include "services/state_db.h"

namespace ot {

namespace {

// Подготовленный запрос: параметры по порядку (?1, ?2…), шаги — step(); закрывается сам.
class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK) {
            spdlog::warn("recent: {}", sqlite3_errmsg(db));
            st_ = nullptr;
        }
    }
    ~Statement() { sqlite3_finalize(st_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    bool ok() const { return st_ != nullptr; }
    Statement& text(int i, const QString& value) {
        const QByteArray utf8 = value.toUtf8();
        sqlite3_bind_text(st_, i, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Statement& integer(int i, qint64 value) {
        sqlite3_bind_int64(st_, i, value);
        return *this;
    }
    Statement& real(int i, double value) {
        sqlite3_bind_double(st_, i, value);
        return *this;
    }
    int step() { return st_ != nullptr ? sqlite3_step(st_) : SQLITE_ERROR; }
    bool run() { return step() == SQLITE_DONE; }
    QString columnText(int i) const {
        return QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(st_, i)),
                                 sqlite3_column_bytes(st_, i));
    }
    qint64 columnInt(int i) const { return sqlite3_column_int64(st_, i); }
    double columnReal(int i) const { return sqlite3_column_double(st_, i); }

private:
    sqlite3* db_;
    sqlite3_stmt* st_ = nullptr;
};

constexpr char kColumns[] = "path, opened_at, pinned, favorite, page_count, last_page, zoom, layout";

RecentEntry readEntry(const Statement& st) {
    RecentEntry e;
    e.path = st.columnText(0);
    e.openedAt = QDateTime::fromMSecsSinceEpoch(st.columnInt(1));
    e.pinned = st.columnInt(2) != 0;
    e.favorite = st.columnInt(3) != 0;
    e.pageCount = static_cast<int>(st.columnInt(4));
    e.view.page = static_cast<int>(st.columnInt(5));
    e.view.zoomPercent = st.columnReal(6);
    e.view.layout = st.columnText(7);
    return e;
}

}  // namespace

RecentFiles& RecentFiles::shared() {
    static RecentFiles recent(StateDb::shared());
    return recent;
}

RecentFiles::RecentFiles(StateDb& db, QObject* parent) : QObject(parent), db_(db) {}

void RecentFiles::touch(const QString& path, int pageCount) {
    if (!db_.isOpen() || path.isEmpty()) {
        return;
    }
    {
        std::lock_guard lock(db_.mutex());
        // Время открытия строго растёт: два файла в одну миллисекунду не меняются местами.
        qint64 last = 0;
        {
            Statement st(db_.handle(), "SELECT MAX(opened_at) FROM recent");
            if (st.step() == SQLITE_ROW) {
                last = st.columnInt(0);
            }
        }
        const qint64 now = std::max(QDateTime::currentMSecsSinceEpoch(), last + 1);
        Statement st(
            db_.handle(),
            "INSERT INTO recent(key, path, opened_at, page_count, in_recent) VALUES(?1, ?2, ?3, ?4, 1) "
            "ON CONFLICT(key) DO UPDATE SET path = excluded.path, opened_at = excluded.opened_at, "
            "in_recent = 1, page_count = CASE WHEN excluded.page_count > 0 THEN excluded.page_count "
            "ELSE page_count END");
        st.text(1, DocumentRegistry::pathKey(path)).text(2, path).integer(3, now).integer(4, pageCount).run();
        trim();
    }
    emit changed();
}

// Не больше kLimit незакреплённых в недавних; строка, которой нет ни в недавних, ни в избранном, удаляется.
void RecentFiles::trim() {
    Statement drop(
        db_.handle(),
        "UPDATE recent SET in_recent = 0 WHERE in_recent = 1 AND pinned = 0 AND key NOT IN "
        "(SELECT key FROM recent WHERE in_recent = 1 AND pinned = 0 ORDER BY opened_at DESC LIMIT ?1)");
    drop.integer(1, kLimit).run();
    Statement gone(db_.handle(), "DELETE FROM recent WHERE in_recent = 0 AND favorite = 0");
    gone.run();
}

QList<RecentEntry> RecentFiles::query(const char* where, const char* order) const {
    QList<RecentEntry> out;
    if (!db_.isOpen()) {
        return out;
    }
    std::lock_guard lock(db_.mutex());
    const QByteArray sql =
        QByteArrayLiteral("SELECT ") + kColumns + " FROM recent WHERE " + where + " ORDER BY " + order;
    Statement st(db_.handle(), sql.constData());
    while (st.step() == SQLITE_ROW) {
        out << readEntry(st);
    }
    return out;
}

QList<RecentEntry> RecentFiles::recent() const {
    QList<RecentEntry> all = query("in_recent = 1", "pinned DESC, opened_at DESC");
    // Незакреплённых — не больше kLimit (закреплённые сверх лимита остаются).
    int unpinned = 0;
    QList<RecentEntry> out;
    for (const RecentEntry& e : all) {
        if (e.pinned || ++unpinned <= kLimit) {
            out << e;
        }
    }
    return out;
}

QList<RecentEntry> RecentFiles::favorites() const { return query("favorite = 1", "favorite_at DESC"); }

std::optional<RecentEntry> RecentFiles::find(const QString& path) const {
    if (!db_.isOpen()) {
        return std::nullopt;
    }
    std::lock_guard lock(db_.mutex());
    const QByteArray sql = QByteArrayLiteral("SELECT ") + kColumns + " FROM recent WHERE key = ?1";
    Statement st(db_.handle(), sql.constData());
    st.text(1, DocumentRegistry::pathKey(path));
    if (st.step() == SQLITE_ROW) {
        return readEntry(st);
    }
    return std::nullopt;
}

bool RecentFiles::exec(const char* sql, const QString& path, std::initializer_list<qint64> numbers) const {
    if (!db_.isOpen()) {
        return false;
    }
    std::lock_guard lock(db_.mutex());
    Statement st(db_.handle(), sql);
    st.text(1, DocumentRegistry::pathKey(path));
    int i = 2;
    for (const qint64 n : numbers) {
        st.integer(i++, n);
    }
    return st.run();
}

void RecentFiles::setPinned(const QString& path, bool pinned) {
    if (exec("UPDATE recent SET pinned = ?2 WHERE key = ?1", path, {pinned ? 1 : 0})) {
        emit changed();
    }
}

void RecentFiles::setFavorite(const QString& path, bool favorite) {
    if (!db_.isOpen()) {
        return;
    }
    {
        std::lock_guard lock(db_.mutex());
        // Избранным можно сделать и файл, которого нет в недавних (Главная → «Файлы»).
        Statement st(
            db_.handle(),
            "INSERT INTO recent(key, path, opened_at, favorite, favorite_at, in_recent) "
            "VALUES(?1, ?2, 0, ?3, ?4, 0) ON CONFLICT(key) DO UPDATE SET favorite = excluded.favorite, "
            "favorite_at = excluded.favorite_at");
        st.text(1, DocumentRegistry::pathKey(path))
            .text(2, path)
            .integer(3, favorite ? 1 : 0)
            .integer(4, QDateTime::currentMSecsSinceEpoch())
            .run();
        trim();
    }
    emit changed();
}

void RecentFiles::remove(const QString& path) {
    if (!db_.isOpen()) {
        return;
    }
    {
        std::lock_guard lock(db_.mutex());
        Statement st(db_.handle(), "UPDATE recent SET in_recent = 0, pinned = 0 WHERE key = ?1");
        st.text(1, DocumentRegistry::pathKey(path)).run();
        trim();
    }
    emit changed();
}

void RecentFiles::clear() {
    if (!db_.isOpen()) {
        return;
    }
    {
        std::lock_guard lock(db_.mutex());
        Statement st(db_.handle(), "DELETE FROM recent");
        st.run();
    }
    emit changed();
}

void RecentFiles::saveView(const QString& path, const LastView& view) {
    if (!db_.isOpen()) {
        return;
    }
    std::lock_guard lock(db_.mutex());
    Statement st(db_.handle(), "UPDATE recent SET last_page = ?2, zoom = ?3, layout = ?4 WHERE key = ?1");
    st.text(1, DocumentRegistry::pathKey(path))
        .integer(2, view.page)
        .real(3, view.zoomPercent)
        .text(4, view.layout);
    st.run();
}

std::optional<LastView> RecentFiles::lastView(const QString& path) const {
    const std::optional<RecentEntry> e = find(path);
    if (!e || (e->view.page == 0 && e->view.zoomPercent <= 0 && e->view.layout.isEmpty())) {
        return std::nullopt;
    }
    return e->view;
}

}  // namespace ot
