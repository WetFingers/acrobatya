// unit.aatl_reject — честный отказ разбора AATL (план 4.5, п. 4; sign-31): искусственная фикстура
// tests/fixtures/trust/not-a-list.acrobatsecuritysettings (PDF с SecuritySettings.xml и тремя сертификатами
// PKI) — «не похоже на список» (NotATrustList, сертификаты найдены, но их меньше порога); не PDF — NotPdf.
// Настоящий tl12.acrobatsecuritysettings — engine.aatl (корпус trust).
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "core/sign/aatl.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;

TEST(aatl_reject, artificial_file_is_not_a_trust_list) {
    OT_FEATURE("sign-31");
    const fs::path file =
        ot::test::sourceDir() / L"tests" / L"fixtures" / L"trust" / L"not-a-list.acrobatsecuritysettings";
    const ot::sign::AatlResult r = ot::sign::parseAatlFile(file);
    EXPECT_EQ(r.status, ot::sign::AatlStatus::NotATrustList);
    EXPECT_EQ(r.certificates.size(), 3u);
    EXPECT_EQ(r.identities, 3);
    EXPECT_FALSE(r.signedList);
    EXPECT_FALSE(r.detail.empty());
    for (const ot::sign::ListedCertificate& c : r.certificates) {
        EXPECT_EQ(c.flags, ot::sign::kTrustSignatures);
        EXPECT_FALSE(c.label.empty());
    }
}

TEST(aatl_reject, not_a_pdf) {
    OT_FEATURE("sign-31");
    const ot::sign::AatlResult r = ot::sign::parseAatl("<SecuritySettings><Certificate>AAAA</Certificate>");
    EXPECT_EQ(r.status, ot::sign::AatlStatus::NotPdf);
    EXPECT_TRUE(r.certificates.empty());
}
