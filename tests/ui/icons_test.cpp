// ui.icons — иконки Lucide (эталон 6.5): каждое имя списков есть в assets/icons и в ресурсах
// (ui.icons.exist), таблица инструментов и панелей ссылается только на них; Icons::get красит штрих токеном,
// ставит stroke-width 1,75, кэширует по (имя, цвет, размер, dpr); QIcon из Icons::icon перекрашивается сменой
// темы.
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <algorithm>

#include "support/ui_test.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

using ot::ui::Icons;
using ot::ui::IconTone;
using ot::ui::Theme;
using ot::ui::ThemeChoice;
using ot::ui::ThemeKind;
using ot::ui::Tokens;

namespace {

QApplication& app() { return *qobject_cast<QApplication*>(QCoreApplication::instance()); }

QStringList readList(const QString& rel) {
    QFile f(QStringLiteral(OTTISK_SOURCE_DIR "/") + rel);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    QStringList names;
    for (const QByteArray& raw : f.readAll().split('\n')) {
        const QString line =
            QString::fromUtf8(raw.left(raw.indexOf('#') < 0 ? raw.size() : raw.indexOf('#'))).trimmed();
        if (!line.isEmpty()) {
            names << line;
        }
    }
    return names;
}

// Цвет самого непрозрачного пикселя (штрих без сглаживания) — им иконка и покрашена.
QColor inkColor(const QPixmap& pm) {
    const QImage img = pm.toImage().convertToFormat(QImage::Format_ARGB32);
    QColor best(0, 0, 0, 0);
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QColor c = QColor::fromRgba(img.pixel(x, y));
            if (c.alpha() > best.alpha()) {
                best = c;
            }
        }
    }
    return best;
}

bool near(const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) <= 6 && std::abs(a.green() - b.green()) <= 6 &&
           std::abs(a.blue() - b.blue()) <= 6;
}

}  // namespace

class IconsTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { Theme::apply(app(), ThemeChoice::Light); }

    // ui.icons.exist: списки → файлы → ресурсы; таблица эталона 6.5 — из этих имён; LICENSE рядом.
    void exist() {
        const QStringList names = readList(QStringLiteral("src/ui/theme/icon_names.txt")) +
                                  readList(QStringLiteral("assets/icons/icons.txt"));
        QVERIFY(names.size() >= 40);
        for (const QString& n : names) {
            QVERIFY2(QFile::exists(QStringLiteral(OTTISK_SOURCE_DIR "/assets/icons/") + n +
                                   QStringLiteral(".svg")),
                     qPrintable(n));
            QVERIFY2(Icons::exists(n), qPrintable(n));
        }
        QVERIFY(QFile::exists(QStringLiteral(OTTISK_SOURCE_DIR "/assets/icons/LICENSE")));
        QCOMPARE(std::size(ot::ui::kToolIcons), size_t(16));
        QCOMPARE(std::size(ot::ui::kPaneIcons), size_t(8));
        for (const auto& t : ot::ui::kToolIcons) {
            QVERIFY2(names.contains(QLatin1String(t.icon)), t.icon);
        }
        for (const auto& t : ot::ui::kPaneIcons) {
            QVERIFY2(names.contains(QLatin1String(t.icon)), t.icon);
        }
        QCOMPARE(QString::fromLatin1(ot::ui::toolIconName("comment")), QStringLiteral("message-square"));
        QCOMPARE(QString::fromLatin1(ot::ui::toolIconName("javascript")), QStringLiteral("braces"));
        QCOMPARE(QString::fromLatin1(ot::ui::toolIconName("signatures")), QStringLiteral("file-signature"));
        QVERIFY(!Icons::exists(QStringLiteral("no-such-icon")));
        // Встроены все SVG из assets/icons, и только они.
        QCOMPARE(Icons::names().size(), names.size());
    }

    void substitutesStrokeColorAndWidth() {
        const QColor c(0x12, 0x34, 0x56);
        const QByteArray svg = Icons::svgData(QStringLiteral("printer"), c);
        QVERIFY(svg.contains("stroke=\"#123456\""));
        QVERIFY(svg.contains("stroke-width=\"1.75\""));
        QVERIFY(!svg.contains("currentColor"));
        QVERIFY(!svg.contains("stroke-width=\"2\""));
    }

    void recolorsByTone() {
        const auto& p = Tokens::palette(ThemeKind::Light);
        const QPixmap text = Icons::get(QStringLiteral("printer"), IconTone::Text, 48, 1.0);
        QCOMPARE(text.size(), QSize(48, 48));
        QVERIFY(near(inkColor(text), p.text.color()));
        QVERIFY(near(inkColor(Icons::get(QStringLiteral("printer"), IconTone::Accent, 48, 1.0)),
                     p.accent.color()));
        QVERIFY(near(inkColor(Icons::get(QStringLiteral("x"), IconTone::Danger, 48, 1.0)), p.danger.color()));
        QVERIFY(
            near(inkColor(Icons::get(QStringLiteral("x"), QColor(0, 128, 0), 48, 1.0)), QColor(0, 128, 0)));
    }

    void cachesByNameColorSizeDpr() {
        Icons::clearCache();
        const QPixmap a = Icons::get(QStringLiteral("search"), IconTone::Text, 20, 1.0);
        QCOMPARE(Icons::cacheSize(), 1);
        const QPixmap b = Icons::get(QStringLiteral("search"), IconTone::Text, 20, 1.0);
        QCOMPARE(a.cacheKey(), b.cacheKey());
        QCOMPARE(Icons::cacheSize(), 1);
        Icons::get(QStringLiteral("search"), IconTone::Muted, 20, 1.0);
        Icons::get(QStringLiteral("search"), IconTone::Text, 16, 1.0);
        const QPixmap hi = Icons::get(QStringLiteral("search"), IconTone::Text, 20, 2.0);
        QCOMPARE(Icons::cacheSize(), 4);
        QCOMPARE(hi.size(), QSize(40, 40));
        QCOMPARE(hi.deviceIndependentSize(), QSizeF(20, 20));
    }

    // Перекраска по themeChanged: QIcon из Icons::icon рисуется цветом текущей темы, состояние «вкл» —
    // accent.
    void iconFollowsTheme() {
        const QIcon icon = Icons::icon(QStringLiteral("printer"));
        QVERIFY(!icon.isNull());
        QVERIFY(
            near(inkColor(icon.pixmap(QSize(48, 48), 1.0)), Tokens::palette(ThemeKind::Light).text.color()));
        QVERIFY(near(inkColor(icon.pixmap(QSize(48, 48), 1.0, QIcon::Normal, QIcon::On)),
                     Tokens::palette(ThemeKind::Light).accent.color()));
        Theme::apply(app(), ThemeChoice::Dark);
        QVERIFY(
            near(inkColor(icon.pixmap(QSize(48, 48), 1.0)), Tokens::palette(ThemeKind::Dark).text.color()));
        const QColor disabled = inkColor(icon.pixmap(QSize(48, 48), 1.0, QIcon::Disabled));
        QVERIFY(!near(disabled, Tokens::palette(ThemeKind::Dark).text.color()));
    }
};

OT_UI_TEST(IconsTest, "icons");
#include "icons_test.moc"
