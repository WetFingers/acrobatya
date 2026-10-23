#include "core/render/image_compare.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include <fmt/format.h>

namespace ot {

namespace {

constexpr int kWindow = 8;
constexpr int kSsimStep = 4;

int channel(const Bitmap& b, int x, int y, int c) {
    const std::uint8_t* p = b.row(y) + static_cast<ptrdiff_t>(x) * b.components;
    return b.components == 1 ? p[0] : p[c];
}

double gray(const Bitmap& b, int x, int y) {
    if (b.components == 1) {
        return b.row(y)[x];
    }
    const std::uint8_t* p = b.row(y) + static_cast<ptrdiff_t>(x) * b.components;
    return 0.299 * p[0] + 0.587 * p[1] + 0.114 * p[2];
}

double ssimWindow(const Bitmap& a, const Bitmap& b, int x0, int y0, int w, int h) {
    constexpr double c1 = (0.01 * 255) * (0.01 * 255);
    constexpr double c2 = (0.03 * 255) * (0.03 * 255);
    double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
    const double n = static_cast<double>(w) * h;
    for (int y = y0; y < y0 + h; ++y) {
        for (int x = x0; x < x0 + w; ++x) {
            const double va = gray(a, x, y);
            const double vb = gray(b, x, y);
            sa += va;
            sb += vb;
            saa += va * va;
            sbb += vb * vb;
            sab += va * vb;
        }
    }
    const double ma = sa / n, mb = sb / n;
    const double va = saa / n - ma * ma, vb = sbb / n - mb * mb, cov = sab / n - ma * mb;
    return ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
}

}  // namespace

bool CompareResult::within(const FuzzyThreshold& t) const {
    // Все условия сразу (эталон 9.3): max_delta ∧ MSE ∧ wMSE ∧ (SSIM, если задан).
    return sameSize && maxDelta <= t.maxDelta && mse <= t.mse && windowedMse <= t.windowedMse &&
           (t.ssim <= 0.0 || ssim >= t.ssim);
}

std::string CompareResult::toJson() const {
    return fmt::format(
        R"({{"same_size":{},"width":{},"height":{},"differing_pixels":{},"max_delta":{},"mse":{:.6f},"wmse":{:.6f},"ssim":{:.6f}}})",
        sameSize ? "true" : "false", width, height, differingPixels, maxDelta, mse, windowedMse, ssim);
}

CompareResult compareBitmaps(const Bitmap& a, const Bitmap& b, Bitmap* diff) {
    CompareResult r;
    r.width = a.width;
    r.height = a.height;
    r.sameSize = a.width == b.width && a.height == b.height && !a.empty();
    if (!r.sameSize) {
        r.ssim = 0.0;
        return r;
    }
    if (diff != nullptr) {
        diff->width = a.width;
        diff->height = a.height;
        diff->components = 3;
        diff->stride = a.width * 3;
        diff->pixels.assign(static_cast<size_t>(diff->stride) * static_cast<size_t>(a.height), 0);
    }
    const int wx = (a.width + kWindow - 1) / kWindow;
    const int wy = (a.height + kWindow - 1) / kWindow;
    std::vector<double> windowSum(static_cast<size_t>(wx) * static_cast<size_t>(wy), 0.0);
    std::vector<int> windowCount(windowSum.size(), 0);
    double total = 0.0;
    for (int y = 0; y < a.height; ++y) {
        for (int x = 0; x < a.width; ++x) {
            int pixelMax = 0;
            double pixelSq = 0.0;
            for (int c = 0; c < 3; ++c) {
                const int d = std::abs(channel(a, x, y, c) - channel(b, x, y, c));
                pixelMax = std::max(pixelMax, d);
                pixelSq += static_cast<double>(d) * d;
                if (diff != nullptr) {
                    diff->row(y)[x * 3 + c] = static_cast<std::uint8_t>(std::min(255, d * 16));
                }
            }
            if (pixelMax > 0) {
                ++r.differingPixels;
            }
            r.maxDelta = std::max(r.maxDelta, pixelMax);
            total += pixelSq / 3.0;
            const size_t w =
                static_cast<size_t>(y / kWindow) * static_cast<size_t>(wx) + static_cast<size_t>(x / kWindow);
            windowSum[w] += pixelSq / 3.0;
            ++windowCount[w];
        }
    }
    r.mse = total / (static_cast<double>(a.width) * a.height);
    for (size_t i = 0; i < windowSum.size(); ++i) {
        if (windowCount[i] > 0) {
            r.windowedMse = std::max(r.windowedMse, windowSum[i] / windowCount[i]);
        }
    }
    if (a.width >= kWindow && a.height >= kWindow) {
        double sum = 0.0;
        int count = 0;
        for (int y = 0; y + kWindow <= a.height; y += kSsimStep) {
            for (int x = 0; x + kWindow <= a.width; x += kSsimStep) {
                sum += ssimWindow(a, b, x, y, kWindow, kWindow);
                ++count;
            }
        }
        r.ssim = count > 0 ? sum / count : 1.0;
    }
    return r;
}

}  // namespace ot
