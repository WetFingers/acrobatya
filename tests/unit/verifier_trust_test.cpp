// unit.verifier_trust — доверие, время и отзыв (план 4.4, п. 2; эталон 3.8 «Валидатор», «Граничные случаи»;
// sign-30, sign-32, sign-33): B-B тестовой PKI с корнем в хранилище доверия — «действительна», без него —
// «подписант не доверен»; якорь только для сертификации подписи не делает доверенной; доверенный выпускающий
// ЦС — якорь (частичная цепочка); время проверки — /M «заявленное»; сертификат истёк к /M — не доверен;
// отозван до /M — недействительна, после — действительна (retroactive, как Acrobat); нет данных об отзыве:
// «обязательно» — «Неизвестно», «по возможности» — действительна с предупреждением, «никогда» — без запросов;
// встроенные данные
// (/DSS, adbe-revocationInfoArchival) — без сети, «LTV включено».
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/sign/chain.h"
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
using ot::sign::DocumentStatus;
using ot::sign::DocumentVerification;
using ot::sign::Issue;
using ot::sign::RevocationPolicy;
using ot::sign::RevocationStatus;
using ot::sign::SignatureInfo;
using ot::sign::SignRequest;
using ot::sign::TimeSource;
using ot::sign::TrustStore;
using ot::sign::Verdict;
using ot::sign::VerifyOptions;

namespace {

ot::sign::Bytes pki(const char* name) {
    const std::string s = ot::test::readPki(name);
    return ot::sign::Bytes(s.begin(), s.end());
}

TrustStore rootTrusted(unsigned flags = ot::sign::kTrustSignatures) {
    TrustStore t;
    t.add(pki("root.cer"), flags);
    return t;
}

fs::path signWith(const fs::path& dir, const std::wstring& name, ot::sign::SigningIdentity identity,
                  std::int64_t signingTime = 0) {
    const fs::path path = ot::test::unsignedCopy(dir, name);
    SignRequest r;
    r.identity = std::move(identity);
    r.signingTime = signingTime;
    auto doc = Document::openOrThrow(path);
    (void)ot::sign::signDocument(*doc, r);
    return path;
}

DocumentVerification verify(const fs::path& path, const VerifyOptions& options) {
    auto doc = Document::openOrThrow(path);
    return ot::sign::verifyDocument(*doc, options);
}

}  // namespace

TEST(verifier_trust, bb_trusted_with_root_untrusted_without) {
    OT_FEATURE("sign-30");
    OT_FEATURE("sign-33");
    ot::test::TempDir dir;
    const fs::path path = signWith(dir.path(), L"bb.pdf", ot::test::pkiIdentity());
    const TrustStore trust = rootTrusted();
    VerifyOptions options;
    options.trust = &trust;
    DocumentVerification v = verify(path, options);
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_TRUE(s.intact);
    EXPECT_TRUE(s.trusted) << s.chainError;
    ASSERT_EQ(s.chain.size(), 3u);
    EXPECT_EQ(s.chain[0].commonName, "АкроБатя тест");
    EXPECT_EQ(s.chain[2].commonName, "АкроБатя тест — корневой ЦС");
    EXPECT_EQ(s.timeSource, TimeSource::Claimed);
    EXPECT_EQ(s.validationTime, s.claimedTime);
    // Сети нет (fetcher не задан), «по возможности»: отзыв неизвестен — предупреждение, подпись
    // действительна.
    EXPECT_EQ(s.revocation, RevocationStatus::Unknown);
    EXPECT_TRUE(s.has(Issue::RevocationUnchecked));
    EXPECT_FALSE(s.ltv);
    EXPECT_EQ(s.verdict, Verdict::Valid);
    EXPECT_EQ(v.status, DocumentStatus::Valid);
    EXPECT_FALSE(v.embeddedCertificates.empty());

    // Сеть PKI: OCSP о подписанте, CRL о выпускающем ЦС — отзыв проверен, LTV нет (данные не в файле).
    const auto net = ot::test::pkiResponder();
    options.fetcher = net.get();
    v = verify(path, options);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].revocation, RevocationStatus::Good);
    EXPECT_EQ(v.signatures[0].revocationDetails.size(), 2u);
    EXPECT_FALSE(v.signatures[0].has(Issue::RevocationUnchecked));
    EXPECT_FALSE(v.signatures[0].ltv);
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Valid);
    EXPECT_GE(net->requestCount(), 2u);

    // Без якорей — подпись цела, подписант не доверен; отзыв недоверенного не проверяется.
    v = verify(path, VerifyOptions{});
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_FALSE(v.signatures[0].trusted);
    EXPECT_TRUE(v.signatures[0].has(Issue::Untrusted));
    EXPECT_EQ(v.signatures[0].verdict, Verdict::ValidUntrusted);
    EXPECT_EQ(v.status, DocumentStatus::Problems);
}

TEST(verifier_trust, anchor_purpose_and_partial_chain) {
    OT_FEATURE("sign-32");
    ot::test::TempDir dir;
    const fs::path path = signWith(dir.path(), L"bb.pdf", ot::test::pkiIdentity("signer-ec.pfx"));
    const TrustStore certOnly = rootTrusted(ot::sign::kTrustCertification);
    VerifyOptions options;
    options.trust = &certOnly;
    EXPECT_EQ(verify(path, options).signatures.at(0).verdict, Verdict::ValidUntrusted)
        << "якорь только для сертифицированных документов";

    TrustStore inter;
    inter.add(pki("inter.cer"));
    options.trust = &inter;
    const DocumentVerification v = verify(path, options);
    EXPECT_EQ(v.signatures.at(0).verdict, Verdict::Valid) << v.signatures.at(0).chainError;
    EXPECT_EQ(v.signatures.at(0).chain.size(), 2u) << "доверенный выпускающий ЦС — якорь";
}

TEST(verifier_trust, revoked_before_signing_is_invalid) {
    OT_FEATURE("sign-33");
    ot::test::TempDir dir;
    const fs::path path = signWith(dir.path(), L"отозван.pdf", ot::test::revokedIdentity());
    const TrustStore trust = rootTrusted();
    const auto net = ot::test::pkiResponder();
    VerifyOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    const DocumentVerification v = verify(path, options);
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_EQ(s.revocation, RevocationStatus::Revoked);
    EXPECT_TRUE(s.has(Issue::Revoked));
    EXPECT_EQ(s.verdict, Verdict::Invalid);
    EXPECT_EQ(v.status, DocumentStatus::Invalid);
}

TEST(verifier_trust, revoked_after_signing_is_valid) {
    ot::test::TempDir dir;
    // /M — 1 марта 2026, отзыв — 1 июня 2026: на момент подписи сертификат действовал.
    const fs::path path =
        signWith(dir.path(), L"до-отзыва.pdf", ot::test::revokedIdentity(), ot::test::utc(2026, 3, 1));
    const TrustStore trust = rootTrusted();
    const auto net = ot::test::pkiResponder();
    VerifyOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    const DocumentVerification v = verify(path, options);
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_EQ(s.validationTime, "2026-03-01T00:00:00Z");
    EXPECT_EQ(s.revocation, RevocationStatus::Good);
    ASSERT_FALSE(s.revocationDetails.empty());
    EXPECT_EQ(s.revocationDetails[0].revocationTime, "2026-06-01T00:00:00Z")
        << "отзыв позже — виден в свойствах";
    EXPECT_EQ(s.verdict, Verdict::Valid);
}

TEST(verifier_trust, revocation_policy_without_network) {
    ot::test::TempDir dir;
    const fs::path path = signWith(dir.path(), L"bb.pdf", ot::test::pkiIdentity());
    const TrustStore trust = rootTrusted();
    const auto offline = ot::test::offlineFetcher();
    VerifyOptions options;
    options.trust = &trust;
    options.fetcher = offline.get();

    options.revocation = RevocationPolicy::Required;
    DocumentVerification v = verify(path, options);
    EXPECT_TRUE(v.signatures.at(0).has(Issue::RevocationUnknown));
    EXPECT_EQ(v.signatures.at(0).verdict, Verdict::Unknown)
        << "«Неизвестно (нет данных об отзыве)», не недействительна";
    EXPECT_GT(offline->requestCount(), 0u);

    options.revocation = RevocationPolicy::BestEffort;
    v = verify(path, options);
    EXPECT_TRUE(v.signatures.at(0).has(Issue::RevocationUnchecked));
    EXPECT_EQ(v.signatures.at(0).verdict, Verdict::Valid);

    offline->clearRequests();
    options.revocation = RevocationPolicy::Never;
    v = verify(path, options);
    EXPECT_FALSE(v.signatures.at(0).revocationChecked);
    EXPECT_EQ(v.signatures.at(0).verdict, Verdict::Valid);
    EXPECT_EQ(offline->requestCount(), 0u);
}

TEST(verifier_trust, certificate_expired_at_claimed_time) {
    ot::test::TempDir dir;
    // Сертификаты PKI действуют до 2036-01-01; /M — 2037.
    const fs::path path =
        signWith(dir.path(), L"истёк.pdf", ot::test::pkiIdentity(), ot::test::utc(2037, 1, 1));
    const TrustStore trust = rootTrusted();
    VerifyOptions options;
    options.trust = &trust;
    const DocumentVerification v = verify(path, options);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].validationTime, "2037-01-01T00:00:00Z");
    EXPECT_TRUE(v.signatures[0].has(Issue::CertExpired)) << v.signatures[0].chainError;
    EXPECT_FALSE(v.signatures[0].trusted);
    EXPECT_EQ(v.signatures[0].verdict, Verdict::ValidUntrusted);
}

// Корпус EU DSS: отзыв по встроенным данным без сети (FakeHttpFetcher падает на любой запрос).
// adbe_ocsp_signed.pdf — OCSP всей цепочки в adbe-revocationInfoArchival: «LTV включено», сети нет;
// pades-lt-extended-dss.pdf — в /DSS OCSP о подписанте, о выпускающем ЦС LuxTrust данных нет: подписант — по
// файлу, ЦС — сеть (недоступна) → «по возможности» действительна с предупреждением, LTV нет.
TEST(verifier_trust, embedded_revocation_data_without_network) {
    OT_FEATURE("sign-33");
    const auto lt = ot::test::dssFile("pades-lt-extended-dss.pdf");
    const auto adbe = ot::test::dssFile("adbe_ocsp_signed.pdf");
    if (!lt || !adbe) {
        GTEST_SKIP() << "нет корпуса dss";
    }
    const auto check = [](const fs::path& file, bool fullLtv) {
        SCOPED_TRACE(file.filename().string());
        const DocumentVerification plain = verify(file, VerifyOptions{});
        const TrustStore roots = ot::test::embeddedRoots(plain);
        ASSERT_GT(roots.anchors().size(), 0u);
        const auto offline = ot::test::offlineFetcher();
        VerifyOptions options;
        options.trust = &roots;
        options.fetcher = offline.get();
        options.retroactiveRevocation = true;
        const DocumentVerification v = verify(file, options);
        ASSERT_FALSE(v.signatures.empty());
        const SignatureInfo& s = v.signatures[0];
        EXPECT_TRUE(s.intact);
        EXPECT_TRUE(s.trusted) << s.chainError;
        ASSERT_FALSE(s.revocationDetails.empty());
        EXPECT_EQ(s.revocationDetails[0].status, RevocationStatus::Good) << s.revocationDetails[0].detail;
        EXPECT_EQ(s.revocationDetails[0].source, ot::sign::RevocationSource::Ocsp);
        if (fullLtv) {
            EXPECT_EQ(s.verdict, Verdict::Valid);  // у второго файла есть секции после подписи — п. 4
        }
        if (fullLtv) {
            EXPECT_EQ(s.revocation, RevocationStatus::Good);
            EXPECT_TRUE(s.ltv) << "все данные отзыва — в файле";
            EXPECT_EQ(offline->requestCount(), 0u) << "встроенных данных хватило — сеть не нужна";
        } else {
            EXPECT_FALSE(s.ltv);
            EXPECT_TRUE(s.has(Issue::RevocationUnchecked));
            ASSERT_TRUE(s.signer.has_value());
            const auto signerOcsp = ot::sign::ocspUrls(ot::sign::loadCertificate(s.signer->der).get());
            ASSERT_FALSE(signerOcsp.empty());
            for (const ot::net::Request& r : offline->requests()) {
                EXPECT_NE(r.url, signerOcsp[0]) << "о подписанте сеть не спрашивали";
            }
        }
    };
    check(*adbe, true);
    check(*lt, false);
}
