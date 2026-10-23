// unit.to_pdf.* — «Сохранить как PDF» для документа «только просмотр» (эталон 3.1 «Открытие», план 2.3;
// view-01): многостраничный TIFF и PNG открываются не как PDF, запись даёт PDF с тем же числом страниц и
// размерами, страница рисуется (не белая); кириллица в пути; отмена между страницами — MuError Aborted и
// файла нет; PDF на входе — тоже пишется (копия страниц).
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "core/create/to_pdf.h"
#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;

namespace {

fs::path fixture(const char* rel) { return ot::test::sourceDir() / rel; }

fs::path tempDir(const wchar_t* name) {
    const fs::path dir = fs::temp_directory_path() / L"ottisk-to-pdf" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

bool hasInk(const ot::Bitmap& bmp) {
    for (const auto px : bmp.pixels) {
        if (px < 200) {
            return true;
        }
    }
    return false;
}

}  // namespace

TEST(to_pdf, multipage_tiff_becomes_pdf_with_same_pages) {
    OT_FEATURE("view-01");
    auto src = Document::openOrThrow(fixture("tests/fixtures/images/multipage-g4.tif"));
    ASSERT_FALSE(src->isPdf());
    ASSERT_GT(src->pageCount(), 1);
    const fs::path out = tempDir(L"tiff") / L"скан страниц.pdf";
    ot::writeAsPdf(*src, out);
    ASSERT_TRUE(fs::exists(out));
    auto pdf = Document::openOrThrow(out);
    EXPECT_TRUE(pdf->isPdf());
    ASSERT_EQ(pdf->pageCount(), src->pageCount());
    for (int i = 0; i < src->pageCount(); ++i) {
        EXPECT_NEAR(pdf->pageSize(i).width, src->pageSize(i).width, 0.5f) << i;
        EXPECT_NEAR(pdf->pageSize(i).height, src->pageSize(i).height, 0.5f) << i;
    }
    EXPECT_TRUE(hasInk(pdf->renderPage(0, {})));
}

TEST(to_pdf, png_and_bytes) {
    auto src = Document::openOrThrow(fixture("tests/fixtures/images/chart.png"));
    ASSERT_FALSE(src->isPdf());
    const std::string bytes = ot::renderToPdf(*src);
    ASSERT_GT(bytes.size(), 100u);
    EXPECT_EQ(bytes.substr(0, 5), "%PDF-");
    auto pdf = Document::openOrThrowMemory(bytes);
    EXPECT_EQ(pdf->pageCount(), 1);
    EXPECT_TRUE(hasInk(pdf->renderPage(0, {})));
}

TEST(to_pdf, cancel_leaves_no_file) {
    auto src = Document::openOrThrow(fixture("tests/fixtures/images/multipage-g4.tif"));
    const fs::path out = tempDir(L"cancel") / L"out.pdf";
    int calls = 0;
    try {
        ot::writeAsPdf(*src, out, [&] { return ++calls > 1; });
        FAIL() << "no abort";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ot::ErrorKind::Aborted);
    }
    EXPECT_FALSE(fs::exists(out));
}

TEST(to_pdf, pdf_input_is_copied_page_by_page) {
    auto src = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 3));
    auto pdf = Document::openOrThrowMemory(ot::renderToPdf(*src));
    EXPECT_EQ(pdf->pageCount(), 3);
}
