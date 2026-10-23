// Тон компонента (эталон 6.1, 6.4, 6.6): полосы сообщений, чипы и тосты — нейтральный, акцент, сведения
// (синий), предупреждение (жёлтый), ошибка (красный), успех (зелёный), форма (фиолетовый). Имя тона —
// значение свойства «tone» для QSS (src/ui/theme/qss.cpp).
#pragma once

#include <QString>
#include <QStyle>
#include <QWidget>

#include "ui/theme/icons.h"

namespace ot::ui {

enum class Tone { Neutral, Accent, Info, Warn, Danger, Ok, Form };

inline QString toneName(Tone t) {
    switch (t) {
        case Tone::Accent:
            return QStringLiteral("accent");
        case Tone::Info:
            return QStringLiteral("info");
        case Tone::Warn:
            return QStringLiteral("warn");
        case Tone::Danger:
            return QStringLiteral("danger");
        case Tone::Ok:
            return QStringLiteral("ok");
        case Tone::Form:
            return QStringLiteral("form");
        case Tone::Neutral:
            break;
    }
    return QStringLiteral("neutral");
}

inline IconTone toneIcon(Tone t) {
    switch (t) {
        case Tone::Accent:
            return IconTone::Accent;
        case Tone::Info:
            return IconTone::Info;
        case Tone::Warn:
            return IconTone::Warn;
        case Tone::Danger:
            return IconTone::Danger;
        case Tone::Ok:
            return IconTone::Ok;
        case Tone::Form:
            return IconTone::Form;
        case Tone::Neutral:
            break;
    }
    return IconTone::Text;
}

// Иконка тона по умолчанию (полоса сообщений, тост).
inline QString toneIconName(Tone t) {
    switch (t) {
        case Tone::Warn:
            return QStringLiteral("triangle-alert");
        case Tone::Danger:
            return QStringLiteral("circle-alert");
        case Tone::Ok:
            return QStringLiteral("circle-check");
        case Tone::Form:
            return QStringLiteral("clipboard-list");
        case Tone::Neutral:
        case Tone::Accent:
        case Tone::Info:
            break;
    }
    return QStringLiteral("info");
}

// Свойство изменилось — QSS по [свойство="…"] пересчитать (у виджета и детей).
inline void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    for (QWidget* child : w->findChildren<QWidget*>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
    w->update();
}

}  // namespace ot::ui
