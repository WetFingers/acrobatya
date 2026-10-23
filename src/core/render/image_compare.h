// Сравнение растров для эталонов рендера (эталон 9.3) — метрики как pdfium_diff --metrics:
// размеры, число отличающихся пикселей, max delta по каналу, MSE, windowed MSE (окно 8×8, максимум
// по окнам), SSIM по серому (окно 8×8, шаг 4). Порог по умолчанию — max_delta ≤ 3 ∧ MSE ≤ 0.05 ∧
// wMSE ≤ 15; для текста ещё SSIM ≥ 0.98. Только для регресса одного движка (Debug ↔ Release).
// Сравнение с другим движком (pdfium в compat) — crossCompare: в области оба растра непустые (≥ 50 пикселей
// «не фон»), SSIM по серому ≥ 0.8, IoU масок «не фон» ≥ 0.7 (другое сглаживание, глифы и толщина линий).
#pragma once

#include <string>

#include "core/render/bitmap.h"
#include "core/util/geometry.h"

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

// Часть растра (область обрезается границами растра; пустое пересечение — пустой растр).
Bitmap cropBitmap(const Bitmap& src, const IRect& area);

struct CrossThreshold {
    int minInk = 50;        // пикселей «не фон» в области у каждого растра
    double ssim = 0.8;      // SSIM по серому в области
    double iou = 0.7;       // пересечение / объединение масок «не фон»
    int sizeTolerance = 1;  // допустимая разница размеров растров, пикселей
    int background = 240;   // «фон» — все каналы ≥ background
};

struct CrossResult {
    bool ok = false;
    bool sizeOk = false;
    IRect area;  // проверенная область (общая часть растров ∩ заданная)
    long long inkA = 0, inkB = 0;
    double ssim = 0.0;
    double iou = 0.0;

    std::string toJson() const;
};

// Мягкое сравнение растров разных движков в области area (пустая — вся общая часть).
CrossResult crossCompare(const Bitmap& a, const Bitmap& b, const IRect& area = {},
                         const CrossThreshold& t = {});

}  // namespace ot
