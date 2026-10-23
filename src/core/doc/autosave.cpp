#include "core/doc/autosave.h"

#include <windows.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <system_error>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/hash.h"
#include "core/util/paths.h"

namespace ot {

namespace fs = std::filesystem;

namespace {

constexpr int kIndexVersion = 1;

std::mutex& indexMutex() {
    static std::mutex m;
    return m;
}

std::int64_t unixSeconds(fs::file_time_type t) {
    const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(t);
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Записать файл целиком через временный в той же папке и MoveFileExW (запись индекса не рвётся посередине).
void writeAtomically(const fs::path& path, std::string_view bytes) {
    fs::path temp = path;
    temp += L".tmp";
    paths::writeFile(temp, bytes);
    if (MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        const DWORD code = GetLastError();
        std::error_code ec;
        fs::remove(temp, ec);
        throw std::runtime_error("cannot replace " + paths::toUtf8(path) + ": " +
                                 std::system_category().message(static_cast<int>(code)));
    }
}

nlohmann::json toJson(const AutosaveEntry& e) {
    return {{"path", paths::toUtf8(e.path)},
            {"key", e.key},
            {"sha256", e.sha256},
            {"mtime", e.mtime},
            {"size", e.size},
            {"copy", e.hasCopy},
            {"position", e.position},
            {"steps", e.steps},
            {"savedAt", e.savedAt}};
}

AutosaveEntry fromJson(const nlohmann::json& j) {
    AutosaveEntry e;
    e.path = paths::fromUtf8(j.at("path").get<std::string>());
    e.key = j.at("key").get<std::string>();
    e.sha256 = j.value("sha256", std::string());
    e.mtime = j.value("mtime", std::int64_t{0});
    e.size = j.value("size", std::uintmax_t{0});
    e.hasCopy = j.value("copy", false);
    e.position = j.value("position", 0);
    e.steps = j.value("steps", 0);
    e.savedAt = j.value("savedAt", std::int64_t{0});
    return e;
}

std::vector<AutosaveEntry> readIndex(const fs::path& dir) {
    std::vector<AutosaveEntry> entries;
    const fs::path index = dir / L"index.json";
    std::error_code ec;
    if (!fs::is_regular_file(index, ec)) {
        return entries;
    }
    try {
        const nlohmann::json j = nlohmann::json::parse(paths::readFile(index));
        for (const nlohmann::json& item : j.at("entries")) {
            entries.push_back(fromJson(item));
        }
    } catch (const std::exception& e) {
        spdlog::warn("autosave: {} is damaged ({}), starting a new index", paths::toUtf8(index), e.what());
        entries.clear();
    }
    return entries;
}

void writeIndex(const fs::path& dir, const std::vector<AutosaveEntry>& entries) {
    nlohmann::json list = nlohmann::json::array();
    for (const AutosaveEntry& e : entries) {
        list.push_back(toJson(e));
    }
    const nlohmann::json j = {{"version", kIndexVersion}, {"entries", list}};
    fs::create_directories(dir);
    writeAtomically(dir / L"index.json", j.dump(1));
}

// Журнал с позиции 0: у сделанного шага MuPDF хранит прежние значения, у отменённого — новые. Отменить все
// шаги, записать журнал, вернуть шаги — документ в памяти тот же.
std::string journalAtStart(Document& doc, int& position, int& steps) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    mu::call(ctx, [&] { position = pdf_undoredo_state(ctx, pdf, &steps); });
    int undone = 0;
    auto redoBack = [&]() noexcept {
        for (; undone > 0; --undone) {
            try {
                mu::call(ctx, [&] { pdf_redo(ctx, pdf); });
            } catch (const std::exception& e) {
                spdlog::error("autosave: redo after journal write failed: {}", e.what());
                return;
            }
        }
    };
    std::string bytes;
    try {
        for (; undone < position; ++undone) {
            mu::call(ctx, [&] { pdf_undo(ctx, pdf); });
        }
        mu::BufferHandle buf(ctx);
        mu::OutputHandle out(ctx);
        mu::call(ctx, [&] {
            buf.reset(fz_new_buffer(ctx, 16 * 1024));
            out.reset(fz_new_output_with_buffer(ctx, buf));
            pdf_write_journal(ctx, pdf, out);
            fz_close_output(ctx, out);
        });
        unsigned char* data = nullptr;
        const size_t len = fz_buffer_storage(ctx, buf, &data);
        bytes.assign(reinterpret_cast<const char*>(data), len);
    } catch (...) {
        redoBack();
        throw;
    }
    redoBack();
    return bytes;
}

}  // namespace

Autosave::Autosave(fs::path dir, std::uintmax_t maxCopyBytes)
    : dir_(std::move(dir)), maxCopyBytes_(maxCopyBytes) {}

std::string Autosave::keyFor(const fs::path& documentPath) {
    std::error_code ec;
    fs::path abs = fs::absolute(documentPath, ec);
    if (ec) {
        abs = documentPath;
    }
    std::wstring wide = abs.lexically_normal().wstring();
    std::replace(wide.begin(), wide.end(), L'/', L'\\');
    if (!wide.empty()) {
        CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
    }
    return hash::sha256Hex(paths::narrow(wide));
}

fs::path Autosave::journalPath(const std::string& key) const { return dir_ / paths::widen(key + ".journal"); }

fs::path Autosave::copyPath(const std::string& key) const { return dir_ / paths::widen(key + ".src.pdf"); }

AutosaveStatus Autosave::save(Document& doc, std::string* error) {
    try {
        if (!doc.journaled() || doc.path().empty() || doc.undoStack().inOperation()) {
            return AutosaveStatus::Skipped;
        }
        fz_context* ctx = doc.context().get();
        pdf_document* pdf = doc.pdf();
        int unsaved = 0;
        mu::call(ctx, [&] { unsaved = pdf_has_unsaved_changes(ctx, pdf); });
        if (unsaved == 0) {
            remove(doc.path());
            return AutosaveStatus::NothingToSave;
        }
        int position = 0;
        int steps = 0;
        const std::string journal = journalAtStart(doc, position, steps);
        const std::string key = keyFor(doc.path());
        // Файл, на котором стоит документ: свой, временный после неудачной замены (Saver) или копия исходника
        // (документ восстановлен из неё) — к нему и подходит журнал.
        const fs::path source = doc.filePath().empty() ? doc.path() : doc.filePath();

        const std::lock_guard lock(indexMutex());
        fs::create_directories(dir_);
        std::vector<AutosaveEntry> entries = readIndex(dir_);
        auto it = std::find_if(entries.begin(), entries.end(),
                               [&](const AutosaveEntry& e) { return e.key == key; });
        const std::uintmax_t size = fs::file_size(source);
        const std::int64_t mtime = unixSeconds(fs::last_write_time(source));
        AutosaveEntry entry;
        if (it != entries.end() && it->size == size && it->mtime == mtime) {
            entry = *it;  // тот же исходник — sha256 и копия уже есть
        } else {
            // Первое автосохранение этого исходника: sha256 и копия (≤ 500 МБ).
            entry.path = doc.path();
            entry.key = key;
            entry.size = size;
            entry.mtime = mtime;
            entry.sha256 = hash::sha256File(source);
            const fs::path copy = copyPath(key);
            std::error_code ec;
            if (fs::equivalent(source, copy, ec)) {
                entry.hasCopy = true;  // документ и так стоит на копии
            } else {
                fs::remove(copy, ec);
                entry.hasCopy = size <= maxCopyBytes_ && CopyFileW(source.c_str(), copy.c_str(), FALSE) != 0;
            }
        }
        writeAtomically(journalPath(key), journal);
        entry.position = position;
        entry.steps = steps;
        entry.savedAt = nowSeconds();
        if (it != entries.end()) {
            *it = entry;
        } else {
            entries.push_back(entry);
        }
        writeIndex(dir_, entries);
        spdlog::info("autosave: {} — {} of {} steps", paths::toUtf8(doc.path()), position, steps);
        return AutosaveStatus::Saved;
    } catch (const std::exception& e) {
        spdlog::error("autosave: {} failed: {}", paths::toUtf8(doc.path()), e.what());
        if (error != nullptr) {
            *error = e.what();
        }
        return AutosaveStatus::Failed;
    }
}

std::vector<AutosaveEntry> Autosave::entries() const {
    std::vector<AutosaveEntry> entries;
    {
        const std::lock_guard lock(indexMutex());
        entries = readIndex(dir_);
    }
    std::erase_if(entries, [&](const AutosaveEntry& e) {
        std::error_code ec;
        return !fs::is_regular_file(journalPath(e.key), ec);
    });
    std::sort(entries.begin(), entries.end(),
              [](const AutosaveEntry& a, const AutosaveEntry& b) { return a.savedAt > b.savedAt; });
    return entries;
}

std::optional<AutosaveEntry> Autosave::find(const fs::path& documentPath) const {
    const std::string key = keyFor(documentPath);
    for (const AutosaveEntry& e : entries()) {
        if (e.key == key) {
            return e;
        }
    }
    return std::nullopt;
}

void Autosave::remove(const fs::path& documentPath) {
    const std::string key = keyFor(documentPath);
    const std::lock_guard lock(indexMutex());
    std::error_code ec;
    if (!fs::exists(dir_, ec)) {
        return;
    }
    std::vector<AutosaveEntry> entries = readIndex(dir_);
    const auto removed = std::erase_if(entries, [&](const AutosaveEntry& e) { return e.key == key; });
    fs::remove(journalPath(key), ec);
    fs::remove(copyPath(key), ec);
    if (removed > 0) {
        try {
            writeIndex(dir_, entries);
        } catch (const std::exception& e) {
            spdlog::error("autosave: index update failed: {}", e.what());
        }
    }
}

RecoverResult Autosave::recover(const AutosaveEntry& entry, const std::string& password) const {
    RecoverResult r;
    const fs::path journal = journalPath(entry.key);
    std::error_code ec;
    if (!fs::is_regular_file(journal, ec)) {
        r.error = RecoverError::NoEntry;
        r.message = "no autosave journal for " + paths::toUtf8(entry.path);
        return r;
    }
    try {
        // Исходник — файл документа, если он тот же (sha256); иначе копия первого автосохранения.
        fs::path source;
        if (fs::is_regular_file(entry.path, ec) && hash::sha256File(entry.path) == entry.sha256) {
            source = entry.path;
        } else {
            const fs::path copy = copyPath(entry.key);
            if (entry.hasCopy && fs::is_regular_file(copy, ec) && hash::sha256File(copy) == entry.sha256) {
                source = copy;
                r.fromCopy = true;
            } else {
                r.error = RecoverError::SourceChanged;
                r.message = "source file changed since autosave and no copy: " + paths::toUtf8(entry.path);
                return r;
            }
        }
        r.document =
            Document::recoverFrom(source, paths::readFile(journal), entry.position, entry.path, password);
    } catch (const JournalMismatch& e) {
        r.error = RecoverError::JournalMismatch;
        r.message = e.what();
    } catch (const std::exception& e) {
        r.error = RecoverError::Failed;
        r.message = e.what();
    }
    return r;
}

}  // namespace ot
