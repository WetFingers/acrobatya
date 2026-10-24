// StateDb — state.sqlite (эталон 4.7, план 1.4): одно подключение sqlite на процесс (shared(); путь —
// app::dataDir()/state.sqlite, в тестах — OTTISK_DATA_DIR), журнал WAL, ожидание занятой базы 5 с. Схема — по
// миграциям: PRAGMA user_version — номер последней применённой, каждая следующая — в своей транзакции; чужие
// таблицы не трогаются. Испорченный файл (не база sqlite) откладывается в state.sqlite.bad и создаётся новый
// — там только восстановимое состояние (индекс, недавние, виды), программа не должна из-за него падать.
//
// Миграции: 1 — индекс поиска по папкам (core/text/index.h: fts_files, fts_pages), 2 — вкладки прошлого
// сеанса (session_store.h: session_tabs), 3 — недавние, избранное и последние виды (recent.h: recent),
// 4 — доверенные файлы и сайты ссылок (protected_view.h: trusted_files, link_policy.h: link_hosts), 5 —
// удостоверения подписи (core/sign/identity.h: identities).
// Запросы — SqlStatement (services/sql.h). Следующие сессии добавляют шаги в конец списка (state_db.cpp) и
// поднимают kSchemaVersion: прочитанные комментарии, подозрения OCR…
//
// Поток: handle() — только под замком mutex() (запросы из пула поиска по папкам, GUI и потоков документов
// идут по очереди).
#pragma once

#include <QString>

#include <mutex>

struct sqlite3;

namespace ot {

class StateDb {
public:
    static constexpr int kSchemaVersion = 6;

    // <каталог данных>/state.sqlite.
    static QString defaultPath();
    // Подключение процесса (открывается при первом вызове по defaultPath()).
    static StateDb& shared();

    // Открыть (создать папку и файл) и довести схему до kSchemaVersion. Ошибка — isOpen() == false и
    // errorString().
    explicit StateDb(const QString& path);
    ~StateDb();
    StateDb(const StateDb&) = delete;
    StateDb& operator=(const StateDb&) = delete;

    bool isOpen() const noexcept { return db_ != nullptr; }
    QString errorString() const { return error_; }
    QString path() const { return path_; }
    sqlite3* handle() const noexcept { return db_; }
    std::mutex& mutex() const noexcept { return mutex_; }
    int schemaVersion() const noexcept { return version_; }
    // Миграций применено при этом открытии.
    int migrationsApplied() const noexcept { return applied_; }

private:
    bool openFile();
    bool migrate();
    void closeHandle() noexcept;

    QString path_;
    QString error_;
    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
    int version_ = 0;
    int applied_ = 0;
};

}  // namespace ot
