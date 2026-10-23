// ClipboardIo — буфер обмена (эталон 3.2, план 1.4): выделенный текст — text/plain и text/html (жирный и
// курсив по шрифтам, core/text/selection.h); «Снимок» (`X`) — область страницы растром при DPI монитора, с
// Alt — при 300 dpi: image/png и изображение Qt (Windows сам добавит CF_DIB). Снимок рисуется задачей
// документа из display list кеша (с пометками или без), dpi записывается в PNG. В буфер кладёт put — нужен
// QGuiApplication (в unit-тестах с QCoreApplication буфера нет: put возвращает false).
#pragma once

#include <QFuture>
#include <QImage>
#include <QMimeData>
#include <QRectF>
#include <QString>

#include <memory>
#include <utility>
#include <vector>

namespace ot {

class DocumentSession;
class TextPage;
struct TextSelection;

class ClipboardIo {
public:
    static constexpr qreal kHighDpi = 300;  // снимок с Alt

    static std::unique_ptr<QMimeData> textMime(const QString& plain, const QString& html);
    static std::unique_ptr<QMimeData> textMime(const TextPage& page, const TextSelection& selection);
    // Выделение на нескольких страницах (по порядку): текст страниц через «\n», HTML — фрагменты подряд в
    // одном документе.
    using PageSelection = std::pair<const TextPage*, const TextSelection*>;
    static std::unique_ptr<QMimeData> textMime(const std::vector<PageSelection>& pages);
    static std::unique_ptr<QMimeData> imageMime(const QImage& image);
    // Положить в системный буфер обмена; false — буфера нет (не QGuiApplication) или mime пуст.
    static bool put(std::unique_ptr<QMimeData> mime);

    // Растр области страницы area (пункты страницы, как у TextPage и fz_bound_page) при dpi: белый фон,
    // ARGB32_Premultiplied, dotsPerMeter по dpi. Задача документа (приоритет High); ошибка — исключение в
    // QFuture.
    static QFuture<QImage> snapshot(DocumentSession& session, int page, const QRectF& area, qreal dpi,
                                    bool annotations = true);
};

}  // namespace ot
