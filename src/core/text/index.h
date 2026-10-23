// TextIndex — индекс поиска по папкам (эталон 3.2, 4.7; план 1.4): sqlite FTS5 в state.sqlite. Подключением
// не владеет: его открывает и мигрирует services/state_db.h (одно подключение на процесс; все вызовы — под
// его замком).
//
// Схема (миграция 1 — createSchema):
//  * fts_files(id, path UNIQUE, mtime, pages, encrypted) — ключ «путь + mtime»: свежий файл не извлекается;
//  * fts_pages(path, mtime, page, text) USING fts5 — страница — строка; токенизатор unicode61
//    remove_diacritics 2 (регистр и диакритика не важны: «оттиск» = «ОТТИСК» = «отти́ск»), без стемминга
//    («оттиск» не находит «оттиска»); rowid = id файла · 2^20 + страница (удаление файла — диапазон rowid).
// Пути — UTF-8 как их даёт вызывающий (FolderSearch — абсолютные с «/»); папка запроса — буквальный префикс.
// Зашифрованный файл без пароля — пометка encrypted без текста (пропускается, пока mtime тот же).
// Ошибка sqlite — std::runtime_error с текстом sqlite (английский, для журнала).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace ot {

struct IndexedFile {
    std::string path;
    std::int64_t mtime = 0;
    int pages = 0;  // страниц с текстом
    bool encrypted = false;
};

struct IndexHit {
    std::string path;
    int page = 0;
    std::string snippet;  // контекст: совпадения между \x02 и \x03, обрезка — «…»
};

class TextIndex {
public:
    static constexpr int kPageBits = 20;  // страниц в файле — до 2^20

    explicit TextIndex(sqlite3* db) noexcept : db_(db) {}

    // Таблицы индекса (CREATE … IF NOT EXISTS) — шаг миграции state.sqlite.
    static void createSchema(sqlite3* db);

    std::optional<IndexedFile> file(std::string_view path) const;
    // В индексе тот же mtime (с текстом или с пометкой «зашифрован») — извлекать заново не нужно.
    bool isFresh(std::string_view path, std::int64_t mtime) const;
    // Заменить страницы файла (pages[i] — текст страницы i в UTF-8; пустые не хранятся) — одной транзакцией.
    void upsert(std::string_view path, std::int64_t mtime, const std::vector<std::string>& pages);
    // Зашифрован, пароля нет: страниц нет, пометка до смены mtime.
    void markEncrypted(std::string_view path, std::int64_t mtime);
    void remove(std::string_view path);

    // Совпадения фразы query (слова подряд; кавычки игнорируются) в файлах с путём, начинающимся с folder
    // (пусто — везде): по пути (байты UTF-8), затем по странице; не больше limit.
    std::vector<IndexHit> query(std::string_view query, std::string_view folder = {},
                                int limit = 10000) const;
    std::vector<std::string> files(std::string_view folder = {}) const;
    std::vector<std::string> encryptedFiles(std::string_view folder = {}) const;
    int fileCount() const;
    int pageCount() const;

private:
    sqlite3* db_;
};

}  // namespace ot
