#include "services/signature_image.h"

#include <QBuffer>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>
#include <cmath>

namespace ot::fillsign {

namespace {

int luminance(QRgb p) { return (qRed(p) * 299 + qGreen(p) * 587 + qBlue(p) * 114) / 1000; }

}  // namespace

QImage removeBackground(const QImage& source, int threshold, bool keepColor, const QColor& ink) {
    if (source.isNull()) {
        return {};
    }
    const QImage in = source.convertToFormat(QImage::Format_ARGB32);
    QImage out(in.size(), QImage::Format_ARGB32);
    const int t = std::clamp(threshold, 1, 255);
    const double soft = std::max(1.0, t / 2.0);  // ширина мягкого края
    const QRgb inkRgb = ink.isValid() ? ink.rgb() : qRgb(0, 0, 0);
    for (int y = 0; y < in.height(); ++y) {
        const auto* src = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            const QRgb p = src[x];
            const int l = luminance(p);
            int alpha = l >= t ? 0 : static_cast<int>(std::lround(std::min(1.0, (t - l) / soft) * 255.0));
            alpha = alpha * qAlpha(p) / 255;
            const QRgb color = keepColor ? p : inkRgb;
            dst[x] = qRgba(qRed(color), qGreen(color), qBlue(color), alpha);
        }
    }
    return out;
}

QImage cropToInk(const QImage& image, int margin) {
    if (image.isNull()) {
        return {};
    }
    const QImage in = image.convertToFormat(QImage::Format_ARGB32);
    int x0 = in.width();
    int y0 = in.height();
    int x1 = -1;
    int y1 = -1;
    for (int y = 0; y < in.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            if (qAlpha(row[x]) > 0) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
                y0 = std::min(y0, y);
                y1 = std::max(y1, y);
            }
        }
    }
    if (x1 < 0) {
        return {};
    }
    const int m = std::max(0, margin);
    QImage out(x1 - x0 + 1 + 2 * m, y1 - y0 + 1 + 2 * m, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    for (int y = y0; y <= y1; ++y) {
        const auto* src = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        auto* dst = reinterpret_cast<QRgb*>(out.scanLine(y - y0 + m));
        std::copy(src + x0, src + x1 + 1, dst + m);
    }
    return out;
}

QImage recolor(const QImage& image, const QColor& color) {
    if (image.isNull()) {
        return {};
    }
    QImage out = image.convertToFormat(QImage::Format_ARGB32);
    const QRgb c = color.rgb();
    for (int y = 0; y < out.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            row[x] = qRgba(qRed(c), qGreen(c), qBlue(c), qAlpha(row[x]));
        }
    }
    return out;
}

QImage strokesToImage(const std::vector<std::vector<QPointF>>& strokes, const QSizeF& canvas,
                      const QColor& color, double width, double scale) {
    if (strokes.empty() || canvas.isEmpty()) {
        return {};
    }
    const double k = std::max(1.0, scale);
    QImage image(static_cast<int>(std::ceil(canvas.width() * k)),
                 static_cast<int>(std::ceil(canvas.height() * k)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.scale(k, k);
        QPen pen(color.isValid() ? color : QColor(Qt::black), std::max(0.5, width), Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        for (const std::vector<QPointF>& stroke : strokes) {
            if (stroke.empty()) {
                continue;
            }
            if (stroke.size() == 1) {
                painter.drawPoint(stroke.front());  // точка — круглый конец пера
                continue;
            }
            QPainterPath path(stroke.front());
            for (std::size_t i = 1; i + 1 < stroke.size(); ++i) {
                path.quadTo(stroke[i], (stroke[i] + stroke[i + 1]) / 2);
            }
            path.lineTo(stroke.back());
            painter.drawPath(path);
        }
    }
    return cropToInk(image.convertToFormat(QImage::Format_ARGB32));
}

QImage typedImage(const QString& text, const QString& family, const QColor& color, int pixelSize) {
    const QString line = text.simplified();
    if (line.isEmpty()) {
        return {};
    }
    QFont font(family);
    font.setPixelSize(std::max(8, pixelSize));
    const QFontMetricsF metrics(font);
    // Рукописные шрифты выходят за advance (росчерки) — запас по краям в высоту строки.
    const double pad = metrics.height();
    const QSizeF size(metrics.horizontalAdvance(line) + 2 * pad, metrics.height() + pad);
    QImage image(static_cast<int>(std::ceil(size.width())), static_cast<int>(std::ceil(size.height())),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font);
        painter.setPen(color.isValid() ? color : QColor(Qt::black));
        painter.drawText(QPointF(pad, pad / 2 + metrics.ascent()), line);
    }
    return cropToInk(image.convertToFormat(QImage::Format_ARGB32));
}

QString initialsOf(const QString& name) {
    QString out;
    int count = 0;
    for (const QString& word : name.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const QChar first = word.front();
        if (!first.isLetter()) {
            continue;
        }
        out += first.toUpper();
        out += QLatin1Char('.');
        if (++count == 3) {
            break;
        }
    }
    return out;
}

bool hasInk(const QImage& image, int minAlpha) {
    if (image.isNull()) {
        return false;
    }
    const QImage in = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < in.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        for (int x = 0; x < in.width(); ++x) {
            if (qAlpha(row[x]) >= minAlpha) {
                return true;
            }
        }
    }
    return false;
}

int inkComponents(const QImage& image, int minAlpha) {
    if (image.isNull()) {
        return 0;
    }
    const QImage in = image.convertToFormat(QImage::Format_ARGB32);
    const int w = in.width();
    const int h = in.height();
    std::vector<unsigned char> ink(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(in.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            ink[static_cast<std::size_t>(y) * w + x] = qAlpha(row[x]) >= minAlpha ? 1 : 0;
        }
    }
    int count = 0;
    std::vector<int> stack;
    for (int start = 0; start < w * h; ++start) {
        if (ink[static_cast<std::size_t>(start)] != 1) {
            continue;
        }
        ++count;
        ink[static_cast<std::size_t>(start)] = 2;
        stack.push_back(start);
        while (!stack.empty()) {
            const int i = stack.back();
            stack.pop_back();
            const int cx = i % w;
            const int cy = i / w;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = cx + dx;
                    const int ny = cy + dy;
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) {
                        continue;
                    }
                    const auto n = static_cast<std::size_t>(ny) * w + nx;
                    if (ink[n] == 1) {
                        ink[n] = 2;
                        stack.push_back(static_cast<int>(n));
                    }
                }
            }
        }
    }
    return count;
}

QByteArray toPng(const QImage& image) {
    if (image.isNull()) {
        return {};
    }
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_ARGB32).save(&buffer, "PNG");
    return bytes;
}

}  // namespace ot::fillsign
