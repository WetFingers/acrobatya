// unit.document_open.* — открытие из памяти и с диска (кириллический путь), ошибки, пустой PDF.
#include <gtest/gtest.h>

#include <filesystem>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::MuContext;

TEST(document_open, from_memory_counts_pages_and_sizes) {
    MuContext mu;
    auto doc = Document::openMemory(mu, ot::test::makePdf(mu, 3));
    ASSERT_TRUE(doc);
    EXPECT_FALSE(doc->needsPassword());
    EXPECT_EQ(doc->pageCount(), 3);
    const auto size = doc->pageSize(0);
    EXPECT_FLOAT_EQ(size.width, 595.0f);
    EXPECT_FLOAT_EQ(size.height, 842.0f);
}

TEST(document_open, from_cyrillic_path) {
    OT_FEATURE("view-01");
    MuContext mu;
    ot::test::TempDir dir;
    const auto file = dir.path() / L"проба — документ.pdf";
    ot::paths::writeFile(file, ot::test::makePdf(mu, 2));
    auto doc = Document::open(mu, file);
    ASSERT_TRUE(doc);
    EXPECT_EQ(doc->pageCount(), 2);
}

TEST(document_open, missing_file_throws_runtime_error) {
    MuContext mu;
    ot::test::TempDir dir;
    EXPECT_THROW(Document::open(mu, dir.path() / L"нет-такого.pdf"), std::runtime_error);
}

TEST(document_open, garbage_throws_mu_error) {
    MuContext mu;
    EXPECT_THROW(Document::openMemory(mu, std::string(64, 'x'), ".pdf"), ot::MuError);
}

TEST(document_open, truncated_pdf_is_repaired) {
    MuContext mu;
    std::string bytes = ot::test::makePdf(mu, 2);
    bytes.resize(bytes.size() - 60);  // без хвоста xref/trailer — MuPDF чинит
    auto doc = Document::openMemory(mu, bytes);
    EXPECT_EQ(doc->pageCount(), 2);
}

TEST(document_open, blank_document_saves_and_reopens) {
    MuContext mu;
    auto blank = Document::createBlank(mu, 4);
    EXPECT_EQ(blank->pageCount(), 4);
    const std::string bytes = blank->savePdfToMemory();
    ASSERT_GT(bytes.size(), 100u);
    EXPECT_EQ(bytes.substr(0, 5), "%PDF-");
    auto again = Document::openMemory(mu, bytes);
    EXPECT_EQ(again->pageCount(), 4);
}

TEST(document_open, mini_fixtures_open_when_present) {
    const auto dir = ot::test::sourceDir() / L"tests" / L"data" / L"mini";
    if (!std::filesystem::exists(dir / L"blank-a4.pdf")) {
        GTEST_SKIP() << "tests/data/mini ещё не сгенерированы";
    }
    MuContext mu;
    auto blank = Document::open(mu, dir / L"blank-a4.pdf");
    EXPECT_GE(blank->pageCount(), 1);
    if (std::filesystem::exists(dir / L"enc-r6.pdf")) {
        auto enc = Document::open(mu, dir / L"enc-r6.pdf");
        EXPECT_TRUE(enc->needsPassword());
        EXPECT_FALSE(enc->authenticate("wrong"));
        EXPECT_TRUE(enc->authenticate("user"));
        EXPECT_GE(enc->pageCount(), 1);
    }
}
