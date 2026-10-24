// engine.aatl — настоящий Adobe Approved Trust List (план 4.5, п. 4; эталон 3.8 «Доверие»; sign-31): корпус
// trust (этап 0) — tl12.acrobatsecuritysettings: разбор «по факту» даёт не меньше 100 сертификатов, среди них
// Adobe Root CA G2 (SHA-256 зафиксирован), его флаги — подписи и сертифицированные документы; подпись списка
// цела. Нет корпуса — пропуск (строка в «Нужна ручная проверка»).
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "core/sign/aatl.h"
#include "core/sign/ossl.h"
#include "core/util/features.h"
#include "support/probe.h"

namespace fs = std::filesystem;

namespace {

// Adobe Root CA G2 (CN=Adobe Root CA G2, OU=Adobe Trust Services, O=Adobe Systems Incorporated, C=US).
constexpr const char* kAdobeRootG2 = "458E0E219698B18D2D4093D6336A12547953A54C05DD967C3A5A268B772D1B22";

}  // namespace

TEST(aatl, real_trust_list_from_corpus) {
    OT_FEATURE("sign-31");
    const fs::path file = ot::test::corpusRoot() / L"trust" / L"tl12.acrobatsecuritysettings";
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        GTEST_SKIP() << "corpus tag trust is missing: " << file.string();
    }
    const ot::sign::AatlResult r = ot::sign::parseAatlFile(file);
    ASSERT_EQ(r.status, ot::sign::AatlStatus::Ok) << r.detail;
    EXPECT_GE(static_cast<int>(r.certificates.size()), ot::sign::kAatlMinCertificates);
    EXPECT_TRUE(r.signedList);
    EXPECT_TRUE(r.signatureIntact);
    EXPECT_FALSE(r.signer.empty());
    const auto root = std::find_if(r.certificates.begin(), r.certificates.end(), [](const auto& c) {
        return ot::sign::ossl::toHex(ot::sign::ossl::sha256(c.der)) == kAdobeRootG2;
    });
    ASSERT_NE(root, r.certificates.end());
    EXPECT_EQ(root->label, "Adobe Root CA G2");
    EXPECT_NE(root->flags & ot::sign::kTrustSignatures, 0u);
    EXPECT_NE(root->flags & ot::sign::kTrustCertification, 0u);
    std::printf("AATL: %zu certificates, %d identities, %d skipped, signed by %s\n", r.certificates.size(),
                r.identities, r.skipped, r.signer.c_str());
}
