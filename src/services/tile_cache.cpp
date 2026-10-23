#include "services/tile_cache.h"

#include <QMutexLocker>

#include <algorithm>

namespace ot {

size_t qHash(const TileKey& key, size_t seed) noexcept { return hashValue(key) ^ seed; }

TileCache::TileCache(qint64 maxBytes) { applyLimit(maxBytes); }

void TileCache::applyLimit(qint64 maxBytes) {
    maxBytes_ = std::max<qint64>(maxBytes, 0);
    const qint64 previews = maxBytes_ / kPreviewDivisor;
    previews_.setMaxCost(previews);
    cache_.setMaxCost(maxBytes_ - previews);
}

qint64 TileCache::limitFromMegabytes(int megabytes) noexcept {
    return static_cast<qint64>(std::clamp(megabytes, kMinMegabytes, kMaxMegabytes)) * 1024 * 1024;
}

bool TileCache::insertPreview(const TileKey& key, const QImage& image) {
    if (image.isNull()) {
        return false;
    }
    const qsizetype cost = image.sizeInBytes();
    const QMutexLocker lock(&mutex_);
    if (cost > previews_.maxCost()) {
        return false;
    }
    return previews_.insert(key, new QImage(image), cost);
}

std::optional<QImage> TileCache::findPreview(const TileKey& key) {
    const QMutexLocker lock(&mutex_);
    if (const QImage* image = previews_.object(key)) {
        return *image;
    }
    return std::nullopt;
}

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
    for (QCache<TileKey, QImage>* c : {&cache_, &previews_}) {
        for (const TileKey& key : c->keys()) {
            if (key.doc == doc) {
                removed += c->remove(key) ? 1 : 0;
            }
        }
    }
    return removed;
}

int TileCache::removePage(std::uint64_t doc, int page) {
    const QMutexLocker lock(&mutex_);
    int removed = 0;
    for (QCache<TileKey, QImage>* c : {&cache_, &previews_}) {
        for (const TileKey& key : c->keys()) {
            if (key.doc == doc && key.page == page) {
                removed += c->remove(key) ? 1 : 0;
            }
        }
    }
    return removed;
}

void TileCache::clear() {
    const QMutexLocker lock(&mutex_);
    cache_.clear();
    previews_.clear();
}

qint64 TileCache::maxBytes() const {
    const QMutexLocker lock(&mutex_);
    return maxBytes_;
}

void TileCache::setMaxBytes(qint64 maxBytes) {
    const QMutexLocker lock(&mutex_);
    applyLimit(maxBytes);
}

qint64 TileCache::tileBudget() const {
    const QMutexLocker lock(&mutex_);
    return cache_.maxCost();
}

qint64 TileCache::previewBudget() const {
    const QMutexLocker lock(&mutex_);
    return previews_.maxCost();
}

qint64 TileCache::totalBytes() const {
    const QMutexLocker lock(&mutex_);
    return cache_.totalCost() + previews_.totalCost();
}

qint64 TileCache::previewBytes() const {
    const QMutexLocker lock(&mutex_);
    return previews_.totalCost();
}

int TileCache::count() const {
    const QMutexLocker lock(&mutex_);
    return static_cast<int>(cache_.count());
}

int TileCache::previewCount() const {
    const QMutexLocker lock(&mutex_);
    return static_cast<int>(previews_.count());
}

void TileCache::shrink(int percent) {
    const QMutexLocker lock(&mutex_);
    const qint64 p = std::clamp(percent, 0, 100);
    // Временно уменьшенный предел вытесняет давнее; затем предел возвращается.
    for (QCache<TileKey, QImage>* c : {&cache_, &previews_}) {
        const qint64 budget = c->maxCost();
        c->setMaxCost(budget * p / 100);
        c->setMaxCost(budget);
    }
}

}  // namespace ot
