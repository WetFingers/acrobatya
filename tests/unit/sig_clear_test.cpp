// unit.sig_clear — «Очистить подпись» и сведения о сертификате для просмотра (план 4.6, п. 2; эталон 3.8
// «Панель «Подписи»»; sign-22 — очистить подпись, sign-27 — сертификат подписанта): очищенное поле снова
// пустое, документ без подписей после инкрементальной записи, у сертифицирующей уходит и /Perms /DocMDP;
// нельзя — заблокированное поле (только чтение) и подпись, после которой есть другие; сведения сертификата
// подписанта тестовой PKI — назначение ключа, расширенное назначение, адреса OCSP и CRL, отпечатки.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/sign/docmdp.h"
#include "core/sign/sig_fields.h"
#include "core/sign/signer.h"
#include "core/sign/verifier.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::ClearBlock;
using ot::sign::SignRequest;

namespace {

void sign(const fs::path& path, bool lock = false, std::optional<ot::sign::MdpPermission> certify = {},
          ot::RectF rect = {72, 600, 272, 660}) {
    auto doc = Document::openOrThrow(path);
    SignRequest r;
    r.identity = ot::test::pkiIdentity();
    r.level = ot::sign::SignLevel::B;
    r.lock = lock;
    r.certify = certify;
    r.placement.page = 0;
    r.placement.rect = rect;
    (void)ot::sign::signDocument(*doc, r);
}

}  // namespace

TEST(sig_clear, clears_last_signature) {
    OT_FEATURE("sign-22");
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"очистить.pdf");
    sign(path);
    {
        auto doc = Document::openOrThrow(path);
        EXPECT_EQ(ot::sign::clearBlock(*doc, "Signature1"), ClearBlock::None);
        EXPECT_EQ(ot::sign::clearBlock(*doc, "НетТакого"), ClearBlock::NotFound);
        ot::sign::clearSignature(*doc, "Signature1");
        const ot::SaveReport report = ot::Saver::save(*doc);
        ASSERT_TRUE(report.ok()) << report.message;
        EXPECT_EQ(report.mode, ot::SaveMode::Incremental);
    }
    auto doc = Document::openOrThrow(path);
    const std::vector<ot::sign::SignatureField> fields = ot::sign::signatureFields(*doc);
    ASSERT_EQ(fields.size(), 1u);
    EXPECT_FALSE(fields[0].isSigned);
    EXPECT_EQ(ot::sign::clearBlock(*doc, "Signature1"), ClearBlock::NotSigned);
    EXPECT_EQ(ot::sign::verifyDocument(*doc).status, ot::sign::DocumentStatus::NotSigned);
}

TEST(sig_clear, certification_removes_docmdp) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"серт.pdf");
    sign(path, false, ot::sign::MdpPermission::FormFilling, {});
    {
        auto doc = Document::openOrThrow(path);
        ASSERT_EQ(ot::sign::readDocMdp(*doc).permission, ot::sign::MdpPermission::FormFilling);
        ot::sign::clearSignature(*doc, "Signature1");
        ASSERT_TRUE(ot::Saver::save(*doc).ok());
    }
    auto doc = Document::openOrThrow(path);
    EXPECT_EQ(ot::sign::readDocMdp(*doc).permission, ot::sign::MdpPermission::None);
    EXPECT_EQ(ot::sign::certifyBlock(*doc), ot::sign::CertifyBlock::None);
}

TEST(sig_clear, locked_and_covered_signatures_stay) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"две.pdf");
    sign(path);
    sign(path, true, {}, {300, 600, 500, 660});  // вторая — с блокировкой: обе только для чтения
    auto doc = Document::openOrThrow(path);
    EXPECT_EQ(ot::sign::clearBlock(*doc, "Signature1"), ClearBlock::LaterSignatures);
    EXPECT_EQ(ot::sign::clearBlock(*doc, "Signature2"), ClearBlock::ReadOnly);
    EXPECT_THROW(ot::sign::clearSignature(*doc, "Signature1"), std::runtime_error);
}

TEST(cert_details, signer_extensions_and_fingerprints) {
    OT_FEATURE("sign-27");
    const std::string pem = ot::test::readPki("signer-rsa.pem");
    const ot::sign::Ossl<X509> cert = ot::sign::loadCertificate(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(pem.data()), pem.size()));
    const ot::sign::CertSummary summary = ot::sign::summarize(cert.get());
    const ot::sign::CertDetails d = ot::sign::certDetails(summary.der);
    EXPECT_EQ(d.version, 3);
    EXPECT_EQ(d.signatureAlgorithm, "sha256WithRSAEncryption");
    EXPECT_EQ(d.keyUsage, (std::vector<std::string>{"digitalSignature", "nonRepudiation"}));
    EXPECT_TRUE(d.keyUsageCritical);
    EXPECT_NE(std::find(d.extendedKeyUsage.begin(), d.extendedKeyUsage.end(), "emailProtection"),
              d.extendedKeyUsage.end());
    EXPECT_NE(std::find(d.extendedKeyUsage.begin(), d.extendedKeyUsage.end(), "1.3.6.1.5.5.7.3.36"),
              d.extendedKeyUsage.end());
    EXPECT_NE(std::find(d.extendedKeyUsage.begin(), d.extendedKeyUsage.end(), "1.2.840.113583.1.1.5"),
              d.extendedKeyUsage.end());
    EXPECT_EQ(d.ocspUrls, (std::vector<std::string>{"http://pki.acrobatya.test/ocsp"}));
    EXPECT_EQ(d.crlUrls, (std::vector<std::string>{"http://pki.acrobatya.test/inter.crl"}));
    EXPECT_TRUE(d.policies.empty());
    EXPECT_EQ(d.sha1.size(), 40u);
    EXPECT_EQ(d.sha256.size(), 64u);
    EXPECT_FALSE(d.extensions.empty());
    EXPECT_FALSE(d.publicKey.empty());
    // Корень: ЦС, назначение — подпись сертификатов и CRL.
    const std::string rootPem = ot::test::readPki("root.pem");
    const ot::sign::Ossl<X509> root = ot::sign::loadCertificate(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(rootPem.data()), rootPem.size()));
    const ot::sign::CertDetails r = ot::sign::certDetails(ot::sign::summarize(root.get()).der);
    EXPECT_EQ(r.keyUsage, (std::vector<std::string>{"keyCertSign", "cRLSign"}));
    EXPECT_TRUE(r.selfSigned);
    EXPECT_FALSE(d.selfSigned);
}
