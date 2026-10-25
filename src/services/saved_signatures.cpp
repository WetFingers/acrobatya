#include "services/saved_signatures.h"

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <spdlog/spdlog.h>

#include "services/app_info.h"
#include "services/signature_image.h"

namespace ot::fillsign {

QString kindName(SignatureKind kind) {
    return kind == SignatureKind::Initials ? QStringLiteral("initials") : QStringLiteral("signature");
}

SavedSignatures::SavedSignatures() : root_(QDir(app::dataDir()).filePath(QStringLiteral("signatures"))) {}

SavedSignatures::SavedSignatures(QString root) : root_(std::move(root)) {}

QString SavedSignatures::path(SignatureKind kind) const {
    return QDir(root_).filePath(kindName(kind) + QStringLiteral(".png"));
}

bool SavedSignatures::has(SignatureKind kind) const { return QFile::exists(path(kind)); }

QImage SavedSignatures::load(SignatureKind kind) const {
    QImage image;
    if (!has(kind) || !image.load(path(kind), "PNG")) {
        return {};
    }
    return image.convertToFormat(QImage::Format_ARGB32);
}

QByteArray SavedSignatures::png(SignatureKind kind) const {
    QFile file(path(kind));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

bool SavedSignatures::save(SignatureKind kind, const QImage& image, QString* error) {
    auto fail = [error](const QString& text) {
        if (error != nullptr) {
            *error = text;
        }
        spdlog::warn("fillsign: {}", text.toStdString());
        return false;
    };
    if (image.isNull() || !hasInk(image)) {
        return fail(QStringLiteral("empty signature image"));
    }
    if (!QDir().mkpath(root_)) {
        return fail(QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(root_)));
    }
    const QByteArray bytes = toPng(image);
    QSaveFile file(path(kind));
    if (bytes.isEmpty() || !file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        return fail(QStringLiteral("cannot write %1").arg(QDir::toNativeSeparators(path(kind))));
    }
    spdlog::info("fillsign: saved {} ({}x{})", kindName(kind).toStdString(), image.width(), image.height());
    return true;
}

bool SavedSignatures::remove(SignatureKind kind) {
    if (!has(kind)) {
        return false;
    }
    const bool ok = QFile::remove(path(kind));
    if (ok) {
        spdlog::info("fillsign: removed {}", kindName(kind).toStdString());
    }
    return ok;
}

}  // namespace ot::fillsign
