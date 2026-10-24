// unit.warnings_scan — «Проверить документ» перед подписанием (план 4.5, п. 3; эталон 3.8 «Диалог
// подписания»; sign-17): сканер находит в tests/fixtures/sig/warnings.pdf (tools/gen/make_warnings.py)
// JavaScript (имена, /OpenAction, /AA каталога, страницы и поля, цепочка /Next ссылки, закладка), /Launch,
// /URI, /GoToR, мультимедиа
// (/RichMedia, /Sound), вложения (дерево имён и /FileAttachment), /XFA и /NeedAppearances true — по одному
// предупреждению на место, с уровнями; у чистого документа предупреждений нет.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "core/doc/document.h"
#include "core/sign/warnings_scan.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::DocumentWarning;
using ot::sign::WarningKind;
using ot::sign::WarningLevel;

namespace {

fs::path fixture(const wchar_t* name) {
    return ot::test::sourceDir() / L"tests" / L"fixtures" / L"sig" / name;
}

bool hasDetail(const ot::sign::WarningScan& scan, WarningKind kind, const std::string& detail) {
    return std::any_of(scan.warnings.begin(), scan.warnings.end(),
                       [&](const DocumentWarning& w) { return w.kind == kind && w.detail == detail; });
}

}  // namespace

TEST(warnings_scan, finds_scripts_launch_and_the_rest) {
    OT_FEATURE("sign-17");
    auto doc = Document::openOrThrow(fixture(L"warnings.pdf"));
    const ot::sign::WarningScan scan = ot::sign::scanWarnings(*doc);
    std::string all;
    for (const DocumentWarning& w : scan.warnings) {
        all += std::string(ot::sign::warningKindName(w.kind)) + " p" + std::to_string(w.page) + " " +
               w.where + " [" + w.detail + "]\n";
    }
    SCOPED_TRACE(all);
    EXPECT_EQ(scan.count(WarningKind::JavaScript), 7);
    EXPECT_EQ(scan.count(WarningKind::Launch), 1);
    EXPECT_EQ(scan.count(WarningKind::ExternalLink), 1);
    EXPECT_EQ(scan.count(WarningKind::ExternalFile), 1);
    EXPECT_EQ(scan.count(WarningKind::Multimedia), 2);
    EXPECT_EQ(scan.count(WarningKind::EmbeddedFile), 2);
    EXPECT_EQ(scan.count(WarningKind::Xfa), 1);
    EXPECT_EQ(scan.count(WarningKind::NeedAppearances), 1);
    EXPECT_EQ(scan.warnings.size(), 16u);
    EXPECT_TRUE(hasDetail(scan, WarningKind::Launch, "calc.exe"));
    EXPECT_TRUE(hasDetail(scan, WarningKind::ExternalLink, "https://example.com/"));
    EXPECT_TRUE(hasDetail(scan, WarningKind::ExternalFile, "other.pdf"));
    EXPECT_TRUE(hasDetail(scan, WarningKind::EmbeddedFile, "data.txt"));
    EXPECT_TRUE(hasDetail(scan, WarningKind::EmbeddedFile, "note.txt"));
    EXPECT_EQ(scan.worst(), WarningLevel::Danger);
    EXPECT_FALSE(scan.clean());
    // Уровни: сценарии и запуск — опасно, ссылка на сайт — сведения.
    EXPECT_EQ(ot::sign::warningLevel(WarningKind::JavaScript), WarningLevel::Danger);
    EXPECT_EQ(ot::sign::warningLevel(WarningKind::Launch), WarningLevel::Danger);
    EXPECT_EQ(ot::sign::warningLevel(WarningKind::ExternalLink), WarningLevel::Info);
    EXPECT_EQ(ot::sign::warningLevel(WarningKind::EmbeddedFile), WarningLevel::Warning);
    // Страницы: запуск — первая (0), мультимедиа — вторая (1), сценарий имени — документ (−1).
    for (const DocumentWarning& w : scan.warnings) {
        if (w.kind == WarningKind::Launch) {
            EXPECT_EQ(w.page, 0);
        }
        if (w.kind == WarningKind::Multimedia) {
            EXPECT_EQ(w.page, 1);
        }
        EXPECT_EQ(w.level, ot::sign::warningLevel(w.kind));
    }
}

TEST(warnings_scan, clean_document_has_no_warnings) {
    OT_FEATURE("sign-17");
    auto doc = Document::openOrThrow(fixture(L"unsigned.pdf"));
    const ot::sign::WarningScan scan = ot::sign::scanWarnings(*doc);
    EXPECT_TRUE(scan.clean());
    EXPECT_EQ(scan.worst(), WarningLevel::Info);
}
