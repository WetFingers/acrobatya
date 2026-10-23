// Тема интерфейса (эталон 6.4–6.5): Fusion + QPalette из токенов + QSS только для того, чего палитра не
// умеет, шрифт Segoe UI 9 pt. Theme::apply меняет тему без перезапуска (сигнал themeChanged — перекраска
// иконок); «Системная» следует QStyleHints::colorScheme() и контрастности Windows; «Крупные элементы» — scale
// = 1,25.
#pragma once

#include <QColor>
#include <QFont>
#include <QObject>
#include <QPalette>
#include <QPointer>
#include <QString>

#include <optional>
#include <vector>

#include "ui/theme/tokens.h"

class QApplication;
class QStyle;

namespace ot::ui {

enum class ThemeChoice { System, Light, Dark, HighContrast };

QString themeChoiceName(ThemeChoice choice);  // system | light | dark | contrast — для --theme и настроек
std::optional<ThemeChoice> parseThemeChoice(const QString& name);

// palette.cpp
QColor mix(const QColor& base, const QColor& over, double amount);  // over поверх base с долей amount (0…1)
QPalette makeQPalette(const Palette& p);

// qss.cpp — правила по классам и objectName компонентов; цвета пар fg/bg — те же, что в QSS (тест контраста).
struct ColorPair {
    QString component;
    QString state;
    QColor fg;
    QColor bg;
};
QString makeStyleSheet(const Palette& p, const Metrics& m);
std::vector<ColorPair> componentColors(const Palette& p);
// Мягкий фон тона (полосы сообщений, чипы, тосты): tone поверх surface.
QColor softBackground(const Palette& p, const Rgba& tone);

// fonts.cpp
QFont uiFont(const Metrics& m);          // Segoe UI fontPt
QFont panelTitleFont(const Metrics& m);  // 13 px / 600
QFont homeTitleFont(const Metrics& m);   // 20 px / 600
QFont numberFont(const Metrics& m);      // табличные цифры (tnum) — строка состояния

class Theme : public QObject {
    Q_OBJECT
public:
    static Theme& instance();

    // Применить тему ко всему приложению: стиль Fusion (если ещё не он), палитра, QSS, шрифт; themeChanged.
    static void apply(QApplication& app, ThemeChoice choice, double scale = 1.0);
    static ThemeKind resolve(ThemeChoice choice, Qt::ColorScheme scheme, Qt::ContrastPreference contrast);

    ThemeChoice choice() const { return choice_; }
    ThemeKind kind() const { return kind_; }
    double scale() const { return metrics_.scale; }
    const Palette& palette() const { return Tokens::palette(kind_); }
    const Metrics& metrics() const { return metrics_; }
    QString baseStyleName() const;  // "fusion" — под обёрткой QSS

    // Короткие доступы для компонентов.
    static const Palette& colors() { return instance().palette(); }
    static const Metrics& sizes() { return instance().metrics(); }

signals:
    void themeChanged();

private:
    Theme() = default;
    void watchSystem(QApplication& app);
    void onSystemChanged();

    ThemeChoice choice_ = ThemeChoice::Light;
    ThemeKind kind_ = ThemeKind::Light;
    Metrics metrics_ = Tokens::base();
    bool watching_ = false;
    QPointer<QStyle> fusion_;
};

}  // namespace ot::ui
