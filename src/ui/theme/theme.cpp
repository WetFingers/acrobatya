#include "ui/theme/theme.h"

#include <QAccessibilityHints>
#include <QApplication>
#include <QPainter>
#include <QPainterPath>
#include <QProxyStyle>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QStyleOption>

namespace ot::ui {

namespace {

// Fusion рисует рамку флажка и переключателя цветом окна, затемнённым на 40–50 %: на тёмной палитре (#1F1F1F
// → #181818) и в «Высокой контрастности» (чёрный) рамки не видно (рубеж Р1: сцены print-dialog-dark,
// preferences-*-dark). Поверх рамки Fusion — та же фигура цветом textMuted (QPalette::PlaceholderText):
// контраст с surface ≥ 3:1 во всех трёх палитрах (WCAG 1.4.11 для элементов управления); у недоступных —
// border (QPalette::Mid). Рамку фокуса клавиатурой (accent) Fusion рисует сам — её не трогаем.
class IndicatorStyle : public QProxyStyle {
public:
    explicit IndicatorStyle(QStyle* base) : QProxyStyle(base) {}

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget) const override {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        if (element != PE_IndicatorCheckBox && element != PE_IndicatorRadioButton) {
            return;
        }
        if ((option->state & State_HasFocus) && (option->state & State_KeyboardFocusChange)) {
            return;
        }
        const bool enabled = (option->state & State_Enabled) != 0;
        const QColor frame = option->palette.color(enabled ? QPalette::PlaceholderText : QPalette::Mid);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(frame, 1));
        const QRect rect = option->rect;
        if (element == PE_IndicatorCheckBox) {
            painter->translate(0.5, 0.5);  // как Fusion: линия по центру пикселя
            painter->drawRect(rect.adjusted(0, 0, -1, -1));
        } else {
            // Та же окружность, что у Fusion (qfusionstyle.cpp, PE_IndicatorRadioButton).
            const QPointF center = rect.center() + QPoint(1, 1);
            const qreal radius = (rect.width() + (rect.width() + 1) % 2) / 2.0 - 1;
            QPainterPath circle;
            circle.addEllipse(center, radius, radius);
            painter->drawPath(circle);
        }
        painter->restore();
    }
};

}  // namespace

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
        t.fusion_ = new IndicatorStyle(QStyleFactory::create(QStringLiteral("Fusion")));
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

QString Theme::baseStyleName() const {
    const auto* proxy = qobject_cast<const QProxyStyle*>(fusion_.data());
    return proxy != nullptr ? proxy->baseStyle()->name() : QString();
}

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
