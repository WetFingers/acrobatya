// QPalette из токенов (эталон 6.4): роли Fusion ↔ имена таблицы; выключенное — приглушённый текст.
#include <QPalette>

#include "ui/theme/theme.h"

namespace ot::ui {

QColor mix(const QColor& base, const QColor& over, double amount) {
    const double a = amount * over.alphaF();
    auto ch = [a](double x, double y) { return x * (1.0 - a) + y * a; };
    return QColor::fromRgbF(static_cast<float>(ch(base.redF(), over.redF())),
                            static_cast<float>(ch(base.greenF(), over.greenF())),
                            static_cast<float>(ch(base.blueF(), over.blueF())), base.alphaF());
}

QPalette makeQPalette(const Palette& p) {
    QPalette pal;
    const QColor surface = p.surface.color();
    const QColor text = p.text.color();
    const QColor disabledText = mix(surface, p.textMuted.color(), 0.65);
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool off = group == QPalette::Disabled;
        pal.setColor(group, QPalette::Window, surface);
        pal.setColor(group, QPalette::WindowText, off ? disabledText : text);
        pal.setColor(group, QPalette::Base, surface);
        pal.setColor(group, QPalette::AlternateBase, p.surfaceAlt.color());
        pal.setColor(group, QPalette::Text, off ? disabledText : text);
        pal.setColor(group, QPalette::PlaceholderText, p.textMuted.color());
        pal.setColor(group, QPalette::Button, surface);
        pal.setColor(group, QPalette::ButtonText, off ? disabledText : text);
        pal.setColor(group, QPalette::BrightText, p.onAccent.color());
        pal.setColor(group, QPalette::Highlight, off ? p.border.color() : p.accent.color());
        pal.setColor(group, QPalette::HighlightedText, off ? disabledText : p.onAccent.color());
        pal.setColor(group, QPalette::Accent, p.accent.color());
        pal.setColor(group, QPalette::Link, p.info.color());
        pal.setColor(group, QPalette::LinkVisited, p.info.color());
        pal.setColor(group, QPalette::ToolTipBase, text);
        pal.setColor(group, QPalette::ToolTipText, surface);
        // Объём Fusion: Light — блик, Midlight — наведение, Mid — рамки, Dark — холст (фон вокруг страниц),
        // Shadow — тень.
        pal.setColor(group, QPalette::Light, p.surface.color());
        pal.setColor(group, QPalette::Midlight, p.hover.color());
        pal.setColor(group, QPalette::Mid, p.border.color());
        pal.setColor(group, QPalette::Dark, p.canvas.color());
        pal.setColor(group, QPalette::Shadow, QColor(p.shadow.r, p.shadow.g, p.shadow.b));
    }
    return pal;
}

}  // namespace ot::ui
