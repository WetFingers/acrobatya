// engine.edit_stability.* — метрика стабильности редактора на корпусе text-edit (план 3.1, п. 5; эталон 3.9
// «Стабильность», 11 риск 1): у каждого файла тега страницы (до kMaxPages) проходят «пустую правку»
// (core/edit/empty_edit.h: каждый редактируемый абзац пересобран без изменений и записан, fz_stext до/после
// равен по тексту и рамкам ±0,5 pt, рендер в порогах imgcmp, undo — потоки байт в байт). Файл стабилен, если
// стабильны все его страницы с правкой. Доля стабильных среди файлов с редактируемым текстом — не меньше 90 %
// (иначе сужается объём редактора, не план); число, причины «только чтение» и нестабильные файлы — в
// build/edit-stability.json (журнал — docs/progress.md). Файлы идут в нескольких потоках: у каждого документа
// свой клон контекста MuPDF. Нет корпуса — SKIP.
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/edit/empty_edit.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/probe.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr int kMaxPages = 10;
constexpr unsigned kThreads = 4;

struct FileResult {
    std::string rel;
    bool opened = false;
    int pages = 0;
    int editedPages = 0;
    int blocks = 0;
    int editable = 0;
    int ops = 0;
    bool stable = true;
    std::string problem;  // первая страница с разницей
    std::map<std::string, int> readOnly;
    std::string error;  // ошибка нашего кода (не битый файл)
};

const json& textEditRecords() {
    static const json records = [] {
        const fs::path p = ot::test::corpusRoot() / L"index.json";
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            return json::array();
        }
        try {
            const json index = json::parse(ot::paths::readFile(p));
            return index["tags"].value("text-edit", json::array());
        } catch (const std::exception&) {
            return json::array();
        }
    }();
    return records;
}

FileResult runFile(const json& rec) {
    FileResult r;
    r.rel = rec.value("rel", rec.value("path", std::string()));
    if (rec.value("expect", std::string("open")) != "open") {
        return r;
    }
    ot::OpenResult opened = ot::Document::open(ot::test::corpusFile(rec.value("path", std::string()), r.rel));
    if (!opened.ok || !opened.document->isPdf()) {
        return r;
    }
    r.opened = true;
    ot::Document& doc = *opened.document;
    const int n = std::min(doc.pageCount(), kMaxPages);
    for (int page = 0; page < n; ++page) {
        try {
            const ot::edit::EmptyEditReport e = ot::edit::emptyEditPage(doc, page);
            ++r.pages;
            r.blocks += e.blocks;
            r.editable += e.editable;
            r.ops += e.ops;
            r.editedPages += e.streams > 0 ? 1 : 0;
            for (const auto& [code, count] : e.readOnly) {
                r.readOnly[code] += count;
            }
            if (!e.stable() && r.stable) {
                r.stable = false;
                r.problem = "стр. " + std::to_string(page + 1) + ": " + e.problem;
            }
        } catch (const ot::MuError& err) {
            if (!ot::test::isBrokenFileCode(err.code())) {
                r.error = "стр. " + std::to_string(page + 1) + ": " + err.what();
            }
        } catch (const std::exception& err) {
            r.error = "стр. " + std::to_string(page + 1) + ": " + err.what();
        }
    }
    return r;
}

}  // namespace

TEST(edit_stability, empty_edit_on_text_edit_corpus) {
    const json& recs = textEditRecords();
    if (recs.empty()) {
        GTEST_SKIP() << "нет тега text-edit в индексе корпуса — корпус качает этап 0 (tools/fetch_corpus.py)";
    }
    const auto started = std::chrono::steady_clock::now();
    const ot::test::QuietLog quiet;
    std::vector<FileResult> results(recs.size());
    std::atomic<size_t> next{0};
    std::vector<std::thread> workers;
    for (unsigned t = 0; t < kThreads; ++t) {
        workers.emplace_back([&] {
            for (size_t i = next++; i < recs.size(); i = next++) {
                results[i] = runFile(recs[i]);
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }

    int opened = 0, withText = 0, stable = 0, pages = 0, editedPages = 0, blocks = 0, editable = 0, ops = 0;
    std::map<std::string, int> readOnly;
    int allReadOnly = 0;
    json unstable = json::array();
    json errors = json::array();
    for (const FileResult& r : results) {
        if (!r.error.empty()) {
            errors.push_back({{"file", r.rel}, {"error", r.error}});
        }
        if (!r.opened) {
            continue;
        }
        ++opened;
        pages += r.pages;
        editedPages += r.editedPages;
        blocks += r.blocks;
        editable += r.editable;
        ops += r.ops;
        for (const auto& [code, count] : r.readOnly) {
            readOnly[code] += count;
        }
        if (r.editable == 0) {
            allReadOnly += r.blocks > 0 ? 1 : 0;
            continue;
        }
        ++withText;
        if (r.stable) {
            ++stable;
        } else {
            unstable.push_back({{"file", r.rel}, {"problem", r.problem}});
        }
    }
    const double share = withText > 0 ? static_cast<double>(stable) / withText : 0.0;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    json report = {
        {"tag", "text-edit"},
        {"files", recs.size()},
        {"opened", opened},
        {"filesWithEditableText", withText},
        {"stableFiles", stable},
        {"stableShare", share},
        {"filesAllReadOnly", allReadOnly},
        {"pages", pages},
        {"editedPages", editedPages},
        {"blocks", blocks},
        {"editableBlocks", editable},
        {"rebuiltOperators", ops},
        {"readOnlyBlocks", readOnly},
        {"maxPagesPerFile", kMaxPages},
        {"seconds", seconds},
        {"unstable", unstable},
        {"errors", errors},
    };
    const fs::path out = ot::paths::fromUtf8(OTTISK_SOURCE_DIR) / L"build" / L"edit-stability.json";
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream(out, std::ios::binary) << report.dump(2) << "\n";

    std::printf(
        "edit_stability: файлов %zu, открыто %d, с редактируемым текстом %d, стабильных %d (%.1f %%), "
        "только чтение целиком %d, страниц %d (с правкой %d), блоков %d (редактируемых %d), операторов %d, "
        "%.0f с → %s\n",
        recs.size(), opened, withText, stable, share * 100.0, allReadOnly, pages, editedPages, blocks,
        editable, ops, seconds, ot::paths::toUtf8(out).c_str());
    for (const auto& u : unstable) {
        std::printf("unstable: %s — %s\n", u["file"].get<std::string>().c_str(),
                    u["problem"].get<std::string>().c_str());
    }
    for (const auto& e : errors) {
        ADD_FAILURE() << e["file"].get<std::string>() << ": " << e["error"].get<std::string>();
    }
    EXPECT_GE(share, 0.9) << "стабильных " << stable << " из " << withText;
}
