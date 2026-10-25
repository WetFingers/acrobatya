// unit.measure_csv — экспорт измерений в CSV (план 5.5, п. 3; эталон 3.4 «Панель «Комментарии» → Экспорт
// измерений CSV»; comment-40): три измерения в русской локали — разделитель «;», десятичная запятая, точность
// каждого измерения, UTF-8 с BOM и CRLF; в английской — «,» и точка; подпись с разделителем и кавычкой — в
// кавычках, текст-формула (=…) — с апострофом; измерение без /Measure — пустое значение; строки из измерений
// плана этажа (tests/fixtures/annots/measure-viewport.pdf) — страница, тип и значения Acrobat.
#include <gtest/gtest.h>

#include <QLocale>
#include <QString>
#include <QStringList>

#include "core/annot/measure.h"
#include "core/doc/document.h"
#include "core/util/features.h"
#include "services/measure_ops.h"
#include "support/test_pdf.h"

using ot::measure::CsvRow;

namespace {

const QStringList kHeader = {QStringLiteral("Страница"), QStringLiteral("Тип"), QStringLiteral("Значение"),
                             QStringLiteral("Единицы"), QStringLiteral("Подпись")};

std::vector<CsvRow> threeRows() {
    CsvRow distance;
    distance.page = 1;
    distance.kind = QStringLiteral("Расстояние");
    distance.value = 3.52778;
    distance.precision = 2;
    distance.unit = QStringLiteral("м");
    distance.caption = QStringLiteral("3,53 м");
    CsvRow perimeter;
    perimeter.page = 2;
    perimeter.kind = QStringLiteral("Периметр");
    perimeter.value = 1234.5;
    perimeter.precision = 1;
    perimeter.unit = QStringLiteral("см");
    perimeter.caption = QStringLiteral("стена; \"северная\"");
    CsvRow area;
    area.page = 3;
    area.kind = QStringLiteral("Площадь");
    area.value = 50;
    area.precision = 0;
    area.unit = QStringLiteral("м²");
    area.caption = QStringLiteral("=SUM(A1)");
    return {distance, perimeter, area};
}

QString text(const QByteArray& csv) { return QString::fromUtf8(csv.mid(3)); }

}  // namespace

TEST(measure_csv, russian_locale_uses_semicolon_and_decimal_comma) {
    OT_FEATURE("comment-40");
    const QLocale ru(QLocale::Russian, QLocale::Russia);
    EXPECT_EQ(ot::measure::csvSeparator(ru), QLatin1Char(';'));
    const QByteArray csv = ot::measure::toCsv(threeRows(), kHeader, ru);
    ASSERT_TRUE(csv.startsWith("\xEF\xBB\xBF"));
    const QStringList lines = text(csv).split(QStringLiteral("\r\n"));
    ASSERT_EQ(lines.size(), 5);  // заголовок, три строки, пустой хвост после последнего CRLF
    EXPECT_EQ(lines[0], QStringLiteral("Страница;Тип;Значение;Единицы;Подпись"));
    EXPECT_EQ(lines[1], QStringLiteral("1;Расстояние;3,53;м;3,53 м"));
    EXPECT_EQ(lines[2], QStringLiteral("2;Периметр;1234,5;см;\"стена; \"\"северная\"\"\""));
    EXPECT_EQ(lines[3], QStringLiteral("3;Площадь;50;м²;'=SUM(A1)"));
    EXPECT_TRUE(lines[4].isEmpty());
}

TEST(measure_csv, english_locale_uses_comma_and_point) {
    OT_FEATURE("comment-40");
    const QLocale en(QLocale::English, QLocale::UnitedStates);
    EXPECT_EQ(ot::measure::csvSeparator(en), QLatin1Char(','));
    const QStringList lines =
        text(ot::measure::toCsv(threeRows(), kHeader, en)).split(QStringLiteral("\r\n"));
    ASSERT_EQ(lines.size(), 5);
    EXPECT_EQ(lines[1], QStringLiteral("1,Расстояние,3.53,м,\"3,53 м\""));
    EXPECT_EQ(lines[2], QStringLiteral("2,Периметр,1234.5,см,\"стена; \"\"северная\"\"\""));
}

TEST(measure_csv, missing_value_is_empty) {
    CsvRow row;
    row.kind = QStringLiteral("Расстояние");
    row.hasValue = false;
    row.caption = QStringLiteral("12 m");
    const QStringList lines =
        text(ot::measure::toCsv({row}, kHeader, QLocale(QLocale::Russian))).split(QStringLiteral("\r\n"));
    ASSERT_GE(lines.size(), 2);
    EXPECT_EQ(lines[1], QStringLiteral("1;Расстояние;;;12 m"));
}

TEST(measure_csv, rows_from_fixture_measurements) {
    OT_FEATURE("comment-40");
    auto doc = ot::Document::openOrThrow(ot::test::sourceDir() / "tests" / "fixtures" / "annots" /
                                         "measure-viewport.pdf");
    std::vector<CsvRow> rows;
    for (const ot::measure::Measurement& m : ot::measure::listMeasurements(*doc)) {
        CsvRow row;
        row.page = m.ref.page + 1;
        row.kind =
            m.kind == ot::measure::Kind::Area ? QStringLiteral("Площадь") : QStringLiteral("Расстояние");
        row.value = m.value;
        row.hasValue = m.hasValue;
        row.precision = m.precision;
        row.unit = QString::fromStdString(m.unit);
        row.caption = QString::fromStdString(m.caption);
        rows.push_back(row);
    }
    const QStringList lines =
        text(ot::measure::toCsv(rows, kHeader, QLocale(QLocale::Russian))).split(QStringLiteral("\r\n"));
    ASSERT_EQ(lines.size(), 4);
    EXPECT_EQ(lines[1], QStringLiteral("1;Расстояние;15,52;m;15.52 m"));
    EXPECT_EQ(lines[2], QStringLiteral("1;Площадь;54,76;sq m;54.76 sq m"));
}
