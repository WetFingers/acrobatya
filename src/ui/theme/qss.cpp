// QSS из токенов (эталон 6.4): только то, чего палитра Fusion не умеет — главная кнопка, подсказки, холст,
// компоненты src/ui/widgets (по классам ot--ui--…, варианты — динамическими свойствами). Единственное место
// QSS. componentColors() отдаёт те же пары «текст/фон», из которых собраны правила, — их проверяет ui.theme.
#include "ui/theme/theme.h"

#include <utility>

namespace ot::ui {

namespace {

QString css(const QColor& c) {
    if (c.alpha() == 255) {
        return c.name(QColor::HexRgb);
    }
    return QStringLiteral("rgba(%1, %2, %3, %4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

QString css(const Rgba& c) { return css(c.color()); }

QColor primaryHover(const Palette& p) { return mix(p.accent.color(), p.text.color(), 0.14); }

// Шаблон: @имя@ — цвет или размер (px) из токенов.
const char* const kTemplate = R"(
QToolTip {
    color: @surface@;
    background-color: @text@;
    border: none;
    padding: @grid@px @grid2@px;
}
QPushButton[primary="true"] {
    color: @onAccent@;
    background-color: @accent@;
    border: 1px solid @accent@;
    border-radius: @radius@px;
    padding: @grid@px @grid4@px;
    min-height: @row16@px;
}
QPushButton[primary="true"]:hover {
    background-color: @primaryHover@;
    border-color: @primaryHover@;
}
QPushButton[primary="true"]:focus {
    border: @focusRing@px solid @text@;
}
QPushButton[primary="true"]:disabled {
    color: @textMuted@;
    background-color: @hover@;
    border-color: @border@;
}
#pageView, #pageView > QWidget {
    background-color: @canvas@;
}
)";

}  // namespace

QColor softBackground(const Palette& p, const Rgba& tone) {
    return mix(p.surface.color(), tone.color(), 0.16);
}

QString makeStyleSheet(const Palette& p, const Metrics& m) {
    QString qss = QString::fromLatin1(kTemplate);
    const std::pair<const char*, QString> vars[] = {
        {"@surface@", css(p.surface)},
        {"@surfaceAlt@", css(p.surfaceAlt)},
        {"@canvas@", css(p.canvas)},
        {"@border@", css(p.border)},
        {"@text@", css(p.text)},
        {"@textMuted@", css(p.textMuted)},
        {"@accent@", css(p.accent)},
        {"@accentSoft@", css(p.accentSoft)},
        {"@onAccent@", css(p.onAccent)},
        {"@hover@", css(p.hover)},
        {"@primaryHover@", css(primaryHover(p))},
        {"@grid@", QString::number(m.grid)},
        {"@grid2@", QString::number(m.grid * 2)},
        {"@grid4@", QString::number(m.grid * 4)},
        {"@row16@", QString::number(m.row - m.grid * 4)},
        {"@radius@", QString::number(m.radius)},
        {"@radiusLarge@", QString::number(m.radiusLarge)},
        {"@focusRing@", QString::number(m.focusRing)},
    };
    for (const auto& [key, value] : vars) {
        qss.replace(QLatin1String(key), value);
    }
    return qss;
}

std::vector<ColorPair> componentColors(const Palette& p) {
    const QColor text = p.text.color();
    std::vector<ColorPair> pairs = {
        {QStringLiteral("window"), QStringLiteral("normal"), text, p.surface.color()},
        {QStringLiteral("panel"), QStringLiteral("normal"), text, p.surfaceAlt.color()},
        {QStringLiteral("canvas"), QStringLiteral("normal"), text, p.canvas.color()},
        {QStringLiteral("caption"), QStringLiteral("normal"), p.textMuted.color(), p.surface.color()},
        {QStringLiteral("caption"), QStringLiteral("panel"), p.textMuted.color(), p.surfaceAlt.color()},
        {QStringLiteral("button"), QStringLiteral("hover"), text, p.hover.color()},
        {QStringLiteral("button.primary"), QStringLiteral("normal"), p.onAccent.color(), p.accent.color()},
        {QStringLiteral("button.primary"), QStringLiteral("hover"), p.onAccent.color(), primaryHover(p)},
        {QStringLiteral("row"), QStringLiteral("selected"), text, p.accentSoft.color()},
        {QStringLiteral("tooltip"), QStringLiteral("normal"), p.surface.color(), text},
    };
    const std::pair<const char*, Rgba> tones[] = {
        {"info", p.info}, {"warn", p.warn}, {"danger", p.danger}, {"ok", p.ok}, {"form", p.form},
    };
    for (const auto& [name, tone] : tones) {
        pairs.push_back({QStringLiteral("tone"), QLatin1String(name), text, softBackground(p, tone)});
    }
    return pairs;
}

}  // namespace ot::ui
