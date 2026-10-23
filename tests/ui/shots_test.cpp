// ui.shots — снимки сцен (эталон 6.10): --shots-list печатает сцены, сцена рисует PNG 1440 × 900 нужного
// имени, витрина — с кириллицей в Segoe UI; варианты темы, масштаба и языка — суффиксы имени; закреплённые
// сценой тема и масштаб варианту не подчиняются.
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <memory>

#include "support/ui_test.h"
#include "ui/shots/scenes.h"
#include "ui/theme/theme.h"

using namespace ot::ui;

namespace {

QApplication& app() { return *qobject_cast<QApplication*>(QCoreApplication::instance()); }

double meanLuminance(const QImage& img) {
    double sum = 0;
    int n = 0;
    for (int y = 0; y < img.height(); y += 7) {
        for (int x = 0; x < img.width(); x += 7) {
            sum += qGray(img.pixel(x, y));
            ++n;
        }
    }
    return n > 0 ? sum / n : 0;
}

}  // namespace

class ShotsTest : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { Theme::apply(app(), ThemeChoice::Light); }

    void listsScenes() {
        const QStringList names = sceneNames();
        for (const char* n : {"home", "showcase", "showcase-dark", "showcase-large", "showcase-contrast"}) {
            QVERIFY2(names.contains(QLatin1String(n)), n);
        }
        QCOMPARE(QStringList(names).removeDuplicates(), qsizetype(0));
        // Сам exe: --shots-list без окна.
        const QString exe = QStringLiteral(OTTISK_BINARY_DIR "/bin/AcroBatya.exe");
        if (!QFile::exists(exe)) {
            QSKIP("AcroBatya.exe ещё не собран");
        }
        QProcess p;
        p.start(exe, {QStringLiteral("--shots-list")});
        QVERIFY(p.waitForFinished(30000));
        QCOMPARE(p.exitCode(), 0);
        const QStringList printed =
            QString::fromUtf8(p.readAllStandardOutput())
                .split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts);
        QCOMPARE(printed, names);
    }

    void fileNamesCarryVariant() {
        const Scene* home = findScene(QStringLiteral("home"));
        const Scene* dark = findScene(QStringLiteral("showcase-dark"));
        QVERIFY(home != nullptr && dark != nullptr);
        ShotOptions o;
        QCOMPARE(sceneFileName(*home, o), QStringLiteral("home.png"));
        o.theme = ThemeChoice::Dark;
        QCOMPARE(sceneFileName(*home, o), QStringLiteral("home-dark.png"));
        QCOMPARE(sceneFileName(*dark, o), QStringLiteral("showcase-dark.png"));
        o.theme = ThemeChoice::HighContrast;
        o.scale = 1.25;
        o.lang = QStringLiteral("en");
        QCOMPARE(sceneFileName(*home, o), QStringLiteral("home-contrast-large-en.png"));
        QVERIFY(findScene(QStringLiteral("nope")) == nullptr);
    }

    // Сцена showcase: PNG 1440 × 900, кириллица в Segoe UI (все буквы подписей есть в шрифте).
    void showcaseScenePng() {
        std::unique_ptr<QWidget> w(makeScene(QStringLiteral("showcase")));
        QVERIFY(w != nullptr);
        w->show();
        int cyrillic = 0;
        for (QLabel* label : w->findChildren<QLabel*>()) {
            const QString text = label->text();
            if (!text.contains(QRegularExpression(QStringLiteral("[А-Яа-яЁё]")))) {
                continue;
            }
            ++cyrillic;
            QCOMPARE(label->font().family(), QStringLiteral("Segoe UI"));
            const QFontMetrics fm(label->font());
            for (const QChar ch : text) {
                QVERIFY2(fm.inFont(ch), qPrintable(text));
            }
        }
        QVERIFY(cyrillic > 20);
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        QCOMPARE(runShots({QStringLiteral("showcase"), QStringLiteral("showcase-dark"),
                           QStringLiteral("showcase-large")},
                          o),
                 0);
        for (const char* n : {"showcase", "showcase-dark", "showcase-large"}) {
            const QImage img(QDir(dir.path()).filePath(QLatin1String(n) + QStringLiteral(".png")));
            QCOMPARE(img.size(), QSize(1440, 900));
        }
        const double light = meanLuminance(QImage(QDir(dir.path()).filePath(QStringLiteral("showcase.png"))));
        const double dark =
            meanLuminance(QImage(QDir(dir.path()).filePath(QStringLiteral("showcase-dark.png"))));
        QVERIFY2(light > 180 && dark < 90, qPrintable(QStringLiteral("%1 / %2").arg(light).arg(dark)));
    }

    void variantSkipsFixedScenes() {
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        o.theme = ThemeChoice::Dark;
        QCOMPARE(runShots({QStringLiteral("all")}, o), 0);
        const QStringList files =
            QDir(dir.path()).entryList({QStringLiteral("*.png")}, QDir::Files, QDir::Name);
        QCOMPARE(files, (QStringList{QStringLiteral("home-dark.png"), QStringLiteral("showcase-dark.png")}));
        QCOMPARE(runShots({QStringLiteral("nope")}, o), 1);
    }
};

OT_UI_TEST(ShotsTest, "shots");
#include "shots_test.moc"
