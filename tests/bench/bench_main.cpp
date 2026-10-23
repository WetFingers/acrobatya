// bench — замеры движка (эталон 5.8). Первая версия этапа 0: открытие → первый растр первой страницы
// и смена страницы, медиана из N прогонов; сессия 1.2 дополняет (кеш, тайлы, поиск, память).
//   bench <файл.pdf> [--runs 5] [--dpi 96] [--json out.json]
// Замеры — только на Release-сборке (build/release).
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/util/paths.h"

namespace {

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0.0 : v[v.size() / 2];
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: bench <file.pdf> [--runs 5] [--dpi 96] [--json out.json]\n");
        return 2;
    }
    const std::filesystem::path file = argv[1];
    int runs = 5;
    float dpi = 96.0f;
    std::filesystem::path jsonOut;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::wstring a = argv[i];
        if (a == L"--runs")
            runs = std::stoi(argv[i + 1]);
        else if (a == L"--dpi")
            dpi = std::stof(argv[i + 1]);
        else if (a == L"--json")
            jsonOut = argv[i + 1];
    }
    using clock = std::chrono::steady_clock;
    std::vector<double> openFirst, nextPage;
    int pages = 0;
    try {
        for (int r = 0; r < runs; ++r) {
            ot::MuContext mu;
            const auto t0 = clock::now();
            auto doc = ot::Document::open(mu, file);
            (void)doc->renderPage(0, {dpi, true});
            const auto t1 = clock::now();
            pages = doc->pageCount();
            if (pages > 1) {
                (void)doc->renderPage(1, {dpi, true});
            }
            const auto t2 = clock::now();
            openFirst.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            nextPage.push_back(std::chrono::duration<double, std::milli>(t2 - t1).count());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "bench: %s\n", e.what());
        return 1;
    }
    nlohmann::json j = {{"file", ot::paths::toUtf8(file)},
                        {"pages", pages},
                        {"runs", runs},
                        {"dpi", dpi},
#ifdef NDEBUG
                        {"build", "release"},
#else
                        {"build", "debug"},
#endif
                        {"open_first_ms", median(openFirst)},
                        {"next_page_ms", median(nextPage)}};
    const std::string text = j.dump(2);
    std::printf("%s\n", text.c_str());
    if (!jsonOut.empty()) {
        ot::paths::writeFile(jsonOut, text + "\n");
    }
    return 0;
}
