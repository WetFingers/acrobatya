#include "ui/view/view_style.h"

#include <QStringList>

#include <algorithm>
#include <cstddef>
#include <iterator>

#include "services/settings.h"
#include "ui/theme/tokens.h"

namespace ot::ui {

namespace {

std::uint32_t pack(const Rgba& c) noexcept {
    return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
}

// «#RRGGBB» из настроек; не цвет — fallback.
std::uint32_t colorOf(const QString& text, std::uint32_t fallback) {
    const QColor c = QColor::fromString(text);
    return c.isValid()
               ? (static_cast<std::uint32_t>(c.red()) << 16) | (static_cast<std::uint32_t>(c.green()) << 8) |
                     static_cast<std::uint32_t>(c.blue())
               : fallback;
}

}  // namespace

ViewStyle ViewStyle::fromSettings(const Settings& s) {
    ViewStyle v;
    v.overprint = overprintPreviewFromName(s.get(settings::kOverprintPreview).toStdString());
    v.base.recolor = recolorFor(s.get(settings::kRecolor), s);
    return v;
}

Recolor ViewStyle::recolorFor(const QString& mode, const Settings& s) {
    Recolor rc;
    if (mode == QLatin1String("night")) {
        rc.mode = RecolorMode::Replace;
        rc.text = pack(kNightReading.text);
        rc.background = pack(kNightReading.background);
    } else if (mode == QLatin1String("highContrast")) {
        static const QStringList schemes =
            QString::fromLatin1(settings::kHighContrastScheme.choices).split(QLatin1Char('|'));
        const qsizetype i = std::max<qsizetype>(0, schemes.indexOf(s.get(settings::kHighContrastScheme)));
        const ReadingColors& pair = kHighContrastReading[std::min<std::size_t>(
            static_cast<std::size_t>(i), std::size(kHighContrastReading) - 1)];
        rc.mode = RecolorMode::Replace;
        rc.text = pack(pair.text);
        rc.background = pack(pair.background);
    } else if (mode == QLatin1String("custom")) {
        rc.mode = RecolorMode::Replace;
        rc.text = colorOf(s.get(settings::kRecolorText), 0x000000);
        rc.background = colorOf(s.get(settings::kRecolorBackground), 0xFFFFFF);
    } else if (mode == QLatin1String("invert")) {
        rc.mode = RecolorMode::InvertAll;
    }
    if (rc.mode == RecolorMode::Replace) {
        rc.lineArt = s.get(settings::kRecolorLineArt);
        rc.onlyBlack = s.get(settings::kRecolorOnlyBlack);
    }
    return rc;
}

RenderStyle ViewStyle::styleFor(bool pdfx) const {
    RenderStyle style = base;
    style.overprint = simulateOverprint(overprint, pdfx);
    return style;
}

QColor paperColor(const RenderStyle& style) {
    switch (style.recolor.mode) {
        case RecolorMode::Replace: {
            const std::uint32_t c = style.recolor.background;
            return QColor(static_cast<int>((c >> 16) & 0xFF), static_cast<int>((c >> 8) & 0xFF),
                          static_cast<int>(c & 0xFF));
        }
        case RecolorMode::InvertAll:
            return QColor(255 - kPaperColor.r, 255 - kPaperColor.g, 255 - kPaperColor.b);
        case RecolorMode::None:
            break;
    }
    return kPaperColor.color();
}

}  // namespace ot::ui
