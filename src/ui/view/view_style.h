// Стиль рендера окна из настроек (план 2.5b; эталон 3.1 «Сглаживание и качество», 3.16; view-26, view-36,
// ocr-38, pro-47): замена цветов (reading/recolor: ночной режим и высокая контрастность — пары токенов
// kNightReading / kHighContrastReading, свои цвета, «инвертировать всё»), предпросмотр наложения
// (pageDisplay/overprintPreview), сглаживание текста, векторов и картинок и «утолщать тонкие линии»
// (pageDisplay/smoothText, smoothLineArt, smoothImages, enhanceThinLines: AA 8 бит или 0), рамки областей.
// Документу — styleFor(pdfx): overprint «Только для PDF/X» решается по его OutputIntents. Бумага под растром
// — paperColor (цвет фона замены, чёрная при инверсии, иначе kPaperColor).
#pragma once

#include <QColor>
#include <QString>

#include "core/render/render_style.h"

namespace ot {
class Settings;
}

namespace ot::ui {

struct ViewStyle {
    RenderStyle base;  // без overprint
    OverprintPreview overprint = OverprintPreview::PdfX;
    bool showPageBoxes = false;  // pageDisplay/showPageBoxes — слой рамок (boxes_overlay.h)

    static ViewStyle fromSettings(const Settings& settings);
    // Замена цветов по значению reading/recolor (none, night, highContrast, custom, invert); схема высокой
    // контрастности, свои цвета и флажки — из settings.
    static Recolor recolorFor(const QString& mode, const Settings& settings);
    RenderStyle styleFor(bool pdfx) const;
    bool operator==(const ViewStyle&) const = default;
};

QColor paperColor(const RenderStyle& style);

}  // namespace ot::ui
