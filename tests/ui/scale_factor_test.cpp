// ui.scale_factor — масштаб экрана Windows 125 % и 150 % (план 2.6; эталон 3.1 «Рендер», 6.10; view-01):
// AcroBatya.exe
// --shots all-tools под QT_SCALE_FACTOR=1.25 и 1.5 (QT_SCALE_FACTOR задаётся до QApplication — поэтому
// отдельный процесс) пишет кадр 1440 × 900 логических пикселей в пикселях устройства, а тайлы страницы (512
// px устройства) сходятся без швов: сплошная синяя шапка demo/report.pdf шириной больше тайла однородна
// поперёк его границы.
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "support/ui_test.h"

namespace {

QString exePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AcroBatya.exe"));
}

constexpr int kTile = 512;  // core/render/tiles.h — тайл в пикселях устройства

bool headerBlue(QRgb c) {
    return std::abs(qRed(c) - 28) <= 6 && std::abs(qGreen(c) - 71) <= 6 && std::abs(qBlue(c) - 140) <= 6;
}

}  // namespace

class ScaleFactorTest : public QObject {
    Q_OBJECT

private slots:
    void tilesWithoutSeams_data() {
        QTest::addColumn<double>("factor");
        QTest::newRow("125%") << 1.25;
        QTest::newRow("150%") << 1.5;
    }

    void tilesWithoutSeams() {
        QFETCH(double, factor);
        if (!QFileInfo::exists(exePath())) {
            QSKIP("AcroBatya.exe is not built next to ui_tests");
        }
        QTemporaryDir tmp;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("QT_SCALE_FACTOR"), QString::number(factor));
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        env.insert(QStringLiteral("OTTISK_DATA_DIR"), QDir(tmp.path()).filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("OTTISK_INSTANCE"), QStringLiteral("test"));
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(exePath(), {QStringLiteral("--shots"), QStringLiteral("all-tools"), QStringLiteral("--out"),
                            tmp.path()});
        QVERIFY(p.waitForFinished(90000));
        QCOMPARE(p.exitCode(), 0);
        const QImage img = QImage(QDir(tmp.path()).filePath(QStringLiteral("all-tools.png")))
                               .convertToFormat(QImage::Format_RGB32);
        QCOMPARE(img.size(), QSize(qRound(1440 * factor), qRound(900 * factor)));
        // Шапка страницы 1: столбцы, где синего больше 20 пикселей, и строки, где он — больше 60 % ширины.
        int x0 = img.width();
        int x1 = -1;
        for (int x = 0; x < img.width(); ++x) {
            int n = 0;
            for (int y = 0; y < img.height(); y += 2) {
                n += headerBlue(img.pixel(x, y)) ? 1 : 0;
            }
            if (n > 10) {
                x0 = std::min(x0, x);
                x1 = std::max(x1, x);
            }
        }
        QVERIFY(x1 > x0);
        int y0 = -1;
        int y1 = -1;
        for (int y = 0; y < img.height(); ++y) {
            int n = 0;
            for (int x = x0; x <= x1; ++x) {
                n += headerBlue(img.pixel(x, y)) ? 1 : 0;
            }
            const bool row = n * 10 > (x1 - x0 + 1) * 6;
            if (row && y0 < 0) {
                y0 = y;
            }
            if (row) {
                y1 = y;
            } else if (y0 >= 0) {
                break;
            }
        }
        QVERIFY(y1 > y0 + 20);
        // Шапка шире тайла: граница тайла (от левого края страницы) проходит через неё.
        QVERIFY2(x1 - x0 > kTile + 16, qPrintable(QStringLiteral("header %1 px").arg(x1 - x0)));
        // Верхняя треть шапки (над заголовком и эмблемой) — один цвет, без полос на стыке тайлов.
        int bad = 0;
        const QRgb ref = img.pixel((x0 + x1) / 2, y0 + 4);
        for (int y = y0 + 4; y < y0 + (y1 - y0) / 3; ++y) {
            for (int x = x0 + 4; x <= x1 - 4; ++x) {
                const QRgb c = img.pixel(x, y);
                bad += std::abs(qRed(c) - qRed(ref)) > 4 || std::abs(qGreen(c) - qGreen(ref)) > 4 ||
                               std::abs(qBlue(c) - qBlue(ref)) > 4
                           ? 1
                           : 0;
            }
        }
        QCOMPARE(bad, 0);
        std::printf("OT_FEATURE:view-01\n");
    }
};

OT_UI_TEST(ScaleFactorTest, "scale_factor");

#include "scale_factor_test.moc"
