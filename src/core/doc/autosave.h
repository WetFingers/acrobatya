// Автосохранение и восстановление несохранённых правок (эталон 4.5, view-27). Каталог — %LOCALAPPDATA%\AcroBatya\
// autosave (тесты — OTTISK_DATA_DIR\autosave; каталог передаёт services):
//   <key>.journal   — журнал MuPDF документа (key = sha256 пути документа в нижнем регистре);
//   <key>.src.pdf   — копия исходного файла при первом автосохранении (≤ 500 МБ): восстановить, даже если
//   файл
//                     на диске потом изменился;
//   index.json      — записи {path, key, sha256 исходника, mtime, size, position, steps, savedAt, copy}.
// Журнал MuPDF хранит у сделанного шага прежние значения объектов, а новые живут в несохранённой секции xref.
// Поэтому запись: отменить все шаги (фрагменты держат новые значения) → pdf_write_journal с позицией 0 →
// вернуть все шаги; документ в памяти не меняется (pdf_write_snapshot сбросил бы инкрементальные секции и
// сломал undo). Восстановление (Document::recoverFrom): открыть исходник (сверка sha256; не совпал — копия;
// нет копии — отказ), сверить отпечаток xref (как MuPDF), переиграть шаги журнала и отменить лишние до
// сохранённой позиции — правки на месте, история undo/redo та же, файл «не сохранён» (pdf_read_journal
// MuPDF 1.28.5 журнал с правками не загружает — свой загрузчик). Документ без пути (новый, из памяти) не
// автосохраняется. Потокобезопасно: записи разных документов идут из разных потоков (index.json — под замком
// процесса).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ot {

class Document;

struct AutosaveEntry {
    std::filesystem::path path;  // документ
    std::string key;             // имя файлов записи
    std::string sha256;          // исходный файл при первом автосохранении
    std::int64_t mtime = 0;      // его время изменения, секунды Unix
    std::uintmax_t size = 0;
    bool hasCopy = false;  // <key>.src.pdf
    int position = 0;      // шагов сделано (остальные — «Вернуть»)
    int steps = 0;         // шагов в журнале
    std::int64_t savedAt = 0;
};

enum class AutosaveStatus {
    Saved,
    NothingToSave,  // несохранённых правок нет — запись удалена
    Skipped,        // не PDF, нет пути, идёт операция
    Failed,
};

enum class RecoverError {
    None,
    NoEntry,
    SourceChanged,  // исходник изменился, копии нет (или и она не та)
    JournalMismatch,
    Failed,
};

struct RecoverResult {
    std::unique_ptr<Document> document;
    RecoverError error = RecoverError::None;
    bool fromCopy = false;  // открыт из копии: файл на диске с тех пор менялся
    std::string message;
    bool ok() const noexcept { return error == RecoverError::None && document != nullptr; }
};

class Autosave {
public:
    static constexpr std::uintmax_t kMaxCopyBytes = 500ull * 1024 * 1024;

    explicit Autosave(std::filesystem::path dir, std::uintmax_t maxCopyBytes = kMaxCopyBytes);

    const std::filesystem::path& dir() const noexcept { return dir_; }
    static std::string keyFor(const std::filesystem::path& documentPath);

    // Записать журнал документа (поток документа, вне операции). Нет несохранённых правок — запись удаляется.
    AutosaveStatus save(Document& doc, std::string* error = nullptr);

    // Записи index.json (самые свежие — первыми); записи без журнала отброшены.
    std::vector<AutosaveEntry> entries() const;
    std::optional<AutosaveEntry> find(const std::filesystem::path& documentPath) const;
    // Удалить запись и её файлы («Сохранить» прошло или «Не сохранять»).
    void remove(const std::filesystem::path& documentPath);

    // Восстановить документ по записи (пароль — если файл зашифрован).
    RecoverResult recover(const AutosaveEntry& entry, const std::string& password = {}) const;

    std::filesystem::path journalPath(const std::string& key) const;
    std::filesystem::path copyPath(const std::string& key) const;

private:
    std::filesystem::path dir_;
    std::uintmax_t maxCopyBytes_;
};

}  // namespace ot
