// engine.edit_render.* — правка слова не искажает остальную страницу (план 3.3, п. 3; критерий этапа 3 «на
// фикстурах редактора правка слова перевёрстывает абзац без искажения остальной страницы»; эталон 3.9):
// в каждом редактируемом абзаце фикстур tests/fixtures/edit (свои и Word) в середину первой строки
// вставляется слово, Rewriter пишет абзац — рендер страницы вне рамок абзаца до и после (± 2 pt) совпадает до
// пикселя (pixels == 0), новое слово извлекается fz_stext (без учёта переносов), undo возвращает рендер
// целиком. То же на корпусе text-edit (первый редактируемый абзац страницы, до kMaxPages страниц файла): доля
// страниц с pixels == 0 — не меньше 90 %, отчёт — build/edit-render.json (журнал — docs/progress.md). Нет
// корпуса — SKIP.
#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_resolver.h"
#include "core/edit/paragraph_editor.h"
#include "core/edit/rewriter.h"
#include "core/edit/system_fonts.h"
#include "core/edit/text_blocks.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/render/image_compare.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"
#include "support/probe.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::edit::ContentModel;
using ot::edit::ParagraphEditor;
using ot::edit::TextBlock;

namespace {

constexpr int kMaxPages = 5;
constexpr unsigned kThreads = 4;
constexpr float kDpi = 72;

fs::path editFixtures() { return ot::paths::fromUtf8(OTTISK_SOURCE_DIR) / L"tests" / L"fixtures" / L"edit"; }

ot::edit::SystemFonts& systemFonts() {
    static ot::edit::FakeSystemFonts fonts;
    return fonts;
}

struct EditOutcome {
    bool edited = false;    // абзац записан
    long long outside = 0;  // отличающихся пикселей вне абзаца
    bool found = false;     // новое слово в fz_stext
    bool undone = false;    // undo вернул рендер
    std::string error;
};

// Без пробелов и переводов строк: слово шире узкой ячейки переносится по символам.
std::string squeeze(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            out.push_back(c);
        }
    }
    return out;
}

// Вставить слово в середину первой строки абзаца block страницы page и записать.
EditOutcome editBlock(ot::Document& doc, int page, std::size_t block, const std::string& word) {
    EditOutcome out;
    const ContentModel model = ContentModel::build(doc, page);
    const std::vector<TextBlock> blocks = ot::edit::blocksForPage(model);
    const ot::Bitmap before = doc.renderPage(page, {kDpi, false});
    ParagraphEditor e = ParagraphEditor::fromBlock(model, blocks.at(block), blocks);
    const auto& first = e.layout().lines.front();
    const int at = e.wordAt(first.begin + (first.end - first.begin) / 2).begin;
    e.insert(at, ot::utf8::decode(word + " "));
    ot::edit::FontLibrary lib(doc.context().get());
    ot::edit::Rewriter rewriter(doc, lib, systemFonts(), ot::edit::FallbackFonts{});
    const ot::edit::RewriteReport r = rewriter.rewrite(e);
    if (!r.ok()) {
        out.error = std::string(ot::edit::rewriteErrorCode(r.error)) + ": " + r.message;
        return out;
    }
    out.edited = true;
    const ot::Bitmap after = doc.renderPage(page, {kDpi, false});
    out.outside = ot::diffPixelsOutside(before, after, {ot::pixelRect(r.dirty, kDpi, 2)});
    ot::TextOptions options;
    options.readingOrder = false;
    out.found = squeeze(ot::TextPage::build(doc, page, options)->text()).find(word) != std::string::npos;
    doc.undoStack().undo();
    const ot::Bitmap undone = doc.renderPage(page, {kDpi, false});
    out.undone = ot::diffPixelsOutside(before, undone, {}) == 0;
    return out;
}

}  // namespace

TEST(edit_render, fixtures_outside_block_unchanged) {
    OT_FEATURE("edit-01");
    OT_FEATURE("edit-02");
    int edited = 0;
    for (const auto& entry : fs::directory_iterator(editFixtures())) {
        if (entry.path().extension() != L".pdf") {
            continue;
        }
        const std::string name = ot::paths::toUtf8(entry.path().filename());
        SCOPED_TRACE(name);
        auto doc = ot::Document::openOrThrow(entry.path());
        const auto blocks = ot::edit::blocksForPage(ContentModel::build(*doc, 0));
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            if (!blocks[b].editable()) {
                continue;
            }
            SCOPED_TRACE(blocks[b].text);
            const EditOutcome r = editBlock(*doc, 0, b, "inserted");
            ASSERT_TRUE(r.edited) << r.error;
            EXPECT_EQ(r.outside, 0);
            EXPECT_TRUE(r.found);
            EXPECT_TRUE(r.undone);
            ++edited;
        }
    }
    std::printf("edit_render: абзацев фикстур %d\n", edited);
    EXPECT_GE(edited, 40);
}

namespace {

struct FileResult {
    std::string rel;
    bool opened = false;
    int pages = 0;  // страниц с редактируемым абзацем
    int exact = 0;  // вне абзаца — pixels == 0
    int found = 0;  // слово извлекается
    int undone = 0;
    int refused = 0;  // Rewriter отказал (подписан, только чтение)
    std::string problem;
    std::string error;
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
            const auto blocks = ot::edit::blocksForPage(ContentModel::build(doc, page));
            std::size_t pick = blocks.size();
            for (std::size_t b = 0; b < blocks.size(); ++b) {
                if (blocks[b].editable() && blocks[b].text.size() >= 12) {
                    pick = b;
                    break;
                }
            }
            if (pick == blocks.size()) {
                continue;
            }
            const EditOutcome e = editBlock(doc, page, pick, "Ottisk");
            if (!e.edited) {
                ++r.refused;
                if (r.problem.empty()) {
                    r.problem = "стр. " + std::to_string(page + 1) + ": " + e.error;
                }
                continue;
            }
            ++r.pages;
            r.exact += e.outside == 0 ? 1 : 0;
            r.found += e.found ? 1 : 0;
            r.undone += e.undone ? 1 : 0;
            if ((e.outside != 0 || !e.found || !e.undone) && r.problem.empty()) {
                r.problem = "стр. " + std::to_string(page + 1) + ": вне абзаца " + std::to_string(e.outside) +
                            " px" + (e.found ? "" : ", слово не извлекается") + (e.undone ? "" : ", undo");
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

TEST(edit_render, word_edit_on_text_edit_corpus) {
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
    int opened = 0, pages = 0, exact = 0, found = 0, undone = 0, refused = 0;
    json problems = json::array();
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
        exact += r.exact;
        found += r.found;
        undone += r.undone;
        refused += r.refused;
        if (!r.problem.empty()) {
            problems.push_back({{"file", r.rel}, {"problem", r.problem}});
        }
    }
    const double share = pages > 0 ? static_cast<double>(exact) / pages : 0.0;
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const json report = {
        {"tag", "text-edit"},    {"files", recs.size()},         {"opened", opened},   {"editedPages", pages},
        {"exactOutside", exact}, {"exactShare", share},          {"wordFound", found}, {"undone", undone},
        {"refused", refused},    {"maxPagesPerFile", kMaxPages}, {"seconds", seconds}, {"problems", problems},
        {"errors", errors},
    };
    const fs::path out = ot::paths::fromUtf8(OTTISK_SOURCE_DIR) / L"build" / L"edit-render.json";
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    std::ofstream(out, std::ios::binary) << report.dump(2) << "\n";
    std::printf(
        "edit_render: файлов %zu, открыто %d, страниц с правкой %d, вне абзаца без отличий %d (%.1f %%), "
        "слово "
        "извлекается %d, undo %d, отказов %d, %.0f с → %s\n",
        recs.size(), opened, pages, exact, share * 100.0, found, undone, refused, seconds,
        ot::paths::toUtf8(out).c_str());
    for (const auto& e : errors) {
        ADD_FAILURE() << e["file"].get<std::string>() << ": " << e["error"].get<std::string>();
    }
    EXPECT_GE(share, 0.9) << "без отличий " << exact << " из " << pages;
    EXPECT_GE(found, pages * 9 / 10);
    EXPECT_EQ(undone, pages);
}

// Разбор чужого файла: OTTISK_DUMP_FILE, OTTISK_DUMP_PAGE (с 1) — абзац, текст после правки слова и новые
// операции (unit-тест не нужен: --gtest_also_run_disabled_tests --gtest_filter=edit_render.DISABLED_file).
TEST(edit_render, DISABLED_file) {
    const char* file = std::getenv("OTTISK_DUMP_FILE");
    if (file == nullptr) {
        GTEST_SKIP() << "OTTISK_DUMP_FILE не задан";
    }
    const char* pageEnv = std::getenv("OTTISK_DUMP_PAGE");
    const int page = pageEnv != nullptr ? std::atoi(pageEnv) - 1 : 0;
    auto doc = ot::Document::openOrThrow(ot::paths::fromUtf8(file));
    const ContentModel model = ContentModel::build(*doc, page);
    const auto blocks = ot::edit::blocksForPage(model);
    std::size_t pick = blocks.size();
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        if (blocks[b].editable() && blocks[b].text.size() >= 12) {
            pick = b;
            break;
        }
    }
    ASSERT_LT(pick, blocks.size());
    std::printf("block %zu: %s\n", pick, blocks[pick].text.c_str());
    ParagraphEditor e = ParagraphEditor::fromBlock(model, blocks[pick], blocks);
    for (const auto& st : e.styles()) {
        std::printf("style: font %s (%s) size %.2f render %d rise %.2f\n", st.fontName.c_str(),
                    st.fontResource.c_str(), st.size, st.render, st.rise);
    }
    const auto& first = e.layout().lines.front();
    const int at = e.wordAt(first.begin + (first.end - first.begin) / 2).begin;
    e.insert(at, ot::utf8::decode("Ottisk "));
    ot::edit::FontLibrary lib(doc->context().get());
    ot::edit::Rewriter rewriter(*doc, lib, systemFonts(), ot::edit::FallbackFonts{});
    const ot::edit::RewriteReport r = rewriter.rewrite(e);
    std::printf("rewrite: %s %s, lines %d\n", ot::edit::rewriteErrorCode(r.error), r.message.c_str(),
                r.lines);
    const ContentModel after = ContentModel::build(*doc, page);
    for (int op : e.source().ops) {
        std::printf("op %d: %s\n", op, after.opText(op).c_str());
        const auto& span =
            after.spans()[static_cast<std::size_t>(after.operations()[static_cast<std::size_t>(op)].span)];
        std::printf(
            "  text: %s, font %s, render %d, hidden %d, reason %s\n",
            after.spanText(span.op == op ? after.operations()[static_cast<std::size_t>(op)].span : 0).c_str(),
            after.fonts()[static_cast<std::size_t>(span.font)].name.c_str(), span.render, span.hidden ? 1 : 0,
            ot::edit::readOnlyCode(span.reason));
    }
    for (int op : e.source().ops) {
        const auto& span =
            after.spans()[static_cast<std::size_t>(after.operations()[static_cast<std::size_t>(op)].span)];
        for (int g = span.firstGlyph; g < span.firstGlyph + span.glyphCount; ++g) {
            const auto& gl = after.glyphs()[static_cast<std::size_t>(g)];
            std::printf("  glyph U+%04X code %u gid %d w0 %.3f x %.2f\n",
                        gl.unicode.empty() ? 0u : static_cast<unsigned>(gl.unicode[0]), gl.code, gl.gid,
                        gl.w0, gl.origin.x);
        }
        break;
    }
    ot::TextOptions options;
    options.readingOrder = false;
    const std::string text = ot::TextPage::build(*doc, page, options)->text();
    const std::size_t hit = text.find("Ott");
    std::printf("stext: %s\n", hit == std::string::npos ? "(нет Ott)" : text.substr(hit, 80).c_str());
    const std::string probe = blocks[pick].text.substr(0, 10);
    const std::size_t near = text.find(probe);
    std::printf("stext near: %s\n", near == std::string::npos ? "(нет)" : text.substr(near, 120).c_str());
}
