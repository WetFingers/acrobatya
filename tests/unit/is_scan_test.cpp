// unit.is_scan — признак «похоже на скан» (план 3.4, п. 5; эталон 3.15; основа ocr-08): сканы фикстур
// tests/fixtures/scans (страница — одна картинка без текста) — скан; текстовые страницы и страница с текстом
// и маленькими картинками (edit/images.pdf) — нет; доля площади под картинкой и число картинок — в отчёте.
#include <gtest/gtest.h>

#include "core/doc/document.h"
#include "core/ocr/is_scan.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

namespace {

std::filesystem::path fixture(const char* rel) { return ot::test::sourceDir() / "tests" / "fixtures" / rel; }

}  // namespace

TEST(is_scan, scans_are_detected) {
    OT_FEATURE("ocr-08");
    for (const char* name : {"scans/scan-01.pdf", "scans/scan-05.pdf", "scans/scan-10.pdf"}) {
        SCOPED_TRACE(name);
        auto doc = ot::Document::openOrThrow(fixture(name));
        const ot::ocr::ScanCheck c = ot::ocr::checkScan(*doc, 0);
        EXPECT_TRUE(c.scan);
        EXPECT_FALSE(c.hasText);
        EXPECT_GE(c.images, 1);
        EXPECT_GE(c.coverage, ot::ocr::kScanCoverage);
    }
}

TEST(is_scan, text_pages_are_not_scans) {
    for (const char* name : {"edit/latin.pdf", "text/find.pdf", "demo/report.pdf"}) {
        SCOPED_TRACE(name);
        auto doc = ot::Document::openOrThrow(fixture(name));
        const ot::ocr::ScanCheck c = ot::ocr::checkScan(*doc, 0);
        EXPECT_FALSE(c.scan);
        EXPECT_TRUE(c.hasText);
    }
    auto images = ot::Document::openOrThrow(fixture("edit/images.pdf"));
    const ot::ocr::ScanCheck c = ot::ocr::checkScan(*images, 0);
    EXPECT_FALSE(c.scan);
    EXPECT_EQ(c.images, 2);
    EXPECT_LT(c.coverage, 0.2f);
    EXPECT_FALSE(ot::ocr::isScan(*images, 0));
}
