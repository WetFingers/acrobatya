// TileCache — растры тайлов и превью страниц (эталон 4.1–4.2): QCache<TileKey, QImage> с лимитом в байтах
// (по умолчанию 384 МБ, настройка «Кеш страниц» 64…4096 МБ), cost = QImage::sizeInBytes(); вытесняется самое
// давнее. Превью (1/8 масштаба, tiles.h makePreviewKey) — отдельный QCache: 1/8 общего лимита, тайлы — 7/8;
// вместе не больше maxBytes(). Общий на процесс; методы — из любого потока (под мьютексом). Растр больше
// своего бюджета не кладётся.
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
    static constexpr int kMinMegabytes = 64;
    static constexpr int kMaxMegabytes = 4096;

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

    // Превью страницы (ключ makePreviewKey).
    bool insertPreview(const TileKey& key, const QImage& image);
    std::optional<QImage> findPreview(const TileKey& key);

    // Общий лимит (тайлы + превью) и его части.
    qint64 maxBytes() const;
    void setMaxBytes(qint64 maxBytes);
    qint64 tileBudget() const;
    qint64 previewBudget() const;
    // Тайлы + превью.
    qint64 totalBytes() const;
    qint64 previewBytes() const;
    int count() const;
    int previewCount() const;
    // «Мало памяти»: сжать оба кеша до percent % их бюджета (самое давнее — первым).
    void shrink(int percent);

    // Лимит из настройки «Кеш страниц, МБ» (ограничен 64…4096).
    static qint64 limitFromMegabytes(int megabytes) noexcept;

private:
    void applyLimit(qint64 maxBytes);

    mutable QMutex mutex_;
    qint64 maxBytes_ = kDefaultMaxBytes;
    QCache<TileKey, QImage> cache_;
    QCache<TileKey, QImage> previews_;
};

}  // namespace ot
