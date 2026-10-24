// ui.theme — токены и тема (эталон 6.4, 6.5): контраст текста ≥ 4,5:1 в трёх палитрах (формула WCAG —
// support/contrast.h), QPalette и шрифт из токенов, смена темы без перезапуска, «Системная» по QStyleHints,
// «Крупные элементы» × 1,25, рамки флажков и переключателей видны (≥ 3:1).
#include <QAccessibilityHints>
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleHints>
#include <QStyleOptionButton>
#include <QVBoxLayout>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstdio>

#include "support/contrast.h"
#include "support/ui_test.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"

using ot::test::contrastRatio;
using ot::ui::Metrics;
using ot::ui::Palette;
using ot::ui::Theme;
using ot::ui::ThemeChoice;
using ot::ui::ThemeKind;
using ot::ui::Tokens;

namespace {

const char* kindName(ThemeKind k) {
    switch (k) {
        case ThemeKind::Light:
            return "light";
        case ThemeKind::Dark:
            return "dark";
        case ThemeKind::HighContrast:
            return "contrast";
    }
    return "?";
}

QApplication& app() { return *qobject_cast<QApplication*>(QCoreApplication::instance()); }

}  // namespace

class ThemeTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { Theme::apply(app(), ThemeChoice::Light); }

    // ui.theme.contrast: text на surface/surfaceAlt/canvas, подписи, главная кнопка, выбранная строка,
    // наведение.
    void contrast() {
        for (ThemeKind kind : {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast}) {
            const Palette& p = Tokens::palette(kind);
            const struct {
                const char* what;
                ot::ui::Rgba fg, bg;
            } pairs[] = {
                {"text/surface", p.text, p.surface},
                {"text/surfaceAlt", p.text, p.surfaceAlt},
                {"text/canvas", p.text, p.canvas},
                {"textMuted/surface", p.textMuted, p.surface},
                {"textMuted/surfaceAlt", p.textMuted, p.surfaceAlt},
                {"onAccent/accent", p.onAccent, p.accent},
                {"text/accentSoft", p.text, p.accentSoft},
                {"text/hover", p.text, p.hover},
                {"text/formField", p.text, p.formField},
            };
            for (const auto& pair : pairs) {
                const double ratio = contrastRatio(pair.fg.color(), pair.bg.color());
                QVERIFY2(ratio >= 4.5,
                         qPrintable(QStringLiteral("%1 %2: %3:1")
                                        .arg(QLatin1String(kindName(kind)), QLatin1String(pair.what))
                                        .arg(ratio, 0, 'f', 2)));
            }
        }
        // «Высокая контрастность» — не ниже AAA (7:1) для основного текста.
        const Palette& hc = Tokens::palette(ThemeKind::HighContrast);
        QVERIFY(contrastRatio(hc.text.color(), hc.surface.color()) >= 7.0);
        QVERIFY(contrastRatio(hc.text.color(), hc.canvas.color()) >= 7.0);
        std::printf("OT_FEATURE:ocr-45\n");
    }

    // Те же пары, из которых собран QSS компонентов (theme/qss.cpp), — во всех трёх палитрах.
    void componentColorsContrast() {
        for (ThemeKind kind : {ThemeKind::Light, ThemeKind::Dark, ThemeKind::HighContrast}) {
            const auto pairs = ot::ui::componentColors(Tokens::palette(kind));
            QVERIFY(pairs.size() >= 10);
            for (const auto& pair : pairs) {
                const double ratio = contrastRatio(pair.fg, pair.bg);
                QVERIFY2(ratio >= 4.5,
                         qPrintable(QStringLiteral("%1 %2 %3: %4:1")
                                        .arg(QLatin1String(kindName(kind)), pair.component, pair.state)
                                        .arg(ratio, 0, 'f', 2)));
            }
        }
    }

    void tokensMatchDesignTable() {
        const Palette& l = Tokens::palette(ThemeKind::Light);
        const Palette& d = Tokens::palette(ThemeKind::Dark);
        QCOMPARE(l.accent.color(), QColor(0xB2, 0x43, 0x2F));  // «сургуч»
        QCOMPARE(l.surface.color(), QColor(0xFF, 0xFF, 0xFF));
        QCOMPARE(l.canvas.color(), QColor(0xE6, 0xE6, 0xE8));
        QCOMPARE(d.surface.color(), QColor(0x1F, 0x1F, 0x1F));
        QCOMPARE(d.accent.color(), QColor(0xD9, 0x63, 0x4E));
        QCOMPARE(d.canvas.color(), QColor(0x12, 0x12, 0x12));
        QCOMPARE(l.selection.a, std::uint8_t(77));  // rgba(43,108,196,.30)
        const Metrics m = Tokens::base();
        QCOMPARE(m.globalBar, 48);
        QCOMPARE(m.leftPanel, 320);
        QCOMPARE(m.leftPanelCollapsed, 48);
        QCOMPARE(m.navButton, 44);
        QCOMPARE(m.rightPanel, 280);
        QCOMPARE(m.statusBar, 32);
        QCOMPARE(m.tabHeight, 36);
        QCOMPARE(m.row, 32);
        QCOMPARE(m.radius, 4);
        QCOMPARE(m.radiusLarge, 8);
        QCOMPARE(m.motionMs, 120);
        QCOMPARE(m.grid, 4);
    }

    void paletteFontAndStyleFromTokens() {
        Theme::apply(app(), ThemeChoice::Light);
        QCOMPARE(Theme::instance().baseStyleName(), QStringLiteral("fusion"));
        const QPalette pal = QApplication::palette();
        const Palette& l = Tokens::palette(ThemeKind::Light);
        QCOMPARE(pal.color(QPalette::Window), l.surface.color());
        QCOMPARE(pal.color(QPalette::Base), l.surface.color());
        QCOMPARE(pal.color(QPalette::AlternateBase), l.surfaceAlt.color());
        QCOMPARE(pal.color(QPalette::WindowText), l.text.color());
        QCOMPARE(pal.color(QPalette::Text), l.text.color());
        QCOMPARE(pal.color(QPalette::Highlight), l.accent.color());
        QCOMPARE(pal.color(QPalette::HighlightedText), l.onAccent.color());
        QCOMPARE(pal.color(QPalette::Mid), l.border.color());
        QCOMPARE(pal.color(QPalette::Dark), l.canvas.color());
        QCOMPARE(pal.color(QPalette::PlaceholderText), l.textMuted.color());
        QCOMPARE(pal.color(QPalette::Accent), l.accent.color());
        QCOMPARE(QApplication::font().family(), QStringLiteral("Segoe UI"));
        QCOMPARE(QApplication::font().pointSizeF(), 9.0);
        QVERIFY(!app().styleSheet().isEmpty());
        QCOMPARE(Theme::instance().kind(), ThemeKind::Light);
    }

    // Рамка флажка и переключателя видна во всех трёх палитрах (рубеж Р1: у Fusion на тёмной — #181818 на
    // #1F1F1F): у пустого индикатора есть пиксель с контрастом к фону ≥ 3:1 (WCAG 1.4.11). Снимки —
    // build/<пресет>/ui-shots/indicators-<тема>.png.
    void indicatorsVisible() {
        for (const ThemeChoice choice : {ThemeChoice::Light, ThemeChoice::Dark, ThemeChoice::HighContrast}) {
            Theme::apply(app(), choice);
            const Palette& p = Tokens::palette(Theme::instance().kind());
            QWidget w;
            auto* layout = new QVBoxLayout(&w);
            auto* box = new QCheckBox(QStringLiteral("box"));
            auto* radio = new QRadioButton(QStringLiteral("radio"));
            auto* checked = new QCheckBox(QStringLiteral("checked"));
            checked->setChecked(true);
            layout->addWidget(box);
            layout->addWidget(radio);
            layout->addWidget(checked);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            const QImage shot = w.grab().toImage().convertToFormat(QImage::Format_RGB32);
            const QString dir = QCoreApplication::applicationDirPath() + QStringLiteral("/../ui-shots");
            QDir().mkpath(dir);
            shot.save(
                dir +
                QStringLiteral("/indicators-%1.png").arg(QLatin1String(kindName(Theme::instance().kind()))));
            const auto best = [&](QAbstractButton* b, QStyle::SubElement e) {
                QStyleOptionButton opt;
                opt.initFrom(b);
                const QRect r = b->style()->subElementRect(e, &opt, b).translated(b->pos());
                double ratio = 1.0;
                for (int y = r.top(); y <= r.bottom(); ++y) {
                    for (int x = r.left(); x <= r.right(); ++x) {
                        ratio = std::max(ratio, contrastRatio(QColor(shot.pixel(x, y)), p.surface.color()));
                    }
                }
                return ratio;
            };
            const double boxRatio = best(box, QStyle::SE_CheckBoxIndicator);
            const double radioRatio = best(radio, QStyle::SE_RadioButtonIndicator);
            QVERIFY2(
                boxRatio >= 3.0,
                qPrintable(
                    QStringLiteral("%1 checkbox %2").arg(kindName(Theme::instance().kind())).arg(boxRatio)));
            QVERIFY2(
                radioRatio >= 3.0,
                qPrintable(
                    QStringLiteral("%1 radio %2").arg(kindName(Theme::instance().kind())).arg(radioRatio)));
        }
    }

    // Смена темы без перезапуска: палитра всех уже созданных виджетов меняется, themeChanged — один раз.
    void switchesThemeWithoutRestart() {
        QWidget root;
        auto* layout = new QVBoxLayout(&root);
        auto* label = new QLabel(QStringLiteral("Подпись"), &root);
        auto* edit = new QLineEdit(&root);
        auto* button = new QPushButton(QStringLiteral("Кнопка"), &root);
        layout->addWidget(label);
        layout->addWidget(edit);
        layout->addWidget(button);
        root.show();
        QVERIFY(QTest::qWaitForWindowExposed(&root));
        QSignalSpy changed(&Theme::instance(), &Theme::themeChanged);
        Theme::apply(app(), ThemeChoice::Dark);
        QCOMPARE(changed.count(), 1);
        const Palette& d = Tokens::palette(ThemeKind::Dark);
        for (QWidget* w : {static_cast<QWidget*>(&root), static_cast<QWidget*>(label),
                           static_cast<QWidget*>(edit), static_cast<QWidget*>(button)}) {
            QCOMPARE(w->palette().color(QPalette::Window), d.surface.color());
            QCOMPARE(w->palette().color(QPalette::WindowText), d.text.color());
        }
        QCOMPARE(edit->palette().color(QPalette::Base), d.surface.color());
        QCOMPARE(Theme::instance().kind(), ThemeKind::Dark);
        QCOMPARE(Theme::instance().choice(), ThemeChoice::Dark);
        Theme::apply(app(), ThemeChoice::Light);
        QCOMPARE(changed.count(), 2);
        QCOMPARE(label->palette().color(QPalette::WindowText),
                 Tokens::palette(ThemeKind::Light).text.color());
        std::printf("OT_FEATURE:view-25\n");
    }

    void systemThemeFollowsColorScheme() {
        QCOMPARE(
            Theme::resolve(ThemeChoice::System, Qt::ColorScheme::Dark, Qt::ContrastPreference::NoPreference),
            ThemeKind::Dark);
        QCOMPARE(
            Theme::resolve(ThemeChoice::System, Qt::ColorScheme::Light, Qt::ContrastPreference::NoPreference),
            ThemeKind::Light);
        QCOMPARE(Theme::resolve(ThemeChoice::System, Qt::ColorScheme::Unknown,
                                Qt::ContrastPreference::NoPreference),
                 ThemeKind::Light);
        QCOMPARE(
            Theme::resolve(ThemeChoice::System, Qt::ColorScheme::Light, Qt::ContrastPreference::HighContrast),
            ThemeKind::HighContrast);
        QCOMPARE(
            Theme::resolve(ThemeChoice::Light, Qt::ColorScheme::Dark, Qt::ContrastPreference::HighContrast),
            ThemeKind::Light);
        QCOMPARE(Theme::resolve(ThemeChoice::HighContrast, Qt::ColorScheme::Light,
                                Qt::ContrastPreference::NoPreference),
                 ThemeKind::HighContrast);
        // Под offscreen схема системы неизвестна — «Системная» даёт светлую.
        Theme::apply(app(), ThemeChoice::System);
        QCOMPARE(Theme::instance().choice(), ThemeChoice::System);
        QCOMPARE(Theme::instance().kind(),
                 Theme::resolve(ThemeChoice::System, QGuiApplication::styleHints()->colorScheme(),
                                QGuiApplication::styleHints()->accessibility()->contrastPreference()));
    }

    void parsesThemeNames() {
        QCOMPARE(ot::ui::parseThemeChoice(QStringLiteral("dark")), ThemeChoice::Dark);
        QCOMPARE(ot::ui::parseThemeChoice(QStringLiteral("light")), ThemeChoice::Light);
        QCOMPARE(ot::ui::parseThemeChoice(QStringLiteral("system")), ThemeChoice::System);
        QCOMPARE(ot::ui::parseThemeChoice(QStringLiteral("contrast")), ThemeChoice::HighContrast);
        QVERIFY(!ot::ui::parseThemeChoice(QStringLiteral("purple")).has_value());
        QCOMPARE(ot::ui::themeChoiceName(ThemeChoice::HighContrast), QStringLiteral("contrast"));
    }

    // «Крупные элементы» (ocr-42): Tokens::scaled(1,25) — все размеры × 1,25, время движения не меняется.
    void scaledMetrics() {
        const Metrics base = Tokens::base();
        const Metrics big = Tokens::scaled(1.25);
        for (int Metrics::* field : ot::ui::kMetricSizes) {
            QCOMPARE(big.*field, static_cast<int>(base.*field * 1.25 + 0.5));
            QVERIFY(big.*field > base.*field);
        }
        QCOMPARE(big.globalBar, 60);
        QCOMPARE(big.leftPanel, 400);
        QCOMPARE(big.rightPanel, 350);
        QCOMPARE(big.statusBar, 40);
        QCOMPARE(big.navButton, 55);
        QCOMPARE(big.motionMs, base.motionMs);
        QCOMPARE(big.fontPt, 11.25);
        QCOMPARE(big.scale, 1.25);
        const Metrics same = Tokens::scaled(1.0);
        for (int Metrics::* field : ot::ui::kMetricSizes) {
            QCOMPARE(same.*field, base.*field);
        }
        Theme::apply(app(), ThemeChoice::Light, 1.25);
        QCOMPARE(Theme::instance().scale(), 1.25);
        QCOMPARE(Theme::instance().metrics().globalBar, 60);
        QCOMPARE(QApplication::font().pointSizeF(), 11.25);
        std::printf("OT_FEATURE:ocr-42\n");
    }

    void highContrastIsThirdPalette() {
        Theme::apply(app(), ThemeChoice::HighContrast);
        QCOMPARE(Theme::instance().kind(), ThemeKind::HighContrast);
        const Palette& hc = Tokens::palette(ThemeKind::HighContrast);
        QCOMPARE(QApplication::palette().color(QPalette::Window), hc.surface.color());
        QCOMPARE(QApplication::palette().color(QPalette::Text), hc.text.color());
        QVERIFY(hc.surface != Tokens::palette(ThemeKind::Dark).surface ||
                hc.text != Tokens::palette(ThemeKind::Dark).text);
    }

    void fontsFromTokens() {
        const Metrics m = Tokens::base();
        QCOMPARE(ot::ui::uiFont(m).family(), QStringLiteral("Segoe UI"));
        QCOMPARE(ot::ui::panelTitleFont(m).pixelSize(), 13);
        QCOMPARE(ot::ui::panelTitleFont(m).weight(), QFont::DemiBold);
        QCOMPARE(ot::ui::homeTitleFont(m).pixelSize(), 20);
        QCOMPARE(ot::ui::panelTitleFont(Tokens::scaled(1.25)).pixelSize(), 16);
        // Кириллица есть в гарнитуре (offscreen берёт шрифты из QT_QPA_FONTDIR).
        QVERIFY(QFontMetrics(ot::ui::uiFont(m)).inFontUcs4(U'Ж'));
    }
};

OT_UI_TEST(ThemeTest, "theme");
#include "theme_test.moc"
