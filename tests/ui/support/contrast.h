// Контраст по WCAG 2.x (эталон 6.4: текст/фон ≥ 4,5:1): относительная яркость sRGB и отношение (L1 + 0,05) /
// (L2 + 0,05). Полупрозрачный цвет сначала накладывается на фон.
#pragma once

#include <QColor>

#include <algorithm>
#include <cmath>

namespace ot::test {

inline double channelToLinear(double c) {
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double relativeLuminance(const QColor& c) {
    return 0.2126 * channelToLinear(c.redF()) + 0.7152 * channelToLinear(c.greenF()) +
           0.0722 * channelToLinear(c.blueF());
}

inline QColor flatten(const QColor& fg, const QColor& bg) {
    const double a = fg.alphaF();
    return QColor::fromRgbF(static_cast<float>(fg.redF() * a + bg.redF() * (1 - a)),
                            static_cast<float>(fg.greenF() * a + bg.greenF() * (1 - a)),
                            static_cast<float>(fg.blueF() * a + bg.blueF() * (1 - a)));
}

inline double contrastRatio(const QColor& fg, const QColor& bg) {
    const double l1 = relativeLuminance(flatten(fg, bg));
    const double l2 = relativeLuminance(bg);
    return (std::max(l1, l2) + 0.05) / (std::min(l1, l2) + 0.05);
}

}  // namespace ot::test
