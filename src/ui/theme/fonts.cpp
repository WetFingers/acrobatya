// Шрифты (эталон 6.5): системный Segoe UI 9 pt задаётся явно — offscreen иначе берёт «Agency FB»; заголовки
// панелей 13 px/600, Главной 20 px; числа строки состояния — табличные (tnum). Своих TTF нет.
#include <QFont>

#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

QFont segoe() {
    QFont f(QStringLiteral("Segoe UI"));
    f.setStyleHint(QFont::SansSerif);
    f.setHintingPreference(QFont::PreferDefaultHinting);
    return f;
}

}  // namespace

QFont uiFont(const Metrics& m) {
    QFont f = segoe();
    f.setPointSizeF(m.fontPt);
    return f;
}

QFont panelTitleFont(const Metrics& m) {
    QFont f = segoe();
    f.setPixelSize(m.panelTitlePx);
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont homeTitleFont(const Metrics& m) {
    QFont f = segoe();
    f.setPixelSize(m.homeTitlePx);
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont numberFont(const Metrics& m) {
    QFont f = uiFont(m);
    f.setFeature(QFont::Tag("tnum"), 1);
    return f;
}

}  // namespace ot::ui
