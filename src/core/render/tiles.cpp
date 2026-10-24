#include "core/render/tiles.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

namespace {

fz_matrix tilesFz(const Matrix& m) { return {m.a, m.b, m.c, m.d, m.e, m.f}; }

// Масштаб растра: zoom · dpr в тысячных — как в TileKey.
float scaleOf(float zoom, float dpr) noexcept {
    return static_cast<float>(toMilli(zoom)) / 1000.0f * (static_cast<float>(toMilli(dpr)) / 1000.0f);
}

// splitmix64: перемешивание полей ключа.
std::uint64_t mix(std::uint64_t h, std::uint64_t v) noexcept {
    std::uint64_t z = h ^ (v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2));
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::uint64_t asBits(int v) noexcept { return static_cast<std::uint64_t>(static_cast<std::uint32_t>(v)); }

}  // namespace

std::size_t hashValue(const TileKey& k) noexcept {
    std::uint64_t h = 0x84222325cbf29ce4ULL;
    h = mix(h, k.doc);
    h = mix(h, asBits(k.page));
    h = mix(h, asBits(k.zoomBucket));
    h = mix(h, asBits(k.zoomMilli));
    h = mix(h, asBits(k.dprMilli));
    h = mix(h, asBits(k.rotation));
    h = mix(h, (asBits(k.tx) << 32) | asBits(k.ty));
    h = mix(h, k.layersHash);
    h = mix(h, k.style);
    h = mix(h, k.revision);
    h = mix(h, k.annotsHidden);
    return static_cast<std::size_t>(h);
}

int toMilli(float value) noexcept {
    if (!(value > 0.0f) || !std::isfinite(value)) {
        return 1;
    }
    return std::max(1, static_cast<int>(std::lround(static_cast<double>(value) * 1000.0)));
}

int zoomBucket(float zoom) noexcept {
    const double z = zoom > 0.001f && std::isfinite(zoom) ? zoom : 0.001;
    return static_cast<int>(std::lround(8.0 * std::log2(z)));
}

float bucketZoom(int bucket) noexcept { return static_cast<float>(std::exp2(bucket / 8.0)); }

int normalizeRotation(int degrees) noexcept {
    const int d = ((degrees % 360) + 360) % 360;
    return (d + 45) / 90 * 90 % 360;
}

Matrix pageMatrix(const RectF& pageRect, float zoom, float dpr, int rotation) noexcept {
    const float s = scaleOf(zoom, dpr);
    const Matrix m =
        concat(Matrix::scale(s), Matrix::rotate(static_cast<float>(normalizeRotation(rotation))));
    const fz_rect r =
        fz_transform_rect(fz_make_rect(pageRect.x0, pageRect.y0, pageRect.x1, pageRect.y1), tilesFz(m));
    return concat(m, Matrix::translate(-r.x0, -r.y0));
}

IRect pageDeviceRect(const RectF& pageRect, float zoom, float dpr, int rotation) noexcept {
    if (pageRect.empty()) {
        return {};
    }
    const Matrix m = pageMatrix(pageRect, zoom, dpr, rotation);
    // Как Renderer::deviceBounds: fz_round_rect(fz_transform_rect(bound, ctm)).
    const fz_irect b = fz_round_rect(
        fz_transform_rect(fz_make_rect(pageRect.x0, pageRect.y0, pageRect.x1, pageRect.y1), tilesFz(m)));
    return {b.x0, b.y0, b.x1, b.y1};
}

std::vector<TileRect> tilesIntersecting(const IRect& device, const IRect& visible, int tileSize) {
    std::vector<TileRect> out;
    const IRect v{std::max(device.x0, visible.x0), std::max(device.y0, visible.y0),
                  std::min(device.x1, visible.x1), std::min(device.y1, visible.y1)};
    if (device.empty() || v.empty() || tileSize <= 0) {
        return out;
    }
    const int tx0 = (v.x0 - device.x0) / tileSize;
    const int tx1 = (v.x1 - 1 - device.x0) / tileSize;
    const int ty0 = (v.y0 - device.y0) / tileSize;
    const int ty1 = (v.y1 - 1 - device.y0) / tileSize;
    out.reserve(static_cast<size_t>(tx1 - tx0 + 1) * static_cast<size_t>(ty1 - ty0 + 1));
    for (int ty = ty0; ty <= ty1; ++ty) {
        for (int tx = tx0; tx <= tx1; ++tx) {
            const int x0 = device.x0 + tx * tileSize;
            const int y0 = device.y0 + ty * tileSize;
            out.push_back(
                {tx, ty, {x0, y0, std::min(x0 + tileSize, device.x1), std::min(y0 + tileSize, device.y1)}});
        }
    }
    return out;
}

std::vector<TileRect> tileRectsFor(const RectF& pageRect, float zoom, float dpr, int rotation, int tileSize) {
    const IRect device = pageDeviceRect(pageRect, zoom, dpr, rotation);
    return tilesIntersecting(device, device, tileSize);
}

TileKey makeTileKey(std::uint64_t doc, int page, float zoom, float dpr, int rotation, const TileRect& tile,
                    std::uint64_t revision, std::uint64_t layersHash, std::uint64_t style,
                    std::uint32_t annotsHidden) noexcept {
    TileKey k;
    k.doc = doc;
    k.page = page;
    k.zoomBucket = zoomBucket(zoom);
    k.zoomMilli = toMilli(zoom);
    k.dprMilli = toMilli(dpr);
    k.rotation = normalizeRotation(rotation);
    k.tx = tile.tx;
    k.ty = tile.ty;
    k.layersHash = layersHash;
    k.style = style;
    k.revision = revision;
    k.annotsHidden = annotsHidden;
    return k;
}

float previewZoom(float zoom) noexcept {
    return bucketZoom(zoomBucket(zoom)) / static_cast<float>(kPreviewDivisor);
}

TileKey makePreviewKey(std::uint64_t doc, int page, float zoom, float dpr, int rotation,
                       std::uint64_t revision, std::uint64_t layersHash, std::uint64_t style,
                       std::uint32_t annotsHidden) noexcept {
    TileKey k = makeTileKey(doc, page, previewZoom(zoom), dpr, rotation, TileRect{-1, -1, {}}, revision,
                            layersHash, style, annotsHidden);
    k.zoomBucket = zoomBucket(zoom);
    return k;
}

}  // namespace ot
