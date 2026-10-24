// ui.dwrite — живой DirectWrite (план 3.2, п. 2; эталон 3.9 «FontResolver», edit-49 — поиск шрифта):
// системная коллекция находит Arial (файл arial.ttf и PostScript-имя ArialMT), жирный и курсив — свои файлы,
// грань по PostScript-имени и по полному имени, курсив Tahoma — синтетика (DWRITE_FONT_SIMULATIONS_OBLIQUE),
// чужое семейство — нет; список семейств по алфавиту с Arial и Segoe UI; вызов из другого потока — тот же
// ответ. Набор в ui_tests: живой DirectWrite нужен только здесь, остальное — FakeSystemFonts
// (unit.system_fonts).
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include "core/edit/system_fonts.h"
#include "core/util/features.h"
#include "services/system_fonts_dwrite.h"
#include "support/ui_test.h"

namespace {

QString fileOf(const std::optional<ot::edit::SystemFace>& f) {
    return f ? QString::fromStdWString(f->file.filename().wstring()).toLower() : QStringLiteral("<none>");
}

}  // namespace

class DWriteTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        OT_FEATURE("edit-49");
        if (!ot::DWriteSystemFonts().available()) {
            QSKIP("DirectWrite недоступен");
        }
    }

    void findsArial() {
        ot::DWriteSystemFonts fonts;
        const auto arial = fonts.face("Arial", 400, false);
        QVERIFY(arial.has_value());
        QCOMPARE(fileOf(arial), QStringLiteral("arial.ttf"));
        QCOMPARE(QString::fromStdString(arial->family), QStringLiteral("Arial"));
        QCOMPARE(QString::fromStdString(arial->postscript), QStringLiteral("ArialMT"));
        QCOMPARE(arial->weight, 400);
        QVERIFY(!arial->italic);
        QVERIFY(!arial->simulatedBold);
        QVERIFY(std::filesystem::exists(arial->file));
        QCOMPARE(arial->index, 0);
    }

    void stylesHaveOwnFiles() {
        ot::DWriteSystemFonts fonts;
        QCOMPARE(fileOf(fonts.face("Arial", 700, false)), QStringLiteral("arialbd.ttf"));
        QCOMPARE(fileOf(fonts.face("Arial", 400, true)), QStringLiteral("ariali.ttf"));
        QCOMPARE(fileOf(fonts.face("arial", 700, true)), QStringLiteral("arialbi.ttf"));
        QCOMPARE(fileOf(fonts.face("Times New Roman", 400, false)), QStringLiteral("times.ttf"));
        QCOMPARE(fileOf(fonts.face("Courier New", 700, false)), QStringLiteral("courbd.ttf"));
    }

    void byName() {
        ot::DWriteSystemFonts fonts;
        QCOMPARE(fileOf(fonts.byName("ArialMT")), QStringLiteral("arial.ttf"));
        QCOMPARE(fileOf(fonts.byName("Arial-BoldMT")), QStringLiteral("arialbd.ttf"));
        QCOMPARE(fileOf(fonts.byName("TimesNewRomanPS-ItalicMT")), QStringLiteral("timesi.ttf"));
        QCOMPARE(fileOf(fonts.byName("Arial Bold")), QStringLiteral("arialbd.ttf"));
        QVERIFY(!fonts.byName("NoSuchFont-Regular").has_value());
        // Поиск шрифта по имени из PDF — через тот же DirectWrite.
        ot::edit::FontQuery q;
        q.name = "ABCDEF+Arial-BoldMT";
        q.weight = 700;
        QCOMPARE(fileOf(ot::edit::findSystemFont(fonts, q)), QStringLiteral("arialbd.ttf"));
        q.name = "Helvetica";
        q.weight = 400;
        QCOMPARE(fileOf(ot::edit::findSystemFont(fonts, q)), QStringLiteral("arial.ttf"));
    }

    void simulations() {
        ot::DWriteSystemFonts fonts;
        const auto tahoma = fonts.face("Tahoma", 400, true);  // курсива у Tahoma нет
        QVERIFY(tahoma.has_value());
        QCOMPARE(fileOf(tahoma), QStringLiteral("tahoma.ttf"));
        QVERIFY(tahoma->simulatedItalic);
        QVERIFY(!tahoma->simulatedBold);
        QVERIFY(!fonts.face("Fantasy Sans", 400, false).has_value());
    }

    void families() {
        ot::DWriteSystemFonts fonts;
        const auto list = fonts.families();
        QVERIFY(list.size() > 10);
        QVERIFY(std::is_sorted(list.begin(), list.end()));
        QVERIFY(std::find(list.begin(), list.end(), "Arial") != list.end());
        QVERIFY(std::find(list.begin(), list.end(), "Segoe UI") != list.end());
    }

    void otherThread() {
        // Резолвер шрифтов зовёт DirectWrite из потока документа.
        ot::edit::SystemFonts& fonts = ot::systemFonts();
        const auto arial = QtConcurrent::run([&] { return fonts.face("Arial", 400, false); }).result();
        QCOMPARE(fileOf(arial), QStringLiteral("arial.ttf"));
    }
};

OT_UI_TEST(DWriteTest, "dwrite");

#include "dwrite_test.moc"
