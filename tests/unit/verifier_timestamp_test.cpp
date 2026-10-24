// unit.verifier_timestamp — метки времени и «secure time» (план 4.4, п. 3; эталон 3.8 «Валидатор», «LTV и
// B-LTA», «Граничные случаи»; sign-19 — проверка, sign-32 — время): метка подписи B-T проверена (imprint —
// хэш значения подписи, цепочка службы времени на момент метки) — время проверки — время метки; служба не
// доверена — время «заявленное» и предупреждение; испорченная метка — «Неизвестно», время — /M; сертификат
// истёк к /M, но метка раньше — действительна; служба времени истекла к «сейчас» — «просроченная метка» (по
// опции — принять); метка документа — доверие службы, время — её; уровни PAdES B-B/T/LT/LTA по структуре;
// файлы EU DSS.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/sign/cms.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/signer.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/features.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::DocumentVerification;
using ot::sign::Issue;
using ot::sign::PadesLevel;
using ot::sign::SignatureInfo;
using ot::sign::SignatureKind;
using ot::sign::SignRequest;
using ot::sign::TimeSource;
using ot::sign::TrustStore;
using ot::sign::Verdict;
using ot::sign::VerifyOptions;

namespace {

TrustStore rootTrusted() {
    TrustStore t;
    const std::string root = ot::test::readPki("root.cer");
    t.add(Bytes(root.begin(), root.end()));
    return t;
}

// B-T тестовой PKI (служба времени в процессе).
fs::path signBt(const fs::path& dir, const std::wstring& name, std::int64_t signingTime = 0) {
    const fs::path path = ot::test::unsignedCopy(dir, name);
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    SignRequest r;
    r.identity = ot::test::pkiIdentity();
    r.timestamp = tsa.client;
    r.signingTime = signingTime;
    auto doc = Document::openOrThrow(path);
    (void)ot::sign::signDocument(*doc, r);
    return path;
}

DocumentVerification verify(const fs::path& path, const VerifyOptions& options) {
    auto doc = Document::openOrThrow(path);
    return ot::sign::verifyDocument(*doc, options);
}

VerifyOptions trusted(const TrustStore& trust) {
    VerifyOptions o;
    o.trust = &trust;
    o.revocation = ot::sign::RevocationPolicy::Never;
    return o;
}

}  // namespace

TEST(verifier_timestamp, bt_timestamp_gives_secure_time) {
    OT_FEATURE("sign-19");
    OT_FEATURE("sign-32");
    ot::test::TempDir dir;
    const fs::path path = signBt(dir.path(), L"bt.pdf");
    const TrustStore trust = rootTrusted();
    const DocumentVerification v = verify(path, trusted(trust));
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    ASSERT_TRUE(s.timestamp.has_value());
    EXPECT_TRUE(s.timestampIntact);
    EXPECT_TRUE(s.timestampTrusted);
    EXPECT_EQ(s.timestamp->policy, "1.2.3.4.1");
    EXPECT_EQ(s.timeSource, TimeSource::Timestamp);
    EXPECT_EQ(s.validationTime, s.timestamp->genTime);
    EXPECT_EQ(s.level, PadesLevel::T);
    EXPECT_EQ(s.verdict, Verdict::Valid);
    EXPECT_FALSE(s.has(Issue::TimestampInvalid));
    EXPECT_FALSE(s.has(Issue::TimestampUntrusted));
    EXPECT_STREQ(ot::sign::padesLevelName(s.level), "T");

    // Без якорей метка цела, но служба не доверена: время — заявленное.
    const DocumentVerification u = verify(path, VerifyOptions{});
    ASSERT_EQ(u.signatures.size(), 1u);
    EXPECT_TRUE(u.signatures[0].timestampIntact);
    EXPECT_FALSE(u.signatures[0].timestampTrusted);
    EXPECT_TRUE(u.signatures[0].has(Issue::TimestampUntrusted));
    EXPECT_EQ(u.signatures[0].timeSource, TimeSource::Claimed);
}

TEST(verifier_timestamp, damaged_timestamp_is_unknown_and_time_is_claimed) {
    ot::test::TempDir dir;
    const fs::path path = signBt(dir.path(), L"метка-испорчена.pdf");
    const Bytes der = ot::test::readSignatures(path).sigs.at(0).contents;
    const Bytes imprint = ot::sign::ossl::sha256(ot::sign::cmsSignatureValue(der));
    // imprint в TSTInfo токена (вне подписи CMS — подпись документа остаётся целой).
    ot::test::patchContents(path, 0, [&](Bytes& d) {
        const auto it = std::search(d.begin(), d.end(), imprint.begin(), imprint.end());
        ASSERT_NE(it, d.end());
        *it ^= 0x01;
    });
    const TrustStore trust = rootTrusted();
    const DocumentVerification v = verify(path, trusted(trust));
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_TRUE(s.intact) << "метка — unsigned-атрибут, подпись документа цела";
    EXPECT_FALSE(s.timestampIntact);
    EXPECT_TRUE(s.has(Issue::TimestampInvalid));
    EXPECT_EQ(s.timeSource, TimeSource::Claimed);
    EXPECT_EQ(s.verdict, Verdict::Unknown);
}

TEST(verifier_timestamp, expired_signer_with_earlier_timestamp_is_valid) {
    ot::test::TempDir dir;
    // /M — 2037 (сертификаты PKI до 2036-01-01), метка — сейчас: проверка на время метки.
    const fs::path path = signBt(dir.path(), L"истёк-с-меткой.pdf", ot::test::utc(2037, 1, 1));
    const TrustStore trust = rootTrusted();
    const DocumentVerification v = verify(path, trusted(trust));
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_EQ(s.timeSource, TimeSource::Timestamp);
    EXPECT_FALSE(s.has(Issue::CertExpired)) << s.chainError;
    EXPECT_TRUE(s.trusted);
    EXPECT_TRUE(s.has(Issue::ClaimedTimeAfterTimestamp));
    EXPECT_EQ(s.verdict, Verdict::Valid);
}

TEST(verifier_timestamp, expired_timestamp_authority_option) {
    ot::test::TempDir dir;
    const fs::path path = signBt(dir.path(), L"просрочена.pdf");
    const TrustStore trust = rootTrusted();
    VerifyOptions options = trusted(trust);
    options.now = ot::test::utc(2037, 6, 1);  // служба времени PKI действует до 2036-01-01
    options.acceptExpiredTimestamps = true;
    DocumentVerification v = verify(path, options);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_TRUE(v.signatures[0].has(Issue::TimestampExpired));
    EXPECT_EQ(v.signatures[0].timeSource, TimeSource::Timestamp);
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Valid);

    options.acceptExpiredTimestamps = false;
    v = verify(path, options);
    EXPECT_TRUE(v.signatures[0].has(Issue::TimestampExpired));
    EXPECT_EQ(v.signatures[0].timeSource, TimeSource::Claimed) << "просроченная метка не принимается";
}

TEST(verifier_timestamp, document_timestamp_trust_and_levels) {
    OT_FEATURE("sign-19");
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"bb-dts.pdf");
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    {
        auto doc = Document::openOrThrow(path);
        SignRequest r;
        r.identity = ot::test::pkiIdentity();
        (void)ot::sign::signDocument(*doc, r);
    }
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
    }
    const TrustStore trust = rootTrusted();
    const DocumentVerification v = verify(path, trusted(trust));
    ASSERT_EQ(v.signatures.size(), 2u);
    const SignatureInfo& sig = v.signatures[0];
    const SignatureInfo& dts = v.signatures[1];
    EXPECT_EQ(dts.kind, SignatureKind::DocTimestamp);
    EXPECT_TRUE(dts.intact);
    EXPECT_TRUE(dts.trusted) << dts.chainError;
    ASSERT_TRUE(dts.timestamp.has_value());
    EXPECT_EQ(dts.timeSource, TimeSource::Timestamp);
    EXPECT_EQ(dts.validationTime, dts.timestamp->genTime);
    EXPECT_EQ(dts.verdict, Verdict::Valid);
    EXPECT_EQ(dts.level, PadesLevel::None);
    EXPECT_EQ(sig.level, PadesLevel::T) << "метка документа после подписи — B-T без /DSS";
    EXPECT_FALSE(sig.timestamp.has_value());

    const DocumentVerification u = verify(path, VerifyOptions{});
    EXPECT_EQ(u.signatures.at(1).verdict, Verdict::ValidUntrusted);
}

TEST(verifier_timestamp, dss_levels_and_secure_time) {
    const json& table = ot::test::verifierExpectations()["levels"];
    int checked = 0;
    for (const auto& [name, want] : table.items()) {
        const auto file = ot::test::dssFile(name);
        if (!file) {
            continue;
        }
        ++checked;
        SCOPED_TRACE(name);
        const DocumentVerification plain = verify(*file, VerifyOptions{});
        const TrustStore roots = ot::test::embeddedRoots(plain);
        VerifyOptions options;
        options.trust = &roots;
        options.revocation = ot::sign::RevocationPolicy::Never;
        const DocumentVerification v = verify(*file, options);
        std::size_t i = 0;
        for (const json& w : want) {
            const SignatureInfo* s = nullptr;
            if (w.contains("field")) {
                s = ot::test::findSignature(v, w["field"].get<std::string>());
            } else if (w.contains("kind")) {
                for (const SignatureInfo& x : v.signatures) {
                    if (ot::sign::signatureKindName(x.kind) == w["kind"].get<std::string>()) {
                        s = &x;
                        break;
                    }
                }
            } else if (i < v.signatures.size()) {
                s = &v.signatures[i];
            }
            ++i;
            ASSERT_NE(s, nullptr) << w.dump();
            ot::test::expectSignature(*s, w, name);
        }
    }
    if (checked == 0) {
        GTEST_SKIP() << "нет корпуса dss";
    }
}
