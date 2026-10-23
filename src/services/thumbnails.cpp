#include "services/thumbnails.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

#include <algorithm>
#include <exception>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "services/app_info.h"
#include "services/document_registry.h"

namespace ot {

namespace {

// Первая страница шириной width px (высота — до двух ширин: длинные ленты обрезаются по высоте рамкой
// карточки).
QImage renderFirstPage(const QString& path, int width) {
    OpenResult r = Document::open(path.toStdWString());
    if (!r.ok || r.document == nullptr || r.document->pageCount() <= 0) {
        return {};
    }
    const PageSize size = r.document->pageSize(0);
    if (size.width <= 0 || size.height <= 0) {
        return {};
    }
    RenderOptions options;
    options.dpi = std::clamp(72.0f * static_cast<float>(width) / size.width, 2.0f, 600.0f);
    const Bitmap bmp = r.document->renderPage(0, options);
    if (bmp.empty()) {
        return {};
    }
    const QImage::Format format = bmp.components == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888;
    return QImage(bmp.pixels.data(), bmp.width, bmp.height, bmp.stride, format)
        .convertToFormat(QImage::Format_RGB32);
}

}  // namespace

ThumbnailCache& ThumbnailCache::shared() {
    static ThumbnailCache cache(QDir(app::dataDir()).filePath(QStringLiteral("thumbs")));
    return cache;
}

ThumbnailCache::ThumbnailCache(QString dir, int width, QObject* parent)
    : QObject(parent), dir_(std::move(dir)), width_(width) {
    pool_.setMaxThreadCount(2);
    QDir().mkpath(dir_);
}

ThumbnailCache::~ThumbnailCache() {
    pool_.clear();
    pool_.waitForDone();
}

QString ThumbnailCache::cacheFile(const QString& path) const {
    const QFileInfo info(path);
    const QByteArray key = DocumentRegistry::pathKey(path).toUtf8() + '|' +
                           QByteArray::number(info.lastModified().toMSecsSinceEpoch()) + '|' +
                           QByteArray::number(info.size()) + '|' + QByteArray::number(width_);
    return QDir(dir_).filePath(
        QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex()) +
        QStringLiteral(".png"));
}

QImage ThumbnailCache::get(const QString& path) {
    if (const auto it = memory_.constFind(path); it != memory_.constEnd()) {
        return *it;
    }
    if (failed_.contains(path) || pending_.contains(path) || path.isEmpty()) {
        return {};
    }
    const QString file = cacheFile(path);
    if (QFileInfo::exists(file)) {
        QImage cached(file);
        if (!cached.isNull()) {
            memory_.insert(path, cached);
            return cached;
        }
    }
    pending_.insert(path);
    ++renders_;
    const int width = width_;
    // Деструктор ждёт пул: пока задача идёт, объект жив; ответ — очередью в его поток (после удаления не
    // придёт).
    ThumbnailCache* self = this;
    pool_.start([self, path, file, width] {
        QImage image;
        try {
            image = renderFirstPage(path, width);
        } catch (const std::exception& e) {
            spdlog::info("thumbnail: {} not rendered: {}", path.toStdString(), e.what());
        }
        if (!image.isNull() && !image.save(file, "PNG")) {
            spdlog::warn("thumbnail: cannot write {}", file.toStdString());
        }
        QMetaObject::invokeMethod(
            self, [self, path, image] { self->finished(path, image); }, Qt::QueuedConnection);
    });
    return {};
}

void ThumbnailCache::finished(const QString& path, const QImage& image) {
    pending_.remove(path);
    if (image.isNull()) {
        failed_.insert(path);
    } else {
        memory_.insert(path, image);
    }
    emit ready(path);
}

void ThumbnailCache::waitForDone() { pool_.waitForDone(); }

void ThumbnailCache::clearMemory() {
    memory_.clear();
    failed_.clear();
}

}  // namespace ot
