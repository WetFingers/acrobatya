// unit.print_job.* — задание печати (план 2.6; эталон 3.3 «Механика»; view-37, view-43): PDF-вывод
// (QPdfWriter — тот же движок, что QPrinter::PdfFormat) открывается MuPDF с ожидаемым числом страниц и
// размерами листов для «один на листе», n-up, буклета и постера, совпадает с растром листа (imgcmp
// crossCompare); копии с подбором и без; «Серый» — все пиксели R = G = B; dpi растра — потолок «качества»,
// «как изображение», без права высокого качества — 150; без права печати — отказ; отмена; подмена принтеров
// (ScopedPrintDevices, OTTISK_FAKE_PRINTER). compat.print.pdf (print_compat.DISABLED_gdi_to_file) — GDI в
// файл через «Microsoft Print to PDF», нет принтера — пропуск.
#include <gtest/gtest.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPrinterInfo>
#include <QString>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/print_job.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

using ot::DocumentSession;
using ot::FakePrintDevice;
using ot::PrintJob;
using ot::PrintSettings;
using ot::PrintStatus;
namespace print = ot::print;

namespace {

QString fixture(const char* rel) {
    return QString::fromStdWString((ot::test::sourceDir() / "tests" / "fixtures" / rel).wstring());
}

QString qpath(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }

std::unique_ptr<DocumentSession> openSession(const QString& path) {
    ot::test::qtApp();
    auto s = std::make_unique<DocumentSession>();
    EXPECT_TRUE(s->open(path)) << s->errorString().toStdString();
    return s;
}

// Страницы PDF: размеры в пунктах.
std::vector<std::pair<float, float>> pdfPages(const std::filesystem::path& file) {
    std::vector<std::pair<float, float>> out;
    auto doc = ot::Document::openOrThrow(file);
    for (int i = 0; i < doc->pageCount(); ++i) {
        const ot::RectF b = doc->pageBox(i);
        out.emplace_back(b.width(), b.height());
    }
    return out;
}

// Цвет квадрата 60 × 60 pt в левом верхнем углу страницы mixed-sizes.pdf — по нему видно, какая страница.
QColor squareColor(const QImage& sheet, const QRectF& pageOnSheetPx, qreal scalePx) {
    return sheet.pixelColor((pageOnSheetPx.topLeft() + QPointF(50, 50) * scalePx).toPoint());
}

ot::Bitmap toBitmap(const QImage& image) {
    const QImage rgb = image.convertToFormat(QImage::Format_RGB888);
    ot::Bitmap b;
    b.width = rgb.width();
    b.height = rgb.height();
    b.components = 3;
    b.stride = b.width * 3;
    b.pixels.resize(static_cast<size_t>(b.stride) * b.height);
    for (int y = 0; y < b.height; ++y) {
        std::memcpy(&b.pixels[static_cast<size_t>(y) * b.stride], rgb.constScanLine(y),
                    static_cast<size_t>(b.stride));
    }
    return b;
}

}  // namespace

TEST(print_job, plan_permissions_ranges_duplex) {
    OT_FEATURE("view-37");
    ot::OpenSummary doc;
    doc.ok = true;
    doc.pageCount = 10;
    for (int i = 0; i < 10; ++i) {
        doc.pageSizes << QSizeF(595.2756, 841.8898);
    }
    PrintSettings s;
    const print::Paper a4 = print::Paper::a4();
    auto plan = ot::makePrintPlan(doc, s, a4);
    EXPECT_EQ(plan.status, PrintStatus::Done);
    EXPECT_EQ(plan.sheets.size(), 10u);
    s.pages.range = print::PageRange::Custom;
    s.pages.custom = "12";
    EXPECT_EQ(ot::makePrintPlan(doc, s, a4).status, PrintStatus::RangeError);
    s.pages.custom = "1";
    s.pages.parity = print::PageParity::Even;
    EXPECT_EQ(ot::makePrintPlan(doc, s, a4).status, PrintStatus::NothingToPrint);
    s = {};
    s.layout.layout = print::Layout::Booklet;
    plan = ot::makePrintPlan(doc, s, a4);
    EXPECT_EQ(plan.sheets.size(), 6u);
    EXPECT_EQ(plan.duplex, ot::DuplexMode::ShortSide);  // буклет «обе стороны» — по короткой стороне
    s.layout.bookletSubset = print::BookletSubset::FrontOnly;
    EXPECT_EQ(ot::makePrintPlan(doc, s, a4).duplex, ot::DuplexMode::None);
    doc.permissions.print = false;
    EXPECT_EQ(ot::makePrintPlan(doc, s, a4).status, PrintStatus::Denied);
}

TEST(print_job, dpi_quality_image_and_rights) {
    OT_FEATURE("view-43");
    PrintSettings s;
    EXPECT_EQ(ot::printDpi(s, 1200, true), 300);  // «Microsoft Print to PDF» 1200 dpi → 300
    EXPECT_EQ(ot::printDpi(s, 200, true), 200);
    EXPECT_EQ(ot::printDpi(s, 1200, false), 150);
    s.asImage = true;
    s.imageDpi = 600;
    EXPECT_EQ(ot::printDpi(s, 1200, true), 600);
    EXPECT_EQ(ot::printDpi(s, 300, true), 300);
    s.imageDpi = 100;
    EXPECT_EQ(ot::printDpi(s, 1200, true), 100);
    EXPECT_EQ(ot::printScaleFromName(QStringLiteral("shrink")), print::SizeMode::ShrinkOversized);
    EXPECT_EQ(ot::printScaleFromName(QStringLiteral("x")), print::SizeMode::Fit);
}

TEST(print_job, pdf_output_layouts_open_in_mupdf) {
    OT_FEATURE("view-37");
    OT_FEATURE("view-39");
    OT_FEATURE("view-40");
    OT_FEATURE("view-41");
    auto session = openSession(fixture("pages/mixed-sizes.pdf"));
    ot::test::TempDir tmp;
    const auto printTo = [&](const char* name, const PrintSettings& s) {
        const auto file = tmp.path() / name;
        auto device = ot::makePdfDevice(qpath(file), {595.2756f, 841.8898f}, 72);
        PrintJob job(*session, s);
        EXPECT_EQ(job.run(*device), PrintStatus::Done) << name << ": " << job.errorString().toStdString();
        return pdfPages(file);
    };
    // Один на листе: 4 листа, альбомная страница — альбомный лист.
    PrintSettings single;
    single.title = QStringLiteral("mixed-sizes.pdf");
    auto pages = printTo("single.pdf", single);
    ASSERT_EQ(pages.size(), 4u);
    EXPECT_NEAR(pages[0].first, 595.28f, 0.5f);
    EXPECT_NEAR(pages[2].first, 841.89f, 0.5f);   // A4 альбомная
    EXPECT_NEAR(pages[3].second, 841.89f, 0.5f);  // A1 вписан в A4
    // n-up 4 — один лист.
    PrintSettings nup;
    nup.layout.layout = print::Layout::Multiple;
    pages = printTo("nup.pdf", nup);
    EXPECT_EQ(pages.size(), 1u);
    // Буклет 4 страниц — лицо и оборот, альбомные.
    PrintSettings booklet;
    booklet.layout.layout = print::Layout::Booklet;
    pages = printTo("booklet.pdf", booklet);
    ASSERT_EQ(pages.size(), 2u);
    EXPECT_GT(pages[0].first, pages[0].second);
    // Постер плаката A1 (стр. 4) на A4 при 100 % с перекрытием 10 мм — 9 листов.
    PrintSettings poster;
    poster.layout.layout = print::Layout::Poster;
    poster.layout.overlap = 10 * print::kPointsPerMm;
    poster.layout.cutMarks = true;
    poster.pages.range = print::PageRange::Custom;
    poster.pages.custom = "4";
    pages = printTo("poster.pdf", poster);
    EXPECT_EQ(pages.size(), 9u);
}

TEST(print_job, pdf_matches_sheet_raster) {
    OT_FEATURE("view-39");
    auto session = openSession(fixture("pages/mixed-sizes.pdf"));
    ot::test::TempDir tmp;
    PrintSettings s;
    s.layout.layout = print::Layout::Multiple;
    s.layout.borders = true;
    // Растр листа устройства в памяти и тот же лист через PDF → MuPDF.
    FakePrintDevice fake(QStringLiteral("Fake"), 72, print::Paper::a4());
    PrintJob a(*session, s);
    ASSERT_EQ(a.run(fake), PrintStatus::Done);
    ASSERT_EQ(fake.sheets.size(), 1);
    EXPECT_EQ(fake.sheets[0].frames, 4);
    const auto file = tmp.path() / "nup.pdf";
    auto device = ot::makePdfDevice(qpath(file), {595.2756f, 841.8898f}, 72);
    PrintJob b(*session, s);
    ASSERT_EQ(b.run(*device), PrintStatus::Done);
    auto out = ot::Document::openOrThrow(file);
    const ot::Bitmap fromPdf = out->renderPage(0, {.dpi = 72});
    const ot::Bitmap fromDevice = toBitmap(fake.sheets[0].image);
    const ot::CrossResult cmp = ot::crossCompare(fromPdf, fromDevice);
    EXPECT_TRUE(cmp.ok) << cmp.toJson();
}

TEST(print_job, copies_collate_and_order) {
    OT_FEATURE("view-37");
    auto session = openSession(fixture("pages/mixed-sizes.pdf"));
    const print::Paper paper = print::Paper::a4();
    const qreal k = 36 / 72.0;
    // Страницы 1–3 «один на листе», 2 копии без подбора: 1, 1, 2, 2, 3, 3.
    PrintSettings s;
    s.pages.range = print::PageRange::Custom;
    s.pages.custom = "1-3";
    s.copies = 2;
    s.collate = false;
    s.layout.orientation = print::Orientation::Portrait;
    s.layout.size = print::SizeMode::Actual;
    FakePrintDevice fake(QStringLiteral("Fake"), 36, paper);
    PrintJob job(*session, s);
    ASSERT_EQ(job.run(fake), PrintStatus::Done);
    ASSERT_EQ(fake.sheets.size(), 6);
    EXPECT_EQ(job.sheetsPrinted(), 6);
    // Квадрат стр. 1 — красный, стр. 2 (A5) — синий.
    const QRectF a4OnSheet(0, 0, 595.2756 * k, 841.8898 * k);
    const QRectF a5OnSheet((595.2756 - 419.5276) / 2 * k, (841.8898 - 595.2756) / 2 * k, 419.5276 * k,
                           595.2756 * k);
    EXPECT_GT(squareColor(fake.sheets[1].image, a4OnSheet, k).red(), 150);
    EXPECT_GT(squareColor(fake.sheets[2].image, a5OnSheet, k).blue(), 150);
    EXPECT_GT(squareColor(fake.sheets[3].image, a5OnSheet, k).blue(), 150);
    // С подбором: 1, 2, 3, 1, 2, 3.
    s.collate = true;
    FakePrintDevice collated(QStringLiteral("Fake"), 36, paper);
    PrintJob job2(*session, s);
    ASSERT_EQ(job2.run(collated), PrintStatus::Done);
    ASSERT_EQ(collated.sheets.size(), 6);
    EXPECT_GT(squareColor(collated.sheets[3].image, a4OnSheet, k).red(), 150);
    EXPECT_GT(squareColor(collated.sheets[1].image, a5OnSheet, k).blue(), 150);
    // Дуплекс и нечётное число листов — пустой оборот между копиями.
    s.duplex = ot::DuplexMode::LongSide;
    FakePrintDevice duplex(QStringLiteral("Fake"), 36, paper);
    PrintJob job3(*session, s);
    ASSERT_EQ(job3.run(duplex), PrintStatus::Done);
    ASSERT_EQ(duplex.sheets.size(), 7);
    EXPECT_EQ(duplex.sheets[3].images, 0);
    EXPECT_EQ(duplex.options.duplex, ot::DuplexMode::LongSide);
}

TEST(print_job, grayscale_all_pixels_gray) {
    OT_FEATURE("view-37");
    auto session = openSession(fixture("pages/mixed-sizes.pdf"));
    PrintSettings s;
    s.grayscale = true;
    s.pages.range = print::PageRange::Current;
    s.pages.current = 0;
    FakePrintDevice fake(QStringLiteral("Fake"), 36, print::Paper::a4());
    PrintJob job(*session, s);
    ASSERT_EQ(job.run(fake), PrintStatus::Done);
    const QImage& img = fake.sheets[0].image;
    int colored = 0;
    int dark = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            const QRgb c = img.pixel(x, y);
            colored += (qRed(c) != qGreen(c) || qGreen(c) != qBlue(c)) ? 1 : 0;
            dark += qRed(c) < 128 ? 1 : 0;
        }
    }
    EXPECT_EQ(colored, 0);
    EXPECT_GT(dark, 50);
}

TEST(print_job, rights_denied_and_low_resolution) {
    OT_FEATURE("view-37");
    ot::test::TempDir tmp;
    // Без права печати — отказ, на устройство ничего.
    {
        auto doc = ot::Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 2));
        ot::SaveOptions o;
        o.mode = ot::SaveMode::Full;
        o.encryption = ot::SaveEncryption{ot::SaveEncryption::Method::Aes256, "", "owner", -4 & ~(1 << 2)};
        ASSERT_TRUE(ot::Saver::save(*doc, tmp.path() / "noprint.pdf", o).ok());
    }
    auto denied = openSession(qpath(tmp.path() / "noprint.pdf"));
    EXPECT_FALSE(denied->summary().permissions.print);
    FakePrintDevice fake;
    PrintJob job(*denied, {});
    EXPECT_EQ(job.run(fake), PrintStatus::Denied);
    EXPECT_FALSE(fake.begun);
    // Печать без высокого качества (enc-rc4-128, пароль user) — растр не выше 150 dpi на устройстве 300 dpi.
    ot::test::qtApp();
    DocumentSession low;
    ASSERT_TRUE(low.open(fixture("enc/enc-rc4-128.pdf"), QStringLiteral("user")));
    ASSERT_TRUE(low.summary().permissions.print);
    ASSERT_FALSE(low.summary().permissions.printHighRes);
    PrintSettings s;
    s.pages.range = print::PageRange::Current;
    FakePrintDevice hi(QStringLiteral("Fake"), 300, print::Paper::a4());
    PrintJob job2(low, s);
    ASSERT_EQ(job2.run(hi), PrintStatus::Done);
    int width = 0;
    for (const QSize& sz : hi.sheets[0].imageSizes) {
        width = std::max(width, sz.width());
    }
    EXPECT_NEAR(width, 1240, 2);  // A4 при 150 dpi
}

TEST(print_job, cancel_and_failed_device) {
    auto session = openSession(fixture("pages/mixed-sizes.pdf"));
    FakePrintDevice fake;
    PrintJob job(*session, {});
    int calls = 0;
    EXPECT_EQ(job.run(fake,
                      [&](int done, int total) {
                          ++calls;
                          EXPECT_EQ(total, 4);
                          if (done == 1) {
                              job.cancel();
                          }
                      }),
              PrintStatus::Cancelled);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(fake.aborted);
    EXPECT_FALSE(fake.ended);
    EXPECT_TRUE(job.cancelled());
}

TEST(print_job, device_override_and_fake_dir) {
    ot::test::qtApp();
    int opened = 0;
    {
        ot::ScopedPrintDevices scope({QStringLiteral("Office"), QStringLiteral("Home")},
                                     [&](const QString& printer, const QString&) {
                                         ++opened;
                                         return std::make_unique<FakePrintDevice>(printer);
                                     });
        EXPECT_TRUE(ot::PrintDevices::overridden());
        EXPECT_EQ(ot::PrintDevices::printers(),
                  (QStringList{QStringLiteral("Office"), QStringLiteral("Home")}));
        EXPECT_EQ(ot::PrintDevices::defaultPrinter(), QStringLiteral("Office"));
        EXPECT_FALSE(ot::PrintDevices::supportsDuplex(QStringLiteral("Office")));
        auto dev = ot::PrintDevices::open({});
        ASSERT_NE(dev, nullptr);
        EXPECT_EQ(dev->name(), QStringLiteral("Office"));
    }
    EXPECT_EQ(opened, 2);  // supportsDuplex спрашивает устройство подмены, open — второе
    // OTTISK_FAKE_PRINTER — печать без окна в PDF каталога и строка jobs.jsonl.
    ot::test::TempDir tmp;
    // Путь с кириллицей: qputenv пишет узкой кодовой страницей — только широкий _wputenv_s.
    _wputenv_s(L"OTTISK_FAKE_PRINTER", (tmp.path() / "printed").wstring().c_str());
    QString error;
    const PrintStatus status =
        ot::printFileSilently(fixture("pages/mixed-sizes.pdf"), QStringLiteral("Office 2"), &error);
    _wputenv_s(L"OTTISK_FAKE_PRINTER", L"");
    EXPECT_EQ(status, PrintStatus::Done) << error.toStdString();
    const auto printed = tmp.path() / "printed" / "Office_2-1.pdf";
    ASSERT_TRUE(std::filesystem::exists(printed));
    EXPECT_EQ(pdfPages(printed).size(), 4u);
    QFile log(qpath(tmp.path() / "printed" / "jobs.jsonl"));
    ASSERT_TRUE(log.open(QIODevice::ReadOnly));
    EXPECT_TRUE(log.readAll().contains("\"printer\":\"Office 2\""));
    // Нет файла — ошибка, код не Done.
    _wputenv_s(L"OTTISK_FAKE_PRINTER", (tmp.path() / "printed").wstring().c_str());
    EXPECT_EQ(ot::printFileSilently(QStringLiteral("C:/nope/none.pdf"), {}, &error), PrintStatus::Failed);
    _wputenv_s(L"OTTISK_FAKE_PRINTER", L"");
    EXPECT_FALSE(error.isEmpty());
}

// compat.print.pdf: «Microsoft Print to PDF» через GDI в файл без окна (OTTISK_COMPAT_OUT/print/gdi.pdf); нет
// принтера — пропуск (ctest SKIP_REGULAR_EXPRESSION).
TEST(print_compat, DISABLED_gdi_to_file) {
    OT_FEATURE("view-37");
    const char* out = std::getenv("OTTISK_COMPAT_OUT");
    ASSERT_NE(out, nullptr);
    ot::test::qtApp();
    const QString printer = QStringLiteral("Microsoft Print to PDF");
    if (!QPrinterInfo::availablePrinterNames().contains(printer)) {
        GTEST_SKIP() << "no \"Microsoft Print to PDF\"";
    }
    const std::filesystem::path dir = ot::paths::fromUtf8(out) / "print";
    std::filesystem::create_directories(dir);
    const auto file = dir / "gdi.pdf";
    std::filesystem::remove(file);
    auto device = ot::makeGdiDevice(printer, qpath(file));
    ASSERT_NE(device, nullptr);
    EXPECT_GE(device->resolution(), 300);
    auto session = openSession(fixture("pages/mixed-sizes.pdf"));
    PrintSettings s;
    s.title = QStringLiteral("mixed-sizes.pdf");
    s.layout.layout = print::Layout::Multiple;
    s.layout.cols = 2;
    s.layout.rows = 1;
    s.layout.borders = true;
    PrintJob job(*session, s);
    ASSERT_EQ(job.run(*device), PrintStatus::Done) << job.errorString().toStdString();
    ASSERT_TRUE(std::filesystem::exists(file));
    EXPECT_EQ(pdfPages(file).size(), 2u);
}
