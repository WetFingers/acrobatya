// Тайлы страницы и ключ кеша растров (эталон 4.2, HiDPI — 6.1). Одна матрица вида
//     ctm = scale(zoom · dpr) × rotate(view) × translate(−x0, −y0)
// (x0, y0 — левый верхний угол повёрнутой рамки) переводит пункты страницы в пиксели устройства так, что
// рамка страницы начинается в (0, 0); ею же пользуются рендер, хит-тест и выделение. Тайлы 512 × 512 device
// px режут рамку по целым пикселям устройства: углы целые, тайлы не перекрываются и покрывают рамку ровно
// (как fz_round_rect у Renderer::deviceBounds). Масштаб и dpr округляются до тысячных — так же, как в
// TileKey: растр однозначно задан ключом.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/util/geometry.h"

namespace ot {

inline constexpr int kTileSize = 512;

struct TileKey {
    std::uint64_t doc = 0;  // DocumentSession::id()
    int page = 0;
    int zoomBucket = 0;    // round(8 · log2(zoom)) — шаг 1/8 октавы: ближний масштаб для временной подмены
    int zoomMilli = 1000;  // zoom × 1000 — точный масштаб растра
    int dprMilli = 1000;   // devicePixelRatio × 1000
    int rotation = 0;      // поворот вида 0/90/180/270 (сверх /Rotate)
    int tx = 0;            // номер тайла по x
    int ty = 0;            // номер тайла по y
    std::uint64_t layersHash = 0;    // видимость слоёв OCG
    std::uint64_t style = 0;         // RenderStyle::key() (renderer.h): замена цветов, сглаживание, overprint
    std::uint64_t revision = 0;      // DocumentSession::revision()
    std::uint32_t annotsHidden = 0;  // скрытые подтипы аннотаций (бит — AnnotType; 5.1 «Показать/скрыть»)

    bool operator==(const TileKey&) const = default;
};

std::size_t hashValue(const TileKey& key) noexcept;

struct TileKeyHash {
    std::size_t operator()(const TileKey& key) const noexcept { return hashValue(key); }
};

// Тысячные (масштаб, dpr): round(v · 1000), не меньше 1.
int toMilli(float value) noexcept;
// round(8 · log2(zoom)); zoom ≤ 0 — как 1/1000.
int zoomBucket(float zoom) noexcept;
// 2^(bucket / 8).
float bucketZoom(int bucket) noexcept;
// Поворот к 0, 90, 180, 270.
int normalizeRotation(int degrees) noexcept;

// Матрица вида (см. выше); pageRect — рамка страницы в пунктах (fz_bound_page, DisplayList::bounds()).
Matrix pageMatrix(const RectF& pageRect, float zoom, float dpr, int rotation = 0) noexcept;
// Рамка страницы в пикселях устройства при pageMatrix: (0, 0, w, h).
IRect pageDeviceRect(const RectF& pageRect, float zoom, float dpr, int rotation = 0) noexcept;

struct TileRect {
    int tx = 0;
    int ty = 0;
    IRect rect;  // device px, [x0, x1) × [y0, y1)
    bool operator==(const TileRect&) const = default;
};

// Все тайлы страницы построчно (ty, затем tx).
std::vector<TileRect> tileRectsFor(const RectF& pageRect, float zoom, float dpr, int rotation = 0,
                                   int tileSize = kTileSize);
// Тайлы рамки device (pageDeviceRect), пересекающие видимую область visible (те же device px).
std::vector<TileRect> tilesIntersecting(const IRect& device, const IRect& visible, int tileSize = kTileSize);

// Превью страницы (эталон 4.2): вся страница на 1/8 масштаба корзины zoomBucket — пока тайлов нет, его
// растягивают. Одно превью на 1/8 октавы: плавный зум не рисует новое на каждом шаге. Ключ — TileKey с
// tx = ty = −1 и zoomMilli превью; кеш — отдельный (TileCache::insertPreview).
inline constexpr int kPreviewDivisor = 8;
float previewZoom(float zoom) noexcept;
TileKey makePreviewKey(std::uint64_t doc, int page, float zoom, float dpr, int rotation,
                       std::uint64_t revision = 0, std::uint64_t layersHash = 0, std::uint64_t style = 0,
                       std::uint32_t annotsHidden = 0) noexcept;

// Ключ тайла: масштаб и dpr — в тысячных, поворот — нормализован.
TileKey makeTileKey(std::uint64_t doc, int page, float zoom, float dpr, int rotation, const TileRect& tile,
                    std::uint64_t revision = 0, std::uint64_t layersHash = 0, std::uint64_t style = 0,
                    std::uint32_t annotsHidden = 0) noexcept;

}  // namespace ot
