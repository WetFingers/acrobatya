// render_refs — эталоны рендера tests/render/expected (эталон 9.3). Только Release-сборка MuPDF
// (build/release); обновление — только из уполномоченной сессии с причиной.
//
//   render_refs --update --reason "…" [--only id,id] [--files tests/render/files.json] [--dpi 72,150]
//       рендерит каждую страницу files.json при 72 и 150 dpi (AA 8, ICC выкл., RGB без альфы) в
//       tests/render/expected/<id>.<стр>.<dpi>.png и пишет tests/render/expected/expected.json (sha256,
//       версия MuPDF, дата, причина; флаги release_only и свои пороги прежних записей сохраняются)
//   render_refs --mark-release-only <id> --reason "…"
//       пометить запись: в Debug-сборке её тест пропускается (расхождение Debug ↔ Release)
//   render_refs --check [--out <каталог>] [--json <файл>]
//       сверить рендер с эталонами expected.json (метрики imgcmp по каждому растру, итог); --out — текущие и
//       diff-растры расходящихся (для tools/review_refs.py --actual <каталог>); Debug-сборкой — замер
//       Debug ↔ Release; код 1 — есть ошибки или растры за порогом
// Источник "corpus:<путь>" — относительно C:\Tools\ottisk\corpus (или OTTISK_CORPUS).
// Код: 0 — успех, 1 — ошибки рендера, 2 — неверные аргументы.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <openssl/evp.h>
#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/util/paths.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path root() { return ot::paths::fromUtf8(OTTISK_SOURCE_DIR); }

fs::path corpusRoot() {
    if (const char* c = std::getenv("OTTISK_CORPUS"); c != nullptr && *c != '\0') {
        return ot::paths::fromUtf8(c);
    }
    // Не %LOCALAPPDATA%: приложение Claude перенаправляет туда записи своих процессов (docs/progress.md).
    return std::filesystem::path(L"C:\\Tools\\ottisk\\corpus");
}

fs::path resolveSource(const std::string& source) {
    const std::string prefix = "corpus:";
    if (source.rfind(prefix, 0) == 0) {
        return corpusRoot() / ot::paths::fromUtf8(source.substr(prefix.size()));
    }
    return root() / ot::paths::fromUtf8(source);
}

std::string sha256(const std::string& data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(data.data(), data.size(), md, &len, EVP_sha256(), nullptr);
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned int i = 0; i < len; ++i) {
        out += hex[md[i] >> 4];
        out += hex[md[i] & 15];
    }
    return out;
}

std::string nowUtc() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::vector<int> parseDpi(const std::string& s) {
    std::vector<int> out;
    size_t start = 0;
    while (start < s.size()) {
        const size_t comma = s.find(',', start);
        out.push_back(std::stoi(s.substr(start, comma - start)));
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

json loadJson(const fs::path& p) {
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return json();
    }
    return json::parse(ot::paths::readFile(p));
}

void saveJson(const fs::path& p, const json& j) { ot::paths::writeFile(p, j.dump(2, ' ', false) + "\n"); }

// only — id через запятую: перерисовать только их, остальные записи остаются как были. Источника нет
// (корпус не скачан) — прежняя запись сохраняется без изменений («KEEP»), а не выпадает из expected.json.
int update(const fs::path& filesJson, const std::vector<int>& dpis, const std::string& reason,
           const std::set<std::string>& only) {
    const json files = loadJson(filesJson);
    if (!files.contains("files") || !files["files"].is_array()) {
        std::fprintf(stderr, "render_refs: нет списка files в %s\n", ot::paths::toUtf8(filesJson).c_str());
        return 2;
    }
    const fs::path expectedJson = root() / L"tests" / L"render" / L"expected" / L"expected.json";
    const json old = loadJson(expectedJson);
    std::map<std::string, json> previous;
    if (old.is_object()) {
        for (const json& item : old.value("items", json::array())) {
            previous[item.value("id", std::string())] = item;
        }
    }
    ot::MuContext mu;
    fz_set_aa_level(mu.get(), 8);
    fz_disable_icc(mu.get());
    json items = json::array();
    int failures = 0, updated = 0;
    for (const std::string& id : only) {
        bool known = false;
        for (const json& f : files["files"]) {
            known = known || f.value("id", std::string()) == id;
        }
        if (!known) {
            std::fprintf(stderr, "render_refs: --only %s — нет в %s\n", id.c_str(),
                         ot::paths::toUtf8(filesJson).c_str());
            return 2;
        }
    }
    for (const json& f : files["files"]) {
        const std::string id = f.value("id", std::string());
        const std::string source = f.value("source", std::string());
        std::vector<int> pages;
        for (const json& p : f.value("pages", json::array({1}))) {
            pages.push_back(p.get<int>());
        }
        json item = {{"id", id},         {"source", source},      {"renders", json::array()},
                     {"fuzzy", nullptr}, {"release_only", false}, {"release_only_reason", ""}};
        const auto it = previous.find(id);
        if (!only.empty() && only.count(id) == 0) {
            if (it != previous.end()) {
                items.push_back(it->second);
            }
            continue;
        }
        std::error_code ec;
        if (!fs::exists(resolveSource(source), ec) && it != previous.end()) {
            std::printf("KEEP %s: нет источника, запись прежняя\n", id.c_str());
            items.push_back(it->second);
            continue;
        }
        if (it != previous.end()) {
            item["fuzzy"] = it->second.value("fuzzy", json());
            item["release_only"] = it->second.value("release_only", false);
            item["release_only_reason"] = it->second.value("release_only_reason", std::string());
        }
        try {
            auto doc = ot::Document::openOrThrow(mu, resolveSource(source));
            for (int page : pages) {
                for (int dpi : dpis) {
                    const ot::Bitmap bmp = doc->renderPage(page - 1, {static_cast<float>(dpi), true});
                    const std::string png = ot::encodePng(mu, bmp);
                    const std::string rel = "tests/render/expected/" + id + "." + std::to_string(page) + "." +
                                            std::to_string(dpi) + ".png";
                    ot::paths::writeFile(root() / ot::paths::fromUtf8(rel), png);
                    item["renders"].push_back({{"page", page},
                                               {"dpi", dpi},
                                               {"png", rel},
                                               {"sha256", sha256(png)},
                                               {"width", bmp.width},
                                               {"height", bmp.height}});
                }
            }
            std::printf("ok   %s\n", id.c_str());
            ++updated;
        } catch (const std::exception& e) {
            std::printf("FAIL %s: %s\n", id.c_str(), e.what());
            ++failures;
            continue;
        }
        items.push_back(item);
    }
    json out = {{"mupdf", ot::MuContext::version()},
                {"date", nowUtc()},
                {"reason", reason},
#ifdef NDEBUG
                {"build", "release"},
#else
                {"build", "debug"},
#endif
                {"defaults", {{"fuzzy", {{"max_delta", 3}, {"mse", 0.05}, {"wmse", 15}, {"ssim", 0.98}}}}},
                {"items", items}};
    saveJson(expectedJson, out);
    std::printf("expected.json: %zu записей, перерисовано %d, ошибок %d\n", items.size(), updated, failures);
    return failures == 0 ? 0 : 1;
}

int markReleaseOnly(const std::string& id, const std::string& reason) {
    const fs::path expectedJson = root() / L"tests" / L"render" / L"expected" / L"expected.json";
    json doc = loadJson(expectedJson);
    bool found = false;
    for (json& item : doc["items"]) {
        if (item.value("id", std::string()) == id) {
            item["release_only"] = true;
            item["release_only_reason"] = reason;
            found = true;
        }
    }
    if (!found) {
        std::fprintf(stderr, "render_refs: нет записи %s\n", id.c_str());
        return 2;
    }
    saveJson(expectedJson, doc);
    std::printf("release_only: %s (%s)\n", id.c_str(), reason.c_str());
    return 0;
}

// Сверка с эталонами expected.json тем же рендером, что у render_tests: метрики по каждому растру, итог
// (одинаковых, в порогах, за порогом); --out — текущие и diff-растры расходящихся (для review_refs.py
// --actual), --json — итог в файл. Замер Debug ↔ Release: Debug-сборка render_refs --check.
int check(const fs::path& outDir, const fs::path& jsonOut) {
    const fs::path expectedJson = root() / L"tests" / L"render" / L"expected" / L"expected.json";
    const json doc = loadJson(expectedJson);
    ot::FuzzyThreshold defaults;
    if (const json f = doc.value("defaults", json::object()).value("fuzzy", json()); f.is_object()) {
        defaults = {f.value("max_delta", 3), f.value("mse", 0.05), f.value("wmse", 15.0),
                    f.value("ssim", 0.98)};
    }
    ot::MuContext mu;
    fz_set_aa_level(mu.get(), 8);
    fz_disable_icc(mu.get());
    int renders = 0, identical = 0, within = 0, outside = 0, skipped = 0, failures = 0;
    int worstDelta = 0;
    double worstMse = 0, worstWmse = 0, worstSsim = 1;
    json rows = json::array();
    for (const json& item : doc.value("items", json::array())) {
        const std::string id = item.value("id", std::string());
        const fs::path src = resolveSource(item.value("source", std::string()));
        std::error_code ec;
        if (!fs::exists(src, ec)) {
            skipped += static_cast<int>(item.value("renders", json::array()).size());
            std::printf("SKIP %s: нет источника\n", id.c_str());
            continue;
        }
        ot::FuzzyThreshold fuzzy = defaults;
        if (const json f = item.value("fuzzy", json()); f.is_object()) {
            fuzzy = {f.value("max_delta", fuzzy.maxDelta), f.value("mse", fuzzy.mse),
                     f.value("wmse", fuzzy.windowedMse), f.value("ssim", fuzzy.ssim)};
        }
        try {
            auto d = ot::Document::openOrThrow(mu, src);
            for (const json& r : item.value("renders", json::array())) {
                const int page = r.value("page", 1), dpi = r.value("dpi", 72);
                const std::string png = r.value("png", std::string());
                const ot::Bitmap actual = d->renderPage(page - 1, {static_cast<float>(dpi), true});
                const ot::Bitmap expected = ot::loadPng(mu, root() / ot::paths::fromUtf8(png));
                ot::Bitmap diff;
                const ot::CompareResult m = ot::compareBitmaps(expected, actual, &diff);
                ++renders;
                const bool same = m.sameSize && m.differingPixels == 0;
                const bool ok = m.within(fuzzy);
                identical += same ? 1 : 0;
                within += (!same && ok) ? 1 : 0;
                outside += ok ? 0 : 1;
                worstDelta = std::max(worstDelta, m.maxDelta);
                worstMse = std::max(worstMse, m.mse);
                worstWmse = std::max(worstWmse, m.windowedMse);
                worstSsim = std::min(worstSsim, m.ssim);
                std::printf("%s %s p%d @%d %s\n", same ? "same" : (ok ? "ok  " : "DIFF"), id.c_str(), page,
                            dpi, m.toJson().c_str());
                rows.push_back({{"id", id},
                                {"page", page},
                                {"dpi", dpi},
                                {"metrics", json::parse(m.toJson())},
                                {"within", ok}});
                if (!same && !outDir.empty()) {
                    const fs::path name = ot::paths::fromUtf8(png).filename();
                    ot::savePng(mu, actual, outDir / name);
                    ot::savePng(mu, diff, outDir / fs::path(name).replace_extension(L".diff.png"));
                }
            }
        } catch (const std::exception& e) {
            std::printf("FAIL %s: %s\n", id.c_str(), e.what());
            ++failures;
        }
    }
    std::printf(
        "check: растров %d — одинаковых %d, в порогах %d, за порогом %d; пропущено %d, ошибок %d; худшие: "
        "max_delta %d, MSE %.6f, wMSE %.6f, SSIM %.6f\n",
        renders, identical, within, outside, skipped, failures, worstDelta, worstMse, worstWmse, worstSsim);
    if (!jsonOut.empty()) {
        saveJson(jsonOut,
                 {{"mupdf", ot::MuContext::version()},
#ifdef NDEBUG
                  {"build", "release"},
#else
                  {"build", "debug"},
#endif
                  {"renders", renders},
                  {"identical", identical},
                  {"within", within},
                  {"outside", outside},
                  {"skipped", skipped},
                  {"failures", failures},
                  {"worst",
                   {{"max_delta", worstDelta}, {"mse", worstMse}, {"wmse", worstWmse}, {"ssim", worstSsim}}},
                  {"items", rows}});
    }
    return failures == 0 && outside == 0 ? 0 : 1;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(ot::paths::narrow(argv[i]));
    }
    std::string mode, reason, markId, dpi = "72,150";
    std::set<std::string> only;
    fs::path filesJson = root() / L"tests" / L"render" / L"files.json";
    fs::path outDir, jsonOut;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&]() -> std::string { return i + 1 < args.size() ? args[++i] : std::string(); };
        if (a == "--update" || a == "--check") {
            mode = a;
        } else if (a == "--mark-release-only") {
            mode = a;
            markId = value();
        } else if (a == "--reason") {
            reason = value();
        } else if (a == "--files") {
            filesJson = ot::paths::fromUtf8(value());
        } else if (a == "--only") {
            const std::string list = value();
            size_t start = 0;
            while (start <= list.size()) {
                const size_t comma = list.find(',', start);
                const std::string id =
                    list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!id.empty()) {
                    only.insert(id);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
        } else if (a == "--dpi") {
            dpi = value();
        } else if (a == "--out") {
            outDir = ot::paths::fromUtf8(value());
        } else if (a == "--json") {
            jsonOut = ot::paths::fromUtf8(value());
        } else {
            mode.clear();
            break;
        }
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifndef NDEBUG
    // Эталоны снимаются только Release-сборкой (эталон 9.3): Debug MuPDF лишь сверяется с ними.
    if (mode == "--update" || mode == "--mark-release-only") {
        std::fprintf(stderr, "render_refs: %s — только из build/release (это Debug-сборка)\n", mode.c_str());
        return 2;
    }
#endif
    try {
        if (mode == "--update" && !reason.empty()) {
            return update(filesJson, parseDpi(dpi), reason, only);
        }
        if (mode == "--mark-release-only" && !markId.empty() && !reason.empty()) {
            return markReleaseOnly(markId, reason);
        }
        if (mode == "--check") {
            return check(outDir, jsonOut);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "render_refs: %s\n", e.what());
        return 1;
    }
    std::fprintf(stderr,
                 "usage: render_refs --update --reason \"…\" [--only id,id] [--files f.json] [--dpi 72,150]\n"
                 "       render_refs --mark-release-only <id> --reason \"…\"\n"
                 "       render_refs --check [--out <dir>] [--json <file>]\n");
    return 2;
}
