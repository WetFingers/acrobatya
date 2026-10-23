#include "services/document_session.h"

#include <exception>
#include <filesystem>

#include "core/doc/document.h"
#include "core/mu/context.h"

namespace ot {

DocumentSession::DocumentSession(QObject* parent) : QObject(parent), mu_(std::make_unique<MuContext>()) {}

DocumentSession::~DocumentSession() = default;

bool DocumentSession::open(const QString& path) {
    close();
    try {
        doc_ = Document::open(*mu_, std::filesystem::path(path.toStdWString()));
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return false;
    }
    path_ = path;
    error_.clear();
    emit opened(path_);
    return true;
}

void DocumentSession::close() {
    if (doc_) {
        doc_.reset();
        path_.clear();
        emit closed();
    }
}

bool DocumentSession::isOpen() const { return static_cast<bool>(doc_); }

int DocumentSession::pageCount() const {
    if (!doc_) {
        return 0;
    }
    try {
        return doc_->pageCount();
    } catch (const std::exception&) {
        return 0;
    }
}

QSizeF DocumentSession::pageSizePoints(int index) const {
    if (!doc_) {
        return {};
    }
    try {
        const PageSize s = doc_->pageSize(index);
        return {s.width, s.height};
    } catch (const std::exception&) {
        return {};
    }
}

QImage DocumentSession::renderPage(int index, qreal dpi) {
    if (!doc_) {
        return {};
    }
    try {
        RenderOptions opts;
        opts.dpi = static_cast<float>(dpi);
        const Bitmap bmp = doc_->renderPage(index, opts);
        const QImage::Format fmt = bmp.components == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888;
        QImage img(bmp.pixels.data(), bmp.width, bmp.height, bmp.stride, fmt);
        return img.copy();
    } catch (const std::exception& e) {
        error_ = QString::fromUtf8(e.what());
        return {};
    }
}

}  // namespace ot
