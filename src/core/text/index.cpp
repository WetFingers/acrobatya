#include "core/text/index.h"

#include <sqlite3.h>

#include <stdexcept>
#include <utility>

namespace ot {

namespace {

[[noreturn]] void fail(sqlite3* db, const char* what) {
    throw std::runtime_error(std::string("index: ") + what + ": " + sqlite3_errmsg(db));
}

// Подготовленный запрос с привязками и финализацией в деструкторе.
class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK) {
            fail(db, sql);
        }
    }
    ~Stmt() { sqlite3_finalize(st_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& bind(int i, std::string_view text) {
        // Пустой string_view бывает с data() == nullptr — sqlite привязал бы NULL, а не ''.
        sqlite3_bind_text(st_, i, text.data() != nullptr ? text.data() : "", static_cast<int>(text.size()),
                          SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& bind(int i, std::int64_t value) {
        sqlite3_bind_int64(st_, i, value);
        return *this;
    }
    // true — есть строка; false — готово.
    bool step() {
        const int rc = sqlite3_step(st_);
        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc != SQLITE_DONE) {
            fail(db_, "step");
        }
        return false;
    }
    void run() {
        while (step()) {
        }
    }
    // Снова к началу (для следующей строки той же вставки).
    void reset() {
        sqlite3_reset(st_);
        sqlite3_clear_bindings(st_);
    }
    std::int64_t integer(int col) const { return sqlite3_column_int64(st_, col); }
    std::string text(int col) const {
        const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st_, col));
        return p != nullptr ? std::string(p, static_cast<std::size_t>(sqlite3_column_bytes(st_, col)))
                            : std::string();
    }

private:
    sqlite3* db_;
    sqlite3_stmt* st_ = nullptr;
};

void exec(sqlite3* db, const char* sql) {
    char* error = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error != nullptr ? error : "unknown";
        sqlite3_free(error);
        throw std::runtime_error(std::string("index: ") + message);
    }
}

// Транзакция: без commit — откат.
class Transaction {
public:
    explicit Transaction(sqlite3* db) : db_(db) { exec(db, "BEGIN IMMEDIATE"); }
    ~Transaction() {
        if (!done_) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit() {
        exec(db_, "COMMIT");
        done_ = true;
    }

private:
    sqlite3* db_;
    bool done_ = false;
};

std::int64_t firstRowid(std::int64_t id) { return id << TextIndex::kPageBits; }
std::int64_t lastRowid(std::int64_t id) {
    return (id << TextIndex::kPageBits) + ((1 << TextIndex::kPageBits) - 1);
}

// id файла (вставить, если нет) и сброс его страниц.
std::int64_t resetFile(sqlite3* db, std::string_view path, std::int64_t mtime, int pages, bool encrypted) {
    std::int64_t id = 0;
    {
        Stmt find(db, "SELECT id FROM fts_files WHERE path = ?1");
        if (find.bind(1, path).step()) {
            id = find.integer(0);
        }
    }
    if (id != 0) {
        Stmt(db, "DELETE FROM fts_pages WHERE rowid BETWEEN ?1 AND ?2")
            .bind(1, firstRowid(id))
            .bind(2, lastRowid(id))
            .run();
        Stmt(db, "UPDATE fts_files SET mtime = ?2, pages = ?3, encrypted = ?4 WHERE id = ?1")
            .bind(1, id)
            .bind(2, mtime)
            .bind(3, std::int64_t{pages})
            .bind(4, std::int64_t{encrypted ? 1 : 0})
            .run();
    } else {
        Stmt(db, "INSERT INTO fts_files(path, mtime, pages, encrypted) VALUES (?1, ?2, ?3, ?4)")
            .bind(1, path)
            .bind(2, mtime)
            .bind(3, std::int64_t{pages})
            .bind(4, std::int64_t{encrypted ? 1 : 0})
            .run();
        id = sqlite3_last_insert_rowid(db);
    }
    return id;
}

// Фраза FTS5 из запроса: кавычки убраны, пробелы схлопнуты; пусто — искать нечего.
std::string phraseOf(std::string_view query) {
    std::string words;
    bool space = false;
    for (const char c : query) {
        if (c == '"') {
            continue;
        }
        const bool blank = c == ' ' || c == '\t' || c == '\r' || c == '\n';
        if (blank) {
            space = !words.empty();
            continue;
        }
        if (space) {
            words += ' ';
            space = false;
        }
        words += c;
    }
    return words.empty() ? std::string() : '"' + words + '"';
}

constexpr const char* kFolderFilter = "(?2 = '' OR substr(f.path, 1, length(?2)) = ?2)";

}  // namespace

void TextIndex::createSchema(sqlite3* db) {
    exec(db,
         "CREATE TABLE IF NOT EXISTS fts_files("
         " id INTEGER PRIMARY KEY, path TEXT NOT NULL UNIQUE, mtime INTEGER NOT NULL,"
         " pages INTEGER NOT NULL DEFAULT 0, encrypted INTEGER NOT NULL DEFAULT 0);"
         "CREATE VIRTUAL TABLE IF NOT EXISTS fts_pages USING fts5("
         " path UNINDEXED, mtime UNINDEXED, page UNINDEXED, text,"
         " tokenize = 'unicode61 remove_diacritics 2');");
}

std::optional<IndexedFile> TextIndex::file(std::string_view path) const {
    Stmt st(db_, "SELECT path, mtime, pages, encrypted FROM fts_files WHERE path = ?1");
    if (!st.bind(1, path).step()) {
        return std::nullopt;
    }
    IndexedFile f;
    f.path = st.text(0);
    f.mtime = st.integer(1);
    f.pages = static_cast<int>(st.integer(2));
    f.encrypted = st.integer(3) != 0;
    return f;
}

bool TextIndex::isFresh(std::string_view path, std::int64_t mtime) const {
    const auto f = file(path);
    return f.has_value() && f->mtime == mtime;
}

void TextIndex::upsert(std::string_view path, std::int64_t mtime, const std::vector<std::string>& pages) {
    Transaction tx(db_);
    int stored = 0;
    for (const std::string& text : pages) {
        stored += text.find_first_not_of(" \t\r\n") != std::string::npos ? 1 : 0;
    }
    const std::int64_t id = resetFile(db_, path, mtime, stored, false);
    Stmt insert(db_, "INSERT INTO fts_pages(rowid, path, mtime, page, text) VALUES (?1, ?2, ?3, ?4, ?5)");
    const int maxPages = 1 << kPageBits;
    for (int page = 0; page < static_cast<int>(pages.size()) && page < maxPages; ++page) {
        const std::string& text = pages[page];
        if (text.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        insert.bind(1, firstRowid(id) + page)
            .bind(2, path)
            .bind(3, mtime)
            .bind(4, std::int64_t{page})
            .bind(5, text)
            .run();
        insert.reset();
    }
    tx.commit();
}

void TextIndex::markEncrypted(std::string_view path, std::int64_t mtime) {
    Transaction tx(db_);
    resetFile(db_, path, mtime, 0, true);
    tx.commit();
}

void TextIndex::remove(std::string_view path) {
    Transaction tx(db_);
    std::int64_t id = 0;
    {
        Stmt find(db_, "SELECT id FROM fts_files WHERE path = ?1");
        if (find.bind(1, path).step()) {
            id = find.integer(0);
        }
    }
    if (id != 0) {
        Stmt(db_, "DELETE FROM fts_pages WHERE rowid BETWEEN ?1 AND ?2")
            .bind(1, firstRowid(id))
            .bind(2, lastRowid(id))
            .run();
        Stmt(db_, "DELETE FROM fts_files WHERE id = ?1").bind(1, id).run();
    }
    tx.commit();
}

std::vector<IndexHit> TextIndex::query(std::string_view query, std::string_view folder, int limit) const {
    std::vector<IndexHit> hits;
    const std::string phrase = phraseOf(query);
    if (phrase.empty()) {
        return hits;
    }
    const std::string sql =
        std::string(
            "SELECT f.path, fts_pages.page, snippet(fts_pages, 3, char(2), char(3), '\xE2\x80\xA6', 12) "
            "FROM fts_pages JOIN fts_files f ON f.id = (fts_pages.rowid >> 20) "
            "WHERE fts_pages MATCH ?1 AND ") +
        kFolderFilter + " ORDER BY f.path, fts_pages.page LIMIT ?3";
    Stmt st(db_, sql.c_str());
    st.bind(1, phrase).bind(2, folder).bind(3, std::int64_t{limit});
    while (st.step()) {
        IndexHit h;
        h.path = st.text(0);
        h.page = static_cast<int>(st.integer(1));
        h.snippet = st.text(2);
        hits.push_back(std::move(h));
    }
    return hits;
}

std::vector<std::string> TextIndex::files(std::string_view folder) const {
    std::vector<std::string> out;
    Stmt st(
        db_,
        (std::string("SELECT f.path FROM fts_files f WHERE ") + kFolderFilter + " ORDER BY f.path").c_str());
    st.bind(2, folder);
    while (st.step()) {
        out.push_back(st.text(0));
    }
    return out;
}

std::vector<std::string> TextIndex::encryptedFiles(std::string_view folder) const {
    std::vector<std::string> out;
    Stmt st(db_, (std::string("SELECT f.path FROM fts_files f WHERE f.encrypted = 1 AND ") + kFolderFilter +
                  " ORDER BY f.path")
                     .c_str());
    st.bind(2, folder);
    while (st.step()) {
        out.push_back(st.text(0));
    }
    return out;
}

int TextIndex::fileCount() const {
    Stmt st(db_, "SELECT count(*) FROM fts_files");
    return st.step() ? static_cast<int>(st.integer(0)) : 0;
}

int TextIndex::pageCount() const {
    Stmt st(db_, "SELECT count(*) FROM fts_pages");
    return st.step() ? static_cast<int>(st.integer(0)) : 0;
}

}  // namespace ot
