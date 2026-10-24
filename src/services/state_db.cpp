#include "services/state_db.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sqlite3.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/sign/identity.h"
#include "core/text/index.h"
#include "services/app_info.h"

namespace ot {

namespace {

// Шаг миграции: ошибка — исключение (откат транзакции шага).
void exec(sqlite3* db, const char* sql) {
    char* message = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &message) != SQLITE_OK) {
        const std::string text = message != nullptr ? message : sqlite3_errmsg(db);
        sqlite3_free(message);
        throw std::runtime_error("state.sqlite: " + text);
    }
}

// Шаги схемы: шаг i доводит user_version с i до i + 1.
const std::vector<std::function<void(sqlite3*)>>& migrations() {
    static const std::vector<std::function<void(sqlite3*)>> steps = {
        [](sqlite3* db) { TextIndex::createSchema(db); },  // 1: индекс поиска по папкам (1.4)
        // 2: вкладки прошлого сеанса (2.3, services/session_store.h)
        [](sqlite3* db) {
            exec(db,
                 "CREATE TABLE IF NOT EXISTS session_tabs(position INTEGER PRIMARY KEY, path TEXT NOT NULL, "
                 "page INTEGER NOT NULL DEFAULT 0, current INTEGER NOT NULL DEFAULT 0)");
        },
        // 3: недавние файлы, избранное и последние виды (2.3, services/recent.h)
        [](sqlite3* db) {
            exec(db,
                 "CREATE TABLE IF NOT EXISTS recent(key TEXT PRIMARY KEY, path TEXT NOT NULL, "
                 "opened_at INTEGER NOT NULL DEFAULT 0, in_recent INTEGER NOT NULL DEFAULT 1, "
                 "pinned INTEGER NOT NULL DEFAULT 0, favorite INTEGER NOT NULL DEFAULT 0, "
                 "favorite_at INTEGER NOT NULL DEFAULT 0, page_count INTEGER NOT NULL DEFAULT 0, "
                 "last_page INTEGER NOT NULL DEFAULT 0, zoom REAL NOT NULL DEFAULT 0, layout TEXT NOT NULL "
                 "DEFAULT '')");
            exec(db, "CREATE INDEX IF NOT EXISTS recent_opened ON recent(in_recent, opened_at)");
        },
        // 4: доверенные файлы (2.5b: «Включить все функции» защищённого просмотра — scope 'protectedView';
        // JavaScript «для этого файла» 6.1 — свой scope) и сайты ссылок Диспетчера доверия (services/
        // link_policy.h: rule 1 — разрешить, 2 — запретить)
        [](sqlite3* db) {
            exec(db,
                 "CREATE TABLE IF NOT EXISTS trusted_files(key TEXT NOT NULL, scope TEXT NOT NULL, "
                 "path TEXT NOT NULL, trusted_at INTEGER NOT NULL DEFAULT 0, PRIMARY KEY(key, scope))");
            exec(db,
                 "CREATE TABLE IF NOT EXISTS link_hosts(host TEXT PRIMARY KEY, rule INTEGER NOT NULL, "
                 "decided_at INTEGER NOT NULL DEFAULT 0)");
        },
        // 5: удостоверения подписи (4.2, core/sign/identity.h: identities — .pfx в ids\ и сертификаты
        // хранилища Windows, по умолчанию, тайм-аут пароля, пароль DPAPI)
        [](sqlite3* db) { sign::IdentityStore::createSchema(db); },
    };
    return steps;
}

bool execSql(sqlite3* db, const char* sql, QString* error = nullptr) {
    char* message = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
    if (rc != SQLITE_OK && error != nullptr) {
        *error = QString::fromUtf8(message != nullptr ? message : sqlite3_errstr(rc));
    }
    sqlite3_free(message);
    return rc == SQLITE_OK;
}

int readUserVersion(sqlite3* db, int* rc) {
    sqlite3_stmt* st = nullptr;
    int version = 0;
    *rc = sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, nullptr);
    if (*rc == SQLITE_OK) {
        *rc = sqlite3_step(st);
        if (*rc == SQLITE_ROW) {
            version = sqlite3_column_int(st, 0);
            *rc = SQLITE_OK;
        }
    }
    sqlite3_finalize(st);
    return version;
}

}  // namespace

QString StateDb::defaultPath() { return QDir(app::dataDir()).filePath(QStringLiteral("state.sqlite")); }

StateDb& StateDb::shared() {
    static StateDb db(defaultPath());
    return db;
}

StateDb::StateDb(const QString& path) : path_(QFileInfo(path).absoluteFilePath()) {
    QDir().mkpath(QFileInfo(path_).absolutePath());
    if (!openFile()) {
        return;
    }
    if (!migrate()) {
        closeHandle();
    }
}

StateDb::~StateDb() { closeHandle(); }

void StateDb::closeHandle() noexcept {
    if (db_ != nullptr) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool StateDb::openFile() {
    for (int attempt = 0; attempt < 2; ++attempt) {
        const QByteArray file = path_.toUtf8();
        const int rc =
            sqlite3_open_v2(file.constData(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
        if (rc != SQLITE_OK) {
            error_ = QString::fromUtf8(db_ != nullptr ? sqlite3_errmsg(db_) : sqlite3_errstr(rc));
            closeHandle();
            return false;
        }
        sqlite3_busy_timeout(db_, 5000);
        int check = SQLITE_OK;
        version_ = readUserVersion(db_, &check);  // первое чтение заголовка: чужой файл — SQLITE_NOTADB
        if (check == SQLITE_OK) {
            if (!execSql(db_, "PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;", &error_)) {
                closeHandle();
                return false;
            }
            return true;
        }
        error_ = QString::fromUtf8(sqlite3_errstr(check));
        closeHandle();
        const bool damaged = check == SQLITE_NOTADB || check == SQLITE_CORRUPT;
        if (!damaged || attempt > 0) {
            return false;
        }
        // Не база sqlite: отложить и начать заново (там только восстановимое состояние).
        spdlog::warn("state.sqlite is damaged ({}), moved to state.sqlite.bad", sqlite3_errstr(check));
        const QString bad = path_ + QStringLiteral(".bad");
        QFile::remove(bad);
        QFile::rename(path_, bad);
        QFile::remove(path_ + QStringLiteral("-wal"));
        QFile::remove(path_ + QStringLiteral("-shm"));
        error_.clear();
    }
    return false;
}

bool StateDb::migrate() {
    const auto& steps = migrations();
    static_assert(kSchemaVersion >= 1);
    if (static_cast<int>(steps.size()) != kSchemaVersion) {
        error_ = QStringLiteral("state.sqlite: migration list does not match kSchemaVersion");
        return false;
    }
    while (version_ < kSchemaVersion) {
        if (!execSql(db_, "BEGIN IMMEDIATE", &error_)) {
            return false;
        }
        try {
            steps[version_](db_);
        } catch (const std::exception& e) {
            error_ = QString::fromUtf8(e.what());
            execSql(db_, "ROLLBACK");
            return false;
        }
        const QByteArray bump =
            QByteArrayLiteral("PRAGMA user_version = ") + QByteArray::number(version_ + 1);
        if (!execSql(db_, bump.constData(), &error_) || !execSql(db_, "COMMIT", &error_)) {
            execSql(db_, "ROLLBACK");
            return false;
        }
        ++version_;
        ++applied_;
        spdlog::info("state.sqlite: migrated to version {}", version_);
    }
    return true;
}

}  // namespace ot
