#include "core/ocr/tessdata.h"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <system_error>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/fitz.h>

// source/fitz/tessocr.h MuPDF (внутренний заголовок; функции с C-связыванием в libmupdf.lib): Tesseract с
// памятью Leptonica через контекст MuPDF — как у fz_new_ocr_device.
void* ocr_init(fz_context* ctx, const char* lang, const char* datadir, fz_options* options);
void ocr_fin(fz_context* ctx, void* api);
}

namespace ot::ocr {

namespace fs = std::filesystem;

std::vector<std::string> tessdataLanguages(const fs::path& dir) {
    std::vector<std::string> langs;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == L".traineddata") {
            langs.push_back(paths::toUtf8(e.path().stem()));
        }
    }
    std::sort(langs.begin(), langs.end());
    return langs;
}

fs::path findTessdata(const fs::path& exeDir) {
    const fs::path local = exeDir / L"tessdata";
    if (!tessdataLanguages(local).empty()) {
        return local;
    }
    wchar_t* env = nullptr;
    size_t len = 0;
    if (_wdupenv_s(&env, &len, L"TESSDATA_PREFIX") == 0 && env != nullptr) {
        const fs::path prefix(env);
        std::free(env);
        if (!tessdataLanguages(prefix).empty()) {
            return prefix;
        }
    }
    return {};
}

TessdataProbe probeTessdata(const fs::path& dir, const std::string& lang) {
    TessdataProbe probe;
    probe.dir = dir;
    if (dir.empty()) {
        probe.error = "tessdata not found (next to the program or TESSDATA_PREFIX)";
        return probe;
    }
    probe.languages = tessdataLanguages(dir);
    if (std::find(probe.languages.begin(), probe.languages.end(), lang) == probe.languages.end()) {
        probe.error = lang + ".traineddata is missing in " + paths::toUtf8(dir);
        return probe;
    }
    // Tesseract открывает файлы узким fopen: путь — короткое имя 8.3 (ASCII), с разделителем в конце.
    std::string datadir = paths::toUtf8(fs::path(paths::toShortPath(dir.wstring())));
    if (!datadir.empty() && datadir.back() != '\\' && datadir.back() != '/') {
        datadir += '\\';
    }
    try {
        MuContext mu;  // свой корень: проба не трогает общий контекст программы
        fz_context* ctx = mu.get();
        void* api = nullptr;
        mu::call(ctx, [&] { api = ocr_init(ctx, lang.c_str(), datadir.c_str(), nullptr); });
        mu::call(ctx, [&] { ocr_fin(ctx, api); });
        probe.ok = true;
    } catch (const std::exception& e) {
        probe.error = std::string("Tesseract cannot load ") + lang + ": " + e.what();
    }
    return probe;
}

}  // namespace ot::ocr
