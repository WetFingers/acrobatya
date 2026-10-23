// imgcmp — сравнение двух PNG (эталон 9.3):
//   imgcmp a.png b.png [--fuzzy max_delta,mse,wmse,ssim] [--diff out.png] [--cross]
// Печатает JSON с метриками. Код: 0 — в пределах порога, 1 — нет, 2 — ошибка.
// --cross — мягкий режим для сравнения с другим движком (pdfium в compat): размеры могут отличаться
// на 1 px, проверяются SSIM ≥ 0.8 и IoU масок «не фон» ≥ 0.7.
#include <algorithm>
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
    std::fprintf(stderr, "usage: imgcmp a.png b.png [--fuzzy d,mse,wmse,ssim] [--diff out.png] [--cross]\n");
    return 2;
}

bool parseFuzzy(const std::string& s, ot::FuzzyThreshold& t) {
    double v[4] = {static_cast<double>(t.maxDelta), t.mse, t.windowedMse, t.ssim};
    size_t start = 0;
    for (int i = 0; i < 4 && start <= s.size(); ++i) {
        const size_t comma = s.find(',', start);
        const std::string part =
            s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (!part.empty()) {
            v[i] = std::stod(part);
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    t.maxDelta = static_cast<int>(v[0]);
    t.mse = v[1];
    t.windowedMse = v[2];
    t.ssim = v[3];
    return true;
}

double maskIoU(const ot::Bitmap& a, const ot::Bitmap& b) {
    const int w = std::min(a.width, b.width);
    const int h = std::min(a.height, b.height);
    long long inter = 0, uni = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto ink = [](const ot::Bitmap& m, int px, int py) {
                const std::uint8_t* p = m.row(py) + static_cast<ptrdiff_t>(px) * m.components;
                return (p[0] + p[m.components > 1 ? 1 : 0] + p[m.components > 2 ? 2 : 0]) < 3 * 240;
            };
            const bool ia = ink(a, x, y), ib = ink(b, x, y);
            inter += (ia && ib) ? 1 : 0;
            uni += (ia || ib) ? 1 : 0;
        }
    }
    return uni == 0 ? 1.0 : static_cast<double>(inter) / static_cast<double>(uni);
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::vector<std::wstring> files;
    ot::FuzzyThreshold fuzzy;
    std::wstring diffPath;
    bool cross = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--fuzzy" && i + 1 < argc) {
            parseFuzzy(ot::paths::narrow(argv[++i]), fuzzy);
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
            const double iou = maskIoU(a, b);
            ot::Bitmap ca = a, cb = b;
            const int w = std::min(a.width, b.width), h = std::min(a.height, b.height);
            ca.width = cb.width = w;
            ca.height = cb.height = h;
            const auto r = ot::compareBitmaps(ca, cb);
            const bool ok = std::abs(a.width - b.width) <= 1 && std::abs(a.height - b.height) <= 1 &&
                            r.ssim >= 0.8 && iou >= 0.7;
            std::printf("{\"mode\":\"cross\",\"iou\":%.4f,\"metrics\":%s,\"ok\":%s}\n", iou,
                        r.toJson().c_str(), ok ? "true" : "false");
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
