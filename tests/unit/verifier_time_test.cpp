// unit.verifier_time — «Проверять подписи на время» (план 4.6, п. 3; эталон 3.8 «Валидатор»; sign-32 —
// Установки → Подписи → Проверка): время создания подписи (метка, иначе /M — по умолчанию, как было),
// безопасное время (метка времени, иначе текущее) и текущее время — источник и момент проверки у B-T и B-B.
#include <gtest/gtest.h>

#include <filesystem>

#include "core/doc/document.h"
#include "core/sign/signer.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/features.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::TimeSource;
using ot::sign::VerificationTime;

namespace {

fs::path sign(const fs::path& dir, const std::wstring& name, bool timestamp) {
    const fs::path path = ot::test::unsignedCopy(dir, name);
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    ot::sign::SignRequest r;
    r.identity = ot::test::pkiIdentity();
    r.signingTime = ot::test::utc(2026, 3, 1);
    r.level = timestamp ? ot::sign::SignLevel::T : ot::sign::SignLevel::B;
    if (timestamp) {
        r.timestamp = tsa.client;
    }
    auto doc = Document::openOrThrow(path);
    (void)ot::sign::signDocument(*doc, r);
    return path;
}

ot::sign::SignatureInfo verify(const fs::path& path, VerificationTime time) {
    ot::sign::TrustStore trust;
    const std::string root = ot::test::readPki("root.cer");
    trust.add(ot::sign::Bytes(root.begin(), root.end()));
    ot::sign::VerifyOptions o;
    o.trust = &trust;
    o.revocation = ot::sign::RevocationPolicy::Never;
    o.time = time;
    o.now = ot::test::utc(2026, 10, 24);
    auto doc = Document::openOrThrow(path);
    const ot::sign::DocumentVerification v = ot::sign::verifyDocument(*doc, o);
    return v.signatures.at(0);
}

}  // namespace

TEST(verifier_time, timestamped_signature) {
    OT_FEATURE("sign-32");
    ot::test::TempDir dir;
    const fs::path path = sign(dir.path(), L"bt.pdf", true);
    const ot::sign::SignatureInfo created = verify(path, VerificationTime::SigningTime);
    EXPECT_EQ(created.timeSource, TimeSource::Timestamp);
    ASSERT_TRUE(created.timestamp.has_value());
    EXPECT_EQ(created.validationTime, created.timestamp->genTime);
    const ot::sign::SignatureInfo secure = verify(path, VerificationTime::SecureTime);
    EXPECT_EQ(secure.timeSource, TimeSource::Timestamp);
    EXPECT_EQ(secure.validationTime, created.validationTime);
    const ot::sign::SignatureInfo current = verify(path, VerificationTime::Current);
    EXPECT_EQ(current.timeSource, TimeSource::Now);
    EXPECT_EQ(current.validationTime, "2026-10-24T00:00:00Z");
    EXPECT_EQ(current.verdict, ot::sign::Verdict::Valid);
}

TEST(verifier_time, signature_without_timestamp) {
    ot::test::TempDir dir;
    const fs::path path = sign(dir.path(), L"bb.pdf", false);
    const ot::sign::SignatureInfo created = verify(path, VerificationTime::SigningTime);
    EXPECT_EQ(created.timeSource, TimeSource::Claimed);
    EXPECT_EQ(created.validationTime, "2026-03-01T00:00:00Z");
    // Безопасного времени нет — текущее (как Acrobat «Безопасное время (метка времени), встроенное в
    // подпись»).
    const ot::sign::SignatureInfo secure = verify(path, VerificationTime::SecureTime);
    EXPECT_EQ(secure.timeSource, TimeSource::Now);
    EXPECT_EQ(secure.validationTime, "2026-10-24T00:00:00Z");
    const ot::sign::SignatureInfo current = verify(path, VerificationTime::Current);
    EXPECT_EQ(current.timeSource, TimeSource::Now);
}
