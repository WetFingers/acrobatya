// unit.state_db.* — state.sqlite (эталон 4.7, план 1.4): одно подключение на процесс, путь — каталог данных
// (OTTISK_DATA_DIR в тестах), WAL; миграции по PRAGMA user_version (с нуля — до последней, повторное открытие
// ничего не меняет, данные остаются); испорченный файл откладывается в state.sqlite.bad и создаётся новый;
// запросы из нескольких потоков — под замком подключения.
#include <gtest/gtest.h>

#include <QDir>
#include <QString>

#include <sqlite3.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/text/index.h"
#include "core/util/features.h"
#include "services/app_info.h"
#include "services/state_db.h"
#include "support/test_pdf.h"

using ot::StateDb;

namespace fs = std::filesystem;

namespace {

int userVersion(sqlite3* db) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, nullptr);
    int v = -1;
    if (sqlite3_step(st) == SQLITE_ROW) {
        v = sqlite3_column_int(st, 0);
    }
    sqlite3_finalize(st);
    return v;
}

bool hasTable(sqlite3* db, const char* name) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE name = ?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name, -1, SQLITE_STATIC);
    const bool found = sqlite3_step(st) == SQLITE_ROW;
    sqlite3_finalize(st);
    return found;
}

QString qpath(const fs::path& p) { return QString::fromStdWString(p.wstring()); }

}  // namespace

TEST(state_db, creates_schema_with_migrations) {
    OT_FEATURE("view-19");
    ot::test::TempDir dir;
    const fs::path file = dir.path() / L"данные" / L"state.sqlite";
    {
        StateDb db(qpath(file));
        ASSERT_TRUE(db.isOpen()) << db.errorString().toStdString();
        EXPECT_EQ(db.schemaVersion(), StateDb::kSchemaVersion);
        const std::lock_guard lock(db.mutex());
        EXPECT_EQ(userVersion(db.handle()), StateDb::kSchemaVersion);
        EXPECT_TRUE(hasTable(db.handle(), "fts_files"));
        EXPECT_TRUE(hasTable(db.handle(), "fts_pages"));
        ot::TextIndex(db.handle()).upsert("C:/x.pdf", 5, {"привет"});
    }
    EXPECT_TRUE(fs::exists(file));
    // Повторное открытие: миграций нет, данные на месте.
    StateDb again(qpath(file));
    ASSERT_TRUE(again.isOpen());
    const std::lock_guard lock(again.mutex());
    EXPECT_TRUE(ot::TextIndex(again.handle()).isFresh("C:/x.pdf", 5));
    EXPECT_EQ(again.migrationsApplied(), 0);
}

TEST(state_db, migrates_old_version) {
    ot::test::TempDir dir;
    const fs::path file = dir.path() / L"state.sqlite";
    {
        sqlite3* raw = nullptr;
        ASSERT_EQ(sqlite3_open16(file.c_str(), &raw), SQLITE_OK);
        sqlite3_exec(raw, "CREATE TABLE old_stuff(x); PRAGMA user_version = 0;", nullptr, nullptr, nullptr);
        sqlite3_close(raw);
    }
    StateDb db(qpath(file));
    ASSERT_TRUE(db.isOpen());
    EXPECT_EQ(db.migrationsApplied(), StateDb::kSchemaVersion);
    const std::lock_guard lock(db.mutex());
    EXPECT_TRUE(hasTable(db.handle(), "old_stuff"));  // чужие таблицы не трогаются
    EXPECT_TRUE(hasTable(db.handle(), "fts_pages"));
}

TEST(state_db, damaged_file_is_set_aside) {
    ot::test::TempDir dir;
    const fs::path file = dir.path() / L"state.sqlite";
    {
        std::ofstream out(file, std::ios::binary);
        out << "this is not a sqlite database, just garbage bytes................................";
    }
    StateDb db(qpath(file));
    ASSERT_TRUE(db.isOpen()) << db.errorString().toStdString();
    EXPECT_EQ(db.schemaVersion(), StateDb::kSchemaVersion);
    EXPECT_TRUE(fs::exists(dir.path() / L"state.sqlite.bad"));
}

TEST(state_db, default_path_is_in_data_dir_and_shared) {
    // ctest задаёт OTTISK_DATA_DIR (<сборка>/test-data): данные человека не трогаются.
    EXPECT_EQ(StateDb::defaultPath(), QDir(ot::app::dataDir()).filePath(QStringLiteral("state.sqlite")));
    StateDb& a = StateDb::shared();
    StateDb& b = StateDb::shared();
    EXPECT_EQ(&a, &b);
    EXPECT_TRUE(a.isOpen());
}

TEST(state_db, threads_share_one_connection_under_lock) {
    ot::test::TempDir dir;
    StateDb db(qpath(dir.path() / L"state.sqlite"));
    ASSERT_TRUE(db.isOpen());
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&db, t] {
            for (int i = 0; i < 20; ++i) {
                const std::lock_guard lock(db.mutex());
                ot::TextIndex(db.handle())
                    .upsert("C:/t" + std::to_string(t) + "/" + std::to_string(i) + ".pdf", i, {"текст"});
            }
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    const std::lock_guard lock(db.mutex());
    EXPECT_EQ(ot::TextIndex(db.handle()).fileCount(), 80);
}
