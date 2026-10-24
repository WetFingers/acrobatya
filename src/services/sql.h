// SqlStatement — подготовленный запрос к state.sqlite (services/state_db.h): параметры по порядку (?1, ?2…),
// шаги — step(), закрывается сам. Вызывать под StateDb::mutex(). Ошибка подготовки — ok() == false и строка в
// журнал; step() тогда — SQLITE_ERROR.
#pragma once

#include <QByteArray>
#include <QString>

#include <sqlite3.h>

#include <spdlog/spdlog.h>

namespace ot {

class SqlStatement {
public:
    SqlStatement(sqlite3* db, const char* sql) {
        if (db == nullptr || sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK) {
            spdlog::warn("state.sqlite: {}", db != nullptr ? sqlite3_errmsg(db) : "not open");
            st_ = nullptr;
        }
    }
    ~SqlStatement() { sqlite3_finalize(st_); }
    SqlStatement(const SqlStatement&) = delete;
    SqlStatement& operator=(const SqlStatement&) = delete;

    bool ok() const { return st_ != nullptr; }
    SqlStatement& text(int i, const QString& value) {
        const QByteArray utf8 = value.toUtf8();
        sqlite3_bind_text(st_, i, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
        return *this;
    }
    SqlStatement& integer(int i, qint64 value) {
        sqlite3_bind_int64(st_, i, value);
        return *this;
    }
    SqlStatement& real(int i, double value) {
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
    sqlite3_stmt* st_ = nullptr;
};

}  // namespace ot
