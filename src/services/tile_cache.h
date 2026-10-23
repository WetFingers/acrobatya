// TileCache — растры тайлов (эталон 4.1–4.2): QCache<TileKey, QImage> с лимитом в байтах (по умолчанию 384
// МБ, настройка), cost = QImage::sizeInBytes(); вытесняется самое давнее. Общий на процесс; методы — из
// любого потока (под мьютексом). Растр больше лимита не кладётся.
#pragma once

#include <QCache>
#include <QImage>
#include <QMutex>

#include <cstdint>
#include <optional>

#include "core/render/tiles.h"

namespace ot {

// Хеш ключа для QCache/QHash (ADL).
size_t qHash(const TileKey& key, size_t seed = 0) noexcept;

class TileCache {
public:
    static constexpr qint64 kDefaultMaxBytes = 384LL * 1024 * 1024;

    explicit TileCache(qint64 maxBytes = kDefaultMaxBytes);

    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;

    // false — растр пустой или больше лимита (не положен).
    bool insert(const TileKey& key, const QImage& image);
    // Растр (QImage — неявно разделяемый, копия дешёвая) и отметка «свежий»; nullopt — нет.
    std::optional<QImage> find(const TileKey& key);
    bool contains(const TileKey& key) const;
    bool remove(const TileKey& key);
    // Сбросить тайлы документа (закрытие) или страницы (правка — хотя ключ с новой revision и так другой).
    int removeDocument(std::uint64_t doc);
    int removePage(std::uint64_t doc, int page);
    void clear();

    qint64 maxBytes() const;
    void setMaxBytes(qint64 maxBytes);
    qint64 totalBytes() const;
    int count() const;

private:
    mutable QMutex mutex_;
    QCache<TileKey, QImage> cache_;
};

}  // namespace ot
