// ui.shots — снимки сцен (эталон 6.10): --shots-list печатает сцены, сцена рисует PNG 1440 × 900 нужного
// имени, витрина — с кириллицей в Segoe UI; варианты темы, масштаба и языка — суффиксы имени; закреплённые
// сценой тема и масштаб варианту не подчиняются. Сцены вида страниц view-* (2.2b) ждут растра (Scene::ready)
// и пишут PNG 1440 × 900: страницы на холсте, в тёмной теме холст тёмный, бумага белая.
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <cstdio>
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
        for (const char* n : {"home", "showcase", "showcase-dark", "showcase-large", "showcase-contrast",
                              "view-single", "view-continuous", "view-facing", "view-cover", "view-rotated",
                              "view-zoom-400", "view-dark", "view-selection", "view-loupe"}) {
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
        // Без закреплённой темы и масштаба — с суффиксом варианта; showcase-dark и view-dark — уже тёмные.
        QStringList expected;
        for (const Scene& s : sceneTable()) {
            if (!s.theme && !s.scale) {
                expected << s.name + QStringLiteral("-dark.png");
            }
        }
        expected.sort();
        QCOMPARE(files, expected);
        QVERIFY(!files.contains(QStringLiteral("view-dark.png")));
        QCOMPARE(runShots({QStringLiteral("nope")}, o), 1);
    }

    // Сцены вида страниц: каждая дожидается растра и пишет PNG 1440 × 900; страница на холсте видна.
    void viewScenesPng() {
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        QStringList names;
        for (const QString& n : sceneNames()) {
            if (n.startsWith(QStringLiteral("view-"))) {
                names << n;
            }
        }
        // 9 сцен PageView (2.2b) + view-night, view-high-contrast, view-overprint, view-boxes-overlay (2.5b).
        QCOMPARE(names.size(), 13);
        QCOMPARE(runShots(names, o), 0);
        for (const QString& n : names) {
            const QImage img(QDir(dir.path()).filePath(n + QStringLiteral(".png")));
            QCOMPARE(img.size(), QSize(1440, 900));
            if (n == QStringLiteral("view-zoom-400")) {
                continue;  // на 400 % весь кадр — бумага
            }
            // Страницы на холсте: заметная доля кадра — не цвет холста в углу.
            const QColor canvas = img.pixelColor(2, 2);
            int page = 0;
            int total = 0;
            for (int y = 0; y < img.height(); y += 9) {
                for (int x = 0; x < img.width(); x += 9) {
                    page += img.pixelColor(x, y) != canvas ? 1 : 0;
                    ++total;
                }
            }
            QVERIFY2(page * 100 > total * 15,
                     qPrintable(n + QStringLiteral(": %1 из %2").arg(page).arg(total)));
        }
        const QImage dark(QDir(dir.path()).filePath(QStringLiteral("view-dark.png")));
        QVERIFY(qGray(dark.pixel(2, 2)) < 90);  // холст тёмный
        int white = 0;                          // бумага белая (кадр середины по горизонтали)
        for (int y = 0; y < dark.height(); y += 5) {
            white += qGray(dark.pixel(dark.width() / 2, y)) > 245 ? 1 : 0;
        }
        QVERIFY2(white > dark.height() / 5 / 3, qPrintable(QString::number(white)));
        const QImage selection(QDir(dir.path()).filePath(QStringLiteral("view-selection.png")));
        bool tinted = false;  // подсветка выделения/поиска — не серая
        for (int y = 0; y < selection.height() && !tinted; y += 3) {
            for (int x = 0; x < selection.width() && !tinted; x += 3) {
                const QColor c = selection.pixelColor(x, y);
                tinted = c.blue() > c.red() + 25;
            }
        }
        QVERIFY(tinted);
    }

    // ui.shots.stage2 (план 2.6): главные экраны этапа 2 (оболочка, Главная, панели, строка состояния,
    // «Свойства», Установки, защищённый просмотр, «Печать») в тёмной теме — кадр 1440 × 900, глобальная
    // панель и холст тёмные, текст светлый (токены kDarkPalette, не светлые остатки).
    void stage2() {
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        o.theme = ThemeChoice::Dark;
        const QStringList names = {
            QStringLiteral("shell-document"),      QStringLiteral("all-tools"),
            QStringLiteral("home-recent"),         QStringLiteral("pane-thumbnails"),
            QStringLiteral("status-bar"),          QStringLiteral("properties-description"),
            QStringLiteral("preferences-general"), QStringLiteral("protected-view"),
            QStringLiteral("print-dialog"),        QStringLiteral("print-dialog-booklet")};
        for (const QString& n : names) {
            QVERIFY2(sceneNames().contains(n), qPrintable(n));
        }
        QCOMPARE(runShots(names, o), 0);
        for (const QString& n : names) {
            const QImage img(QDir(dir.path()).filePath(n + QStringLiteral("-dark.png")));
            QVERIFY2(!img.isNull(), qPrintable(n));
            QCOMPARE(img.size(), QSize(1440, 900));
            // Глобальная панель (48 px сверху): фон тёмный, есть светлые подписи.
            long sum = 0;
            int light = 0;
            int count = 0;
            for (int y = 4; y < 44; y += 2) {
                for (int x = 0; x < img.width(); x += 4) {
                    const int g = qGray(img.pixel(x, y));
                    sum += g;
                    light += g > 170 ? 1 : 0;
                    ++count;
                }
            }
            QVERIFY2(sum / count < 90, qPrintable(n + QStringLiteral(": %1").arg(sum / count)));
            QVERIFY2(light > 20, qPrintable(n));
        }
        std::printf("OT_FEATURE:view-25\n");
    }

    // ui.shots.stage3 (план 3.4, п. 5): сцены редактора — светлые и тёмные (закреплённая тема) — снимаются,
    // кадр 1440 × 900; у тёмных глобальная панель тёмная; полоса «Похоже на скан» на скане.
    void stage3() {
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        const QStringList names = {QStringLiteral("edit-mode"),
                                   QStringLiteral("edit-caret"),
                                   QStringLiteral("edit-readonly-block"),
                                   QStringLiteral("edit-format-panel"),
                                   QStringLiteral("edit-add-text"),
                                   QStringLiteral("edit-image-selected"),
                                   QStringLiteral("edit-image-crop"),
                                   QStringLiteral("find-replace"),
                                   QStringLiteral("preferences-editing"),
                                   QStringLiteral("scan-bar"),
                                   QStringLiteral("edit-mode-dark"),
                                   QStringLiteral("edit-format-panel-dark"),
                                   QStringLiteral("edit-image-selected-dark")};
        for (const QString& n : names) {
            QVERIFY2(sceneNames().contains(n), qPrintable(n));
        }
        QCOMPARE(runShots(names, o), 0);
        for (const QString& n : names) {
            const QImage img(QDir(dir.path()).filePath(n + QStringLiteral(".png")));
            QVERIFY2(!img.isNull(), qPrintable(n));
            QCOMPARE(img.size(), QSize(1440, 900));
            long sum = 0;
            int count = 0;
            for (int y = 4; y < 44; y += 2) {
                for (int x = 0; x < img.width(); x += 4) {
                    sum += qGray(img.pixel(x, y));
                    ++count;
                }
            }
            const bool dark = n.endsWith(QLatin1String("-dark"));
            QVERIFY2(dark ? sum / count < 90 : sum / count > 150,
                     qPrintable(n + QStringLiteral(": %1").arg(sum / count)));
        }
    }

    // ui.shots.stage4 (план 4.6, п. 4): сцены подписей — светлые и тёмные (закреплённая тема) — снимаются,
    // кадр 1440 × 900; у тёмных глобальная панель тёмная.
    void stage4() {
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        const QStringList names = {QStringLiteral("sign-dialog"),
                                   QStringLiteral("certify-dialog"),
                                   QStringLiteral("sign-appearance-dialog"),
                                   QStringLiteral("sign-warnings"),
                                   QStringLiteral("sig-bar-valid"),
                                   QStringLiteral("sig-bar-problems"),
                                   QStringLiteral("pane-signatures"),
                                   QStringLiteral("certificate-viewer"),
                                   QStringLiteral("preferences-identities"),
                                   QStringLiteral("preferences-trusted"),
                                   QStringLiteral("preferences-tsa"),
                                   QStringLiteral("preferences-verification"),
                                   QStringLiteral("sign-dialog-dark"),
                                   QStringLiteral("sig-bar-problems-dark"),
                                   QStringLiteral("pane-signatures-dark"),
                                   QStringLiteral("certificate-viewer-dark"),
                                   QStringLiteral("preferences-trusted-dark")};
        for (const QString& n : names) {
            QVERIFY2(sceneNames().contains(n), qPrintable(n));
        }
        QCOMPARE(runShots(names, o), 0);
        for (const QString& n : names) {
            const QImage img(QDir(dir.path()).filePath(n + QStringLiteral(".png")));
            QVERIFY2(!img.isNull(), qPrintable(n));
            QCOMPARE(img.size(), QSize(1440, 900));
            long sum = 0;
            int count = 0;
            for (int y = 4; y < 44; y += 2) {
                for (int x = 0; x < img.width(); x += 4) {
                    sum += qGray(img.pixel(x, y));
                    ++count;
                }
            }
            const bool dark = n.endsWith(QLatin1String("-dark"));
            QVERIFY2(dark ? sum / count < 90 : sum / count > 150,
                     qPrintable(n + QStringLiteral(": %1").arg(sum / count)));
        }
    }

    // ui.shots.stage5-fillsign (план 5.3, п. 4): сцены «Заполнить и подписать» — светлые и тёмные
    // (закреплённая тема) — снимаются, кадр 1440 × 900; у тёмных глобальная панель тёмная, у светлых —
    // светлая.
    void stage5fillsign() {
        QTemporaryDir dir;
        ShotOptions o;
        o.outDir = dir.path();
        const QStringList names = {
            QStringLiteral("fill-sign-capture-type"),  QStringLiteral("fill-sign-capture-draw"),
            QStringLiteral("fill-sign-capture-image"), QStringLiteral("fill-sign-text"),
            QStringLiteral("fill-sign-comb"),          QStringLiteral("fill-sign-marks"),
            QStringLiteral("fill-sign-placed"),        QStringLiteral("fill-sign-flatten-dialog"),
            QStringLiteral("fill-sign-text-dark"),     QStringLiteral("fill-sign-capture-draw-dark"),
            QStringLiteral("fill-sign-placed-dark")};
        for (const QString& n : names) {
            QVERIFY2(sceneNames().contains(n), qPrintable(n));
        }
        QCOMPARE(runShots(names, o), 0);
        for (const QString& n : names) {
            const QImage img(QDir(dir.path()).filePath(n + QStringLiteral(".png")));
            QVERIFY2(!img.isNull(), qPrintable(n));
            QCOMPARE(img.size(), QSize(1440, 900));
            long sum = 0;
            int count = 0;
            for (int y = 4; y < 44; y += 2) {
                for (int x = 0; x < img.width(); x += 4) {
                    sum += qGray(img.pixel(x, y));
                    ++count;
                }
            }
            const bool dark = n.endsWith(QLatin1String("-dark"));
            QVERIFY2(dark ? sum / count < 90 : sum / count > 150,
                     qPrintable(n + QStringLiteral(": %1").arg(sum / count)));
        }
    }
};

OT_UI_TEST(ShotsTest, "shots");
#include "shots_test.moc"
