#include "services/tile_cache.h"

#include <QMutexLocker>

namespace ot {

size_t qHash(const TileKey& key, size_t seed) noexcept { return hashValue(key) ^ seed; }

TileCache::TileCache(qint64 maxBytes) { cache_.setMaxCost(maxBytes); }

bool TileCache::insert(const TileKey& key, const QImage& image) {
    if (image.isNull()) {
        return false;
    }
    const qsizetype cost = image.sizeInBytes();
    const QMutexLocker lock(&mutex_);
    if (cost > cache_.maxCost()) {
        return false;
    }
    return cache_.insert(key, new QImage(image), cost);
}

std::optional<QImage> TileCache::find(const TileKey& key) {
    const QMutexLocker lock(&mutex_);
    if (const QImage* image = cache_.object(key)) {
        return *image;
    }
    return std::nullopt;
}

bool TileCache::contains(const TileKey& key) const {
    const QMutexLocker lock(&mutex_);
    return cache_.contains(key);
}

bool TileCache::remove(const TileKey& key) {
    const QMutexLocker lock(&mutex_);
    return cache_.remove(key);
}

int TileCache::removeDocument(std::uint64_t doc) {
    const QMutexLocker lock(&mutex_);
    int removed = 0;
    for (const TileKey& key : cache_.keys()) {
        if (key.doc == doc) {
            removed += cache_.remove(key) ? 1 : 0;
        }
    }
    return removed;
}

int TileCache::removePage(std::uint64_t doc, int page) {
    const QMutexLocker lock(&mutex_);
    int removed = 0;
    for (const TileKey& key : cache_.keys()) {
        if (key.doc == doc && key.page == page) {
            removed += cache_.remove(key) ? 1 : 0;
        }
    }
    return removed;
}

void TileCache::clear() {
    const QMutexLocker lock(&mutex_);
    cache_.clear();
}

qint64 TileCache::maxBytes() const {
    const QMutexLocker lock(&mutex_);
    return cache_.maxCost();
}

void TileCache::setMaxBytes(qint64 maxBytes) {
    const QMutexLocker lock(&mutex_);
    cache_.setMaxCost(maxBytes);
}

qint64 TileCache::totalBytes() const {
    const QMutexLocker lock(&mutex_);
    return cache_.totalCost();
}

int TileCache::count() const {
    const QMutexLocker lock(&mutex_);
    return static_cast<int>(cache_.count());
}

}  // namespace ot
