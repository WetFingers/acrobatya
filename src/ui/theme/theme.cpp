#include "ui/theme/theme.h"

#include <QAccessibilityHints>
#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>

namespace ot::ui {

QString themeChoiceName(ThemeChoice choice) {
    switch (choice) {
        case ThemeChoice::System:
            return QStringLiteral("system");
        case ThemeChoice::Dark:
            return QStringLiteral("dark");
        case ThemeChoice::HighContrast:
            return QStringLiteral("contrast");
        case ThemeChoice::Light:
            break;
    }
    return QStringLiteral("light");
}

std::optional<ThemeChoice> parseThemeChoice(const QString& name) {
    for (ThemeChoice c :
         {ThemeChoice::System, ThemeChoice::Light, ThemeChoice::Dark, ThemeChoice::HighContrast}) {
        if (name.compare(themeChoiceName(c), Qt::CaseInsensitive) == 0) {
            return c;
        }
    }
    return std::nullopt;
}

Theme& Theme::instance() {
    static Theme* theme = new Theme();  // живёт до конца процесса: сигналы держат ссылки на неё
    return *theme;
}

ThemeKind Theme::resolve(ThemeChoice choice, Qt::ColorScheme scheme, Qt::ContrastPreference contrast) {
    switch (choice) {
        case ThemeChoice::Light:
            return ThemeKind::Light;
        case ThemeChoice::Dark:
            return ThemeKind::Dark;
        case ThemeChoice::HighContrast:
            return ThemeKind::HighContrast;
        case ThemeChoice::System:
            break;
    }
    if (contrast == Qt::ContrastPreference::HighContrast) {
        return ThemeKind::HighContrast;
    }
    return scheme == Qt::ColorScheme::Dark ? ThemeKind::Dark : ThemeKind::Light;
}

void Theme::apply(QApplication& app, ThemeChoice choice, double scale) {
    Theme& t = instance();
    // Стиль — только Fusion (с QSS приложения style() — обёртка QStyleSheetStyle, поэтому свой указатель);
    // в main он ставится до создания виджетов, здесь — снова, если кто-то сменил.
    if (t.fusion_.isNull()) {
        t.fusion_ = QStyleFactory::create(QStringLiteral("Fusion"));
        QApplication::setStyle(t.fusion_);
    }
    const QStyleHints* hints = QGuiApplication::styleHints();
    t.choice_ = choice;
    t.kind_ = resolve(choice, hints->colorScheme(), hints->accessibility()->contrastPreference());
    t.metrics_ = Tokens::scaled(scale);
    const Palette& p = Tokens::palette(t.kind_);
    QApplication::setFont(uiFont(t.metrics_));
    QApplication::setPalette(makeQPalette(p));
    app.setStyleSheet(makeStyleSheet(p, t.metrics_));
    t.watchSystem(app);
    emit t.themeChanged();
}

QString Theme::baseStyleName() const { return fusion_.isNull() ? QString() : fusion_->name(); }

void Theme::watchSystem(QApplication& app) {
    if (watching_) {
        return;
    }
    watching_ = true;
    const QStyleHints* hints = QGuiApplication::styleHints();
    connect(hints, &QStyleHints::colorSchemeChanged, this, &Theme::onSystemChanged);
    connect(hints->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this,
            &Theme::onSystemChanged);
    connect(&app, &QObject::destroyed, this, [this] { watching_ = false; });
}

void Theme::onSystemChanged() {
    if (choice_ != ThemeChoice::System) {
        return;
    }
    auto* app = qobject_cast<QApplication*>(QCoreApplication::instance());
    if (app != nullptr) {
        apply(*app, ThemeChoice::System, metrics_.scale);
    }
}

}  // namespace ot::ui
