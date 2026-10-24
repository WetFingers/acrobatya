// «Снимок» (X; эталон 3.2, view-33): рамка на странице → растр области в буфер обмена (PNG и изображение) при
// разрешении экрана, с Alt — 300 dpi; щелчок — страница целиком. Растр рисует задача документа
// (services/clipboard_io.h, display list с пометками); поворот вида — поворотом растра. Однократный: после
// снимка — прежний инструмент, если не закреплён (comment-42).
#include <QCoreApplication>
#include <QFuture>

#include <algorithm>
#include <cmath>

#include "services/clipboard_io.h"
#include "services/document_session.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

QString SnapshotTool::title() const { return QCoreApplication::translate("ot::ui::SnapshotTool", "Снимок"); }

QCursor SnapshotTool::cursor(QPointF /*point*/, Qt::KeyboardModifiers /*modifiers*/) const {
    return QCursor(busy_ ? Qt::BusyCursor : Qt::CrossCursor);
}

void SnapshotTool::finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers modifiers) {
    DocumentSession* session = view_.document();
    if (session == nullptr || !session->isOpen() || busy_) {
        return;
    }
    // Страница: под рамкой (больше всего площади) или под щелчком.
    int page = -1;
    QRectF onScreen;
    if (rect.isEmpty()) {
        page = view_.pageAtViewport(point);
        onScreen = view_.pageViewportRect(page);
    } else {
        double best = 0;
        for (const int p : view_.visiblePages()) {
            const QRectF part = view_.pageViewportRect(p).intersected(rect);
            const double area = part.width() * part.height();
            if (area > best) {
                best = area;
                page = p;
                onScreen = part;
            }
        }
    }
    if (page < 0 || onScreen.isEmpty()) {
        return;
    }
    const QRectF area = view_.pageTransform(page).inverted().mapRect(onScreen);
    const bool high = (modifiers & Qt::AltModifier) != 0;
    const qreal dpi = high ? ClipboardIo::kHighDpi : view_.zoom() * 72.0 * view_.devicePixelRatioF();
    const int rotation = view_.viewRotation();
    busy_ = true;
    QFuture<QImage> future = ClipboardIo::snapshot(*session, page, area, dpi);
    future.then(&controller_, [this, rotation](QFuture<QImage> done) {
        busy_ = false;
        QImage image;
        try {
            image = done.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): снимка нет — буфер не трогаем
            return;
        }
        if (image.isNull()) {
            return;
        }
        if (rotation != 0) {
            const int dpm = image.dotsPerMeterX();
            image = image.transformed(QTransform().rotate(rotation));
            image.setDotsPerMeterX(dpm);
            image.setDotsPerMeterY(dpm);
        }
        if (controller_.copyAllowed()) {  // право /P «копирование» снято — в буфер не кладём (2.5b)
            ClipboardIo::put(ClipboardIo::imageMime(image));
        }
        controller_.emitSnapshot(image);
        controller_.finished();
    });
}

}  // namespace ot::ui
