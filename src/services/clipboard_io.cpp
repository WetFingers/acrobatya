#include "services/clipboard_io.h"

#include <QBuffer>
#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>

#include <algorithm>
#include <cmath>

#include "core/render/display_cache.h"
#include "core/render/renderer.h"
#include "core/text/selection.h"
#include "core/text/stext.h"
#include "services/document_session.h"
#include "services/task_context.h"

namespace ot {

namespace {

PointF applyMatrix(const Matrix& m, float x, float y) {
    return {x * m.a + y * m.c + m.e, x * m.b + y * m.d + m.f};
}

// Область в пунктах → пиксели устройства при матрице страницы (с поворотом), внутри рамки страницы.
IRect deviceArea(const Matrix& m, const QRectF& area, const IRect& page) {
    const PointF p[4] = {applyMatrix(m, float(area.left()), float(area.top())),
                         applyMatrix(m, float(area.right()), float(area.top())),
                         applyMatrix(m, float(area.left()), float(area.bottom())),
                         applyMatrix(m, float(area.right()), float(area.bottom()))};
    float x0 = p[0].x, x1 = p[0].x, y0 = p[0].y, y1 = p[0].y;
    for (const PointF& q : p) {
        x0 = std::min(x0, q.x);
        x1 = std::max(x1, q.x);
        y0 = std::min(y0, q.y);
        y1 = std::max(y1, q.y);
    }
    // Допуск на погрешность float: 216 pt при 300 dpi — ровно 900 px, а не 901.
    IRect r{static_cast<int>(std::floor(x0 + 1e-3f)), static_cast<int>(std::floor(y0 + 1e-3f)),
            static_cast<int>(std::ceil(x1 - 1e-3f)), static_cast<int>(std::ceil(y1 - 1e-3f))};
    r.x0 = std::max(r.x0, page.x0);
    r.y0 = std::max(r.y0, page.y0);
    r.x1 = std::min(r.x1, page.x1);
    r.y1 = std::min(r.y1, page.y1);
    return r;
}

}  // namespace

std::unique_ptr<QMimeData> ClipboardIo::textMime(const QString& plain, const QString& html) {
    auto mime = std::make_unique<QMimeData>();
    mime->setText(plain);
    if (!html.isEmpty()) {
        mime->setHtml(html);
    }
    return mime;
}

std::unique_ptr<QMimeData> ClipboardIo::textMime(const TextPage& page, const TextSelection& selection) {
    return textMime(QString::fromStdString(toPlainText(page, selection)),
                    QString::fromStdString(toHtml(page, selection)));
}

std::unique_ptr<QMimeData> ClipboardIo::imageMime(const QImage& image) {
    auto mime = std::make_unique<QMimeData>();
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    mime->setData(QStringLiteral("image/png"), png);
    mime->setImageData(image);
    return mime;
}

bool ClipboardIo::put(std::unique_ptr<QMimeData> mime) {
    if (!mime || qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return false;
    }
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) {
        return false;
    }
    clipboard->setMimeData(mime.release());  // буфер берёт владение
    return true;
}

QFuture<QImage> ClipboardIo::snapshot(DocumentSession& session, int page, const QRectF& area, qreal dpi,
                                      bool annotations) {
    return session.post(
        [page, area, dpi, annotations](TaskContext& tc) {
            const auto list = tc.displayList(page, annotations);
            // Свой масштаб ровно dpi/72 (pageMatrix тайлов округляет зум до тысячных — 216 pt при 300 dpi
            // дали бы 901 px); снимок — в осях страницы, поворот вида — забота ui.
            const float zoom = static_cast<float>(dpi / 72.0);
            const RectF& b = list->bounds();
            const Matrix m = concat(Matrix::scale(zoom), Matrix::translate(-b.x0 * zoom, -b.y0 * zoom));
            const IRect device{0, 0, static_cast<int>(std::ceil(b.width() * zoom - 1e-3f)),
                               static_cast<int>(std::ceil(b.height() * zoom - 1e-3f))};
            const IRect rect = deviceArea(m, area, device);
            if (rect.empty()) {
                return QImage();
            }
            QImage image(rect.width(), rect.height(), QImage::Format_ARGB32_Premultiplied);
            renderDisplayList(tc.context(), *list, m, rect, image.bits(),
                              static_cast<int>(image.bytesPerLine()), PixelFormat::Bgra32Premultiplied,
                              tc.cookie());
            const int dpm = static_cast<int>(std::lround(dpi / 0.0254));
            image.setDotsPerMeterX(dpm);
            image.setDotsPerMeterY(dpm);
            return image;
        },
        TaskPriority::High, QStringLiteral("snapshot"));
}

}  // namespace ot
