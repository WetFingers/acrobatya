// Сравнение растров для эталонов рендера (эталон 9.3) — метрики как pdfium_diff --metrics:
// размеры, число отличающихся пикселей, max delta по каналу, MSE, windowed MSE (окно 8×8, максимум
// по окнам), SSIM по серому (окно 8×8, шаг 4). Порог по умолчанию — max_delta ≤ 3 ∧ MSE ≤ 0.05 ∧
// wMSE ≤ 15; для текста ещё SSIM ≥ 0.98. Только для регресса одного движка (Debug ↔ Release).
#pragma once

#include <string>

#include "core/render/bitmap.h"

namespace ot {

struct FuzzyThreshold {
    int maxDelta = 3;
    double mse = 0.05;
    double windowedMse = 15.0;
    double ssim = 0.98;  // 0 — не проверять
};

struct CompareResult {
    bool sameSize = false;
    int width = 0;
    int height = 0;
    long long differingPixels = 0;
    int maxDelta = 0;
    double mse = 0.0;
    double windowedMse = 0.0;
    double ssim = 1.0;

    bool within(const FuzzyThreshold& t) const;
    std::string toJson() const;
};

// Сравнить два RGB-растра (серый приводится к RGB). diff (если не nullptr) — карта отличий:
// чёрный — совпало, яркость — |delta| ×16 по каналу.
CompareResult compareBitmaps(const Bitmap& a, const Bitmap& b, Bitmap* diff = nullptr);

}  // namespace ot
