// imgcmp — сравнение двух PNG (эталон 9.3):
//   imgcmp a.png b.png [--fuzzy max_delta,mse,wmse,ssim] [--diff out.png]
//   imgcmp a.png b.png --cross [--region x0,y0,x1,y1]… [--cross-limits min_ink,ssim,iou]
// Печатает JSON с метриками. Код: 0 — в пределах порога, 1 — нет, 2 — ошибка.
// Обычный режим — регресс одного движка (Debug ↔ Release): размеры, отличающиеся пиксели, max delta,
// MSE, wMSE, SSIM; порог по умолчанию 3, 0.05, 15, 0.98 (пустая часть --fuzzy — значение по умолчанию,
// ssim 0 — не проверять). --diff — карта отличий (|delta| × 16 по каналу).
// --cross — сравнение с другим движком (pdfium в compat): размеры могут отличаться на 1 px; в каждой
// области --region (нет областей — вся общая часть) оба растра непустые (≥ 50 пикселей «не фон»),
// SSIM по серому ≥ 0.8, IoU масок «не фон» ≥ 0.7.
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/util/paths.h"

namespace {

int usage() {
    std::fprintf(
        stderr,
        "usage: imgcmp a.png b.png [--fuzzy d,mse,wmse,ssim] [--diff out.png]\n"
        "       imgcmp a.png b.png --cross [--region x0,y0,x1,y1]... [--cross-limits ink,ssim,iou]\n");
    return 2;
}

// "1,2,,4" → значения по позициям; пустая часть оставляет прежнее.
bool parseList(const std::string& s, std::vector<double>& v) {
    size_t start = 0;
    for (size_t i = 0; i < v.size() && start <= s.size(); ++i) {
        const size_t comma = s.find(',', start);
        const std::string part =
            s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!part.empty()) {
            try {
                v[i] = std::stod(part);
            } catch (const std::exception&) {
                return false;
            }
        }
        if (comma == std::string::npos) {
            return true;
        }
        start = comma + 1;
    }
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> files;
    ot::FuzzyThreshold fuzzy;
    ot::CrossThreshold crossLimits;
    std::vector<ot::IRect> regions;
    std::wstring diffPath;
    bool cross = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--fuzzy" && i + 1 < argc) {
            std::vector<double> v = {static_cast<double>(fuzzy.maxDelta), fuzzy.mse, fuzzy.windowedMse,
                                     fuzzy.ssim};
            if (!parseList(ot::paths::narrow(argv[++i]), v)) {
                return usage();
            }
            fuzzy = {static_cast<int>(v[0]), v[1], v[2], v[3]};
        } else if (a == L"--cross-limits" && i + 1 < argc) {
            std::vector<double> v = {static_cast<double>(crossLimits.minInk), crossLimits.ssim,
                                     crossLimits.iou};
            if (!parseList(ot::paths::narrow(argv[++i]), v)) {
                return usage();
            }
            crossLimits.minInk = static_cast<int>(v[0]);
            crossLimits.ssim = v[1];
            crossLimits.iou = v[2];
        } else if (a == L"--region" && i + 1 < argc) {
            std::vector<double> v = {0, 0, 0, 0};
            if (!parseList(ot::paths::narrow(argv[++i]), v)) {
                return usage();
            }
            regions.push_back({static_cast<int>(v[0]), static_cast<int>(v[1]), static_cast<int>(v[2]),
                               static_cast<int>(v[3])});
        } else if (a == L"--diff" && i + 1 < argc) {
            diffPath = argv[++i];
        } else if (a == L"--cross") {
            cross = true;
        } else if (a.rfind(L"--", 0) == 0) {
            return usage();
        } else {
            files.push_back(a);
        }
    }
    if (files.size() != 2) {
        return usage();
    }
    try {
        ot::MuContext mu;
        const ot::Bitmap a = ot::loadPng(mu, files[0]);
        const ot::Bitmap b = ot::loadPng(mu, files[1]);
        if (cross) {
            if (regions.empty()) {
                regions.push_back({});
            }
            bool ok = true;
            std::string items;
            for (const ot::IRect& region : regions) {
                const ot::CrossResult r = ot::crossCompare(a, b, region, crossLimits);
                ok = ok && r.ok;
                items += (items.empty() ? "" : ",") + r.toJson();
            }
            std::printf("{\"mode\":\"cross\",\"regions\":[%s],\"ok\":%s}\n", items.c_str(),
                        ok ? "true" : "false");
            return ok ? 0 : 1;
        }
        ot::Bitmap diff;
        const auto r = ot::compareBitmaps(a, b, diffPath.empty() ? nullptr : &diff);
        if (!diffPath.empty() && !diff.empty()) {
            ot::savePng(mu, diff, diffPath);
        }
        const bool ok = r.within(fuzzy);
        std::printf("{\"metrics\":%s,\"ok\":%s}\n", r.toJson().c_str(), ok ? "true" : "false");
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "imgcmp: %s\n", e.what());
        return 2;
    }
}
