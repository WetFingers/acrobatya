// bench — замеры движка (эталон 5.8, план 1.2). Числа имеют смысл только на Release (build/release): Debug
// MuPDF в разы медленнее, цели 5.8 на нём не выполнимы.
//   bench <файл | имя> [--name id] [--password p] [--runs 5] [--pages 50] [--threads N] [--json out.json]
// Имена: hello, pdf32000, big-1200, images-200, enc-r6 — ищутся в tests/data/mini, <корпус>/gen, <сборка>/gen
// (make_big.py), tests/fixtures и корпусе (%OTTISK_CORPUS% или C:\Tools\ottisk\corpus); нет — код 77
// (SKIP). Каждый прогон начинается с пустого store и кеша глифов. Метрики (samples — по прогону, сравнивает
// медиану tools/bench_compare.py; «падение» — +25 %):
//   open_first_ms      открыть → display list стр. 1 → растр стр. 1 при 100 % (96 dpi, BGRA)   цель ≤ 300 мс
//   layout_ms          рамки всех страниц (fz_bound_page × N — раскладка знает только размеры)  ≤ 200 мс
//   page_uncached_ms   смена страницы без кеша: display list + растр при 100 % (стр. 2…11)       ≤ 150 мс
//   page_cached_ms     то же с готовым display list                                            ≤ 50 мс
//   zoom_first_tile_ms 200 %: первый видимый тайл 512 × 512 при готовом списке                  ≤ 120 мс
//   tiles_100_ms, tiles_200_ms — все тайлы страниц 1…min(50, N) в RenderPool (списки строятся по порядку,
//                      как в потоке документа), время «стены»
//   peak_working_set_mb пиковая память процесса (PeakWorkingSetSize), один файл — один процесс
// Режим поиска (план 1.4): `bench search` (PDF32000_2008.pdf) или `--search <образец>` у любого файла —
//   search_all_ms      открыть → текстовые слои всех страниц → SearchSession по всему документу   ≤ 3 000 мс
#include <windows.h>

#include <psapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/display_cache.h"
#include "core/render/fit.h"
#include "core/render/renderer.h"
#include "core/render/tiles.h"
#include "core/text/search.h"
#include "core/text/stext.h"
#include "core/util/paths.h"
#include "services/render_pool.h"

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Known {
    const char* name;
    const char* file;
    const char* password;
    const char* search = nullptr;  // режим поиска: образец
};

// Файлы плана 1.2 (эталон 5.8).
const Known kKnown[] = {
    {"hello", "hello_world.pdf", ""},
    {"pdf32000", "PDF32000_2008.pdf", ""},
    {"big-1200", "big-1200.pdf", ""},
    {"images-200", "images-200.pdf", ""},
    {"enc-r6", "enc-r6.pdf", "user"},
    {"search", "PDF32000_2008.pdf", "", "annotation"},  // план 1.4: поиск по документу ≤ 3 с
};

fs::path corpusDir() {
    if (const wchar_t* env = _wgetenv(L"OTTISK_CORPUS"); env != nullptr && *env != 0) {
        return env;
    }
    // Не %LOCALAPPDATA%: приложение Claude перенаправляет туда записи своих процессов (docs/progress.md).
    return fs::path(L"C:\\Tools\\ottisk\\corpus");
}

fs::path findFile(const std::string& fileName) {
    const fs::path src = ot::paths::fromUtf8(OTTISK_SOURCE_DIR);
    const fs::path bin = ot::paths::fromUtf8(OTTISK_BINARY_DIR);
    const fs::path name = ot::paths::fromUtf8(fileName);
    for (const fs::path& dir : {src / L"tests" / L"data" / L"mini", corpusDir() / L"gen", bin / L"gen"}) {
        if (fs::exists(dir / name)) {
            return dir / name;
        }
    }
    for (const fs::path& root : {src / L"tests" / L"fixtures", corpusDir()}) {
        std::error_code ec;
        if (root.empty() || !fs::is_directory(root, ec)) {
            continue;
        }
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec),
             end;
             it != end; it.increment(ec)) {
            if (!ec && it->is_regular_file(ec) && it->path().filename() == name) {
                return it->path();
            }
        }
    }
    return {};
}

double peakWorkingSetMb() {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)) == 0) {
        return 0.0;
    }
    return static_cast<double>(pmc.PeakWorkingSetSize) / (1024.0 * 1024.0);
}

// Растр всей страницы при 100 % в буфер (BGRA) клоном этого потока.
void renderWhole(const ot::DisplayList& list, float zoom, std::vector<std::uint8_t>& buf) {
    const ot::IRect device = ot::pageDeviceRect(list.bounds(), zoom, 1.0f);
    if (device.empty()) {
        return;
    }
    buf.resize(static_cast<size_t>(device.width()) * static_cast<size_t>(device.height()) * 4);
    ot::renderDisplayList(ot::MuContext::forThisThread().get(), list,
                          ot::pageMatrix(list.bounds(), zoom, 1.0f), device, buf.data(), device.width() * 4);
}

// Все тайлы страниц [0, pages) при zoom в пуле; списки строятся по порядку в этом потоке.
double tilesWall(const ot::Document& doc, int pages, float zoom, ot::RenderPool& pool) {
    std::atomic<int> failed{0};
    const auto t0 = Clock::now();
    for (int p = 0; p < pages; ++p) {
        const auto list = ot::DisplayList::build(doc, p);
        const ot::Matrix m = ot::pageMatrix(list->bounds(), zoom, 1.0f);
        for (const auto& t : ot::tileRectsFor(list->bounds(), zoom, 1.0f)) {
            ot::TileRequest req;
            req.key = ot::makeTileKey(1, p, zoom, 1.0f, 0, t);
            req.list = list;
            req.matrix = m;
            req.rect = t.rect;
            (void)pool.submit(req, nullptr, [&failed](const ot::TileResult& r) {
                if (!r.ok()) {
                    ++failed;
                }
            });
        }
    }
    pool.waitForDone();
    const auto t1 = Clock::now();
    if (failed.load() != 0) {
        throw std::runtime_error("tiles failed: " + std::to_string(failed.load()));
    }
    return ms(t0, t1);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: bench <file.pdf | hello | pdf32000 | big-1200 | images-200 | enc-r6 | search> "
                     "[--name id]\n"
                     "             [--password p] [--runs 5] [--pages 50] [--threads N] [--search needle]\n"
                     "             [--json out.json]\n");
        return 2;
    }
    std::string arg = ot::paths::toUtf8(argv[1]);
    std::string name;
    std::string password;
    int runs = 5;
    int maxPages = 50;
    int threads = 0;
    std::string needle;
    fs::path jsonOut;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::wstring a = argv[i];
        const std::string v = ot::paths::toUtf8(argv[i + 1]);
        if (a == L"--name") {
            name = v;
        } else if (a == L"--password") {
            password = v;
        } else if (a == L"--runs") {
            runs = std::max(1, std::stoi(v));
        } else if (a == L"--pages") {
            maxPages = std::max(1, std::stoi(v));
        } else if (a == L"--threads") {
            threads = std::stoi(v);
        } else if (a == L"--search") {
            needle = v;
        } else if (a == L"--json") {
            jsonOut = argv[i + 1];
        }
    }
    fs::path file = ot::paths::fromUtf8(arg);
    if (!fs::exists(file)) {
        for (const Known& k : kKnown) {
            if (arg == k.name) {
                file = findFile(k.file);
                if (name.empty()) {
                    name = k.name;
                }
                if (password.empty()) {
                    password = k.password;
                }
                if (needle.empty() && k.search != nullptr) {
                    needle = k.search;
                }
            }
        }
    }
    if (file.empty() || !fs::exists(file)) {
        std::printf("SKIP: %s not found (tests/data/mini, tests/fixtures, corpus, <build>/gen)\n",
                    arg.c_str());
        return 77;
    }
    if (name.empty()) {
        name = ot::paths::toUtf8(file.stem());
    }
#ifndef NDEBUG
    std::fprintf(stderr, "bench: Debug build — numbers are not comparable with 5.8 targets\n");
#endif
    const float zoom100 = ot::percentToZoom(100.0f, 96.0f);
    const float zoom200 = ot::percentToZoom(200.0f, 96.0f);
    std::map<std::string, std::vector<double>> samples;
    int pages = 0;
    try {
        ot::RenderPool pool(threads);
        std::vector<std::uint8_t> buf;
        int searchHits = -1;
        for (int r = 0; r < runs && !needle.empty(); ++r) {
            // Поиск: открыть → слои всех страниц по запросу поиска → все совпадения (как панель поиска).
            ot::MuContext::forThisThread().emptyStore();
            const auto t0 = Clock::now();
            ot::OpenResult opened = ot::Document::open(file, password);
            if (!opened.ok) {
                throw std::runtime_error("open failed: " + opened.error.message);
            }
            const std::unique_ptr<ot::Document> doc = std::move(opened.document);
            pages = doc->pageCount();
            ot::SearchSession search(needle, {});
            search.run(pages,
                       [&](int p) { return ot::TextPage::build(*doc, p, ot::SearchSession::textOptions()); });
            samples["search_all_ms"].push_back(ms(t0, Clock::now()));
            searchHits = search.count();
        }
        if (searchHits >= 0) {
            std::printf("%-12s search \"%s\": %d hits on %d pages\n", name.c_str(), needle.c_str(),
                        searchHits, pages);
        }
        for (int r = 0; r < runs && needle.empty(); ++r) {
            ot::MuContext::forThisThread().emptyStore();
            // Открытие → первый растр первой страницы.
            const auto t0 = Clock::now();
            ot::OpenResult opened = ot::Document::open(file, password);
            if (!opened.ok) {
                throw std::runtime_error("open failed: " + opened.error.message);
            }
            const std::unique_ptr<ot::Document> doc = std::move(opened.document);
            const auto first = ot::DisplayList::build(*doc, 0);
            renderWhole(*first, zoom100, buf);
            samples["open_first_ms"].push_back(ms(t0, Clock::now()));
            pages = doc->pageCount();
            // Раскладка: рамки всех страниц.
            const auto t1 = Clock::now();
            for (int p = 0; p < pages; ++p) {
                (void)doc->pageBox(p);
            }
            samples["layout_ms"].push_back(ms(t1, Clock::now()));
            // Смена страницы без кеша и из кеша списков (стр. 2…11).
            std::vector<std::shared_ptr<const ot::DisplayList>> lists;
            const int last = std::min(pages, 11);
            if (last > 1) {
                const auto t2 = Clock::now();
                for (int p = 1; p < last; ++p) {
                    lists.push_back(ot::DisplayList::build(*doc, p));
                    renderWhole(*lists.back(), zoom100, buf);
                }
                samples["page_uncached_ms"].push_back(ms(t2, Clock::now()) / (last - 1));
                const auto t3 = Clock::now();
                for (const auto& l : lists) {
                    renderWhole(*l, zoom100, buf);
                }
                samples["page_cached_ms"].push_back(ms(t3, Clock::now()) / (last - 1));
            }
            // Зум 200 %: первый видимый тайл при готовом списке.
            {
                const auto tiles = ot::tileRectsFor(first->bounds(), zoom200, 1.0f);
                if (!tiles.empty()) {
                    const ot::IRect& rect = tiles.front().rect;
                    std::vector<std::uint8_t> tile(static_cast<size_t>(rect.width()) * rect.height() * 4);
                    const auto t4 = Clock::now();
                    ot::renderDisplayList(ot::MuContext::forThisThread().get(), *first,
                                          ot::pageMatrix(first->bounds(), zoom200, 1.0f), rect, tile.data(),
                                          rect.width() * 4);
                    samples["zoom_first_tile_ms"].push_back(ms(t4, Clock::now()));
                }
            }
            // Тайлы страниц 1…50 при 100 % и 200 %.
            const int tilePages = std::min(pages, maxPages);
            samples["tiles_100_ms"].push_back(tilesWall(*doc, tilePages, zoom100, pool));
            samples["tiles_200_ms"].push_back(tilesWall(*doc, tilePages, zoom200, pool));
        }
        samples["peak_working_set_mb"].push_back(peakWorkingSetMb());
        nlohmann::json benches = nlohmann::json::array();
        for (const auto& [metric, values] : samples) {
            const std::string unit = metric.ends_with("_mb") ? "mb" : "ms";
            benches.push_back({{"name", name + "/" + metric}, {"unit", unit}, {"samples", values}});
            std::vector<double> sorted = values;
            std::sort(sorted.begin(), sorted.end());
            std::printf("%-12s %-20s median %9.2f %s (n=%zu)\n", name.c_str(), metric.c_str(),
                        sorted[sorted.size() / 2], unit.c_str(), values.size());
        }
        nlohmann::json j = {{"tool", "bench"},
                            {"name", name},
                            {"file", ot::paths::toUtf8(file.filename())},
                            {"pages", pages},
                            {"runs", runs},
                            {"search", needle},
                            {"tile_pages", std::min(pages, maxPages)},
                            {"threads", pool.maxThreads()},
                            {"mupdf", ot::MuContext::version()},
#ifdef NDEBUG
                            {"build", "release"},
#else
                            {"build", "debug"},
#endif
                            {"benchmarks", benches}};
        if (!jsonOut.empty()) {
            std::error_code ec;
            fs::create_directories(jsonOut.parent_path(), ec);
            ot::paths::writeFile(jsonOut, j.dump(1) + "\n");
        }
#ifdef NDEBUG
        // Порог плана 1.4 (только Release): поиск по PDF32000_2008 — не дольше 3 с по медиане.
        if (const auto it = samples.find("search_all_ms"); it != samples.end() && name == "search") {
            std::vector<double> sorted = it->second;
            std::sort(sorted.begin(), sorted.end());
            if (sorted[sorted.size() / 2] > 3000.0) {
                std::fprintf(stderr, "bench search: median %.0f ms > 3000 ms\n", sorted[sorted.size() / 2]);
                return 1;
            }
        }
#endif
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bench %s: %s\n", name.c_str(), e.what());
        return 1;
    }
    return 0;
}
