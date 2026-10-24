// unit.trust_store_full — хранилище доверия целиком (план 4.5, п. 5; эталон 3.8 «Доверие»; sign-30 — флаги и
// экспорт, sign-34 — импорт из файла, подписи и хранилища Windows, sign-08 — корни Windows для подписей и
// сертифицированных документов): «Доверять» из подписи (корень цепочки или выбранный сертификат) делает
// подпись действительной; хранилище Windows ROOT и CA читается только по явному вызову; экспорт
// .cer/.p7c/.fdf и импорт обратно; корни Windows — по назначениям и сохраняются; сертифицированный документ
// доверен только якорем «сертифицированные документы», динамический контент и JavaScript — по флагам якоря.
#include <gtest/gtest.h>

#include <openssl/x509.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/sign/identity.h"
#include "core/sign/signer.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::kTrustCertification;
using ot::sign::kTrustDynamicContent;
using ot::sign::kTrustJavaScript;
using ot::sign::kTrustSignatures;
using ot::sign::TrustStore;
using ot::sign::Verdict;

namespace {

ot::sign::Bytes pki(const char* name) {
    const std::string s = ot::test::readPki(name);
    const ot::sign::Ossl<X509> x = ot::sign::loadCertificate(ot::sign::Bytes(s.begin(), s.end()));
    return ot::sign::ossl::toDer(x.get(), i2d_X509, "i2d_X509");
}

std::string sha256Of(const char* name) { return ot::sign::ossl::toHex(ot::sign::ossl::sha256(pki(name))); }

fs::path signedFile(const fs::path& dir, const std::wstring& name) {
    const fs::path file = ot::test::unsignedCopy(dir, name);
    ot::sign::SignRequest r;
    r.identity = ot::test::pkiIdentity();
    auto doc = Document::openOrThrow(file);
    (void)ot::sign::signDocument(*doc, r);
    return file;
}

ot::sign::DocumentVerification verify(const fs::path& file, const TrustStore* trust) {
    const auto offline = ot::test::offlineFetcher();
    ot::sign::VerifyOptions vo;
    vo.trust = trust;
    vo.fetcher = offline.get();
    auto doc = Document::openOrThrow(file);
    return ot::sign::verifyDocument(*doc, vo);
}

int objects(const TrustStore& store, unsigned flags) {
    const ot::sign::Ossl<X509_STORE> s = store.x509Store(flags);
    return sk_X509_OBJECT_num(X509_STORE_get0_objects(s.get()));
}

}  // namespace

// «Доверять» из подписи: по умолчанию — корень построенной цепочки; выбранный сертификат — он сам.
TEST(trust_store_full, import_from_signature) {
    OT_FEATURE("sign-34");
    OT_FEATURE("sign-30");
    ot::test::TempDir dir;
    const fs::path file = signedFile(dir.path(), L"из подписи.pdf");
    const ot::sign::DocumentVerification before = verify(file, nullptr);
    ASSERT_EQ(before.signatures.size(), 1u);
    EXPECT_EQ(before.signatures[0].verdict, Verdict::ValidUntrusted);

    TrustStore store;
    EXPECT_TRUE(ot::sign::trustFromSignature(store, before.signatures[0], kTrustSignatures));
    ASSERT_EQ(store.anchors().size(), 1u);
    EXPECT_EQ(store.anchors()[0].sha256, sha256Of("root.pem"));
    EXPECT_FALSE(ot::sign::trustFromSignature(store, before.signatures[0], kTrustSignatures)) << "уже есть";
    EXPECT_EQ(verify(file, &store).signatures[0].verdict, Verdict::Valid);

    TrustStore leaf;
    const std::string signer = sha256Of("signer-rsa.pem");
    EXPECT_TRUE(ot::sign::trustFromSignature(leaf, before.signatures[0], kTrustSignatures, signer));
    ASSERT_EQ(leaf.anchors().size(), 1u);
    EXPECT_EQ(leaf.anchors()[0].sha256, signer);
    EXPECT_EQ(verify(file, &leaf).signatures[0].verdict, Verdict::Valid);
    // Сертификата с таким отпечатком в подписи нет.
    EXPECT_FALSE(ot::sign::trustFromSignature(leaf, before.signatures[0], kTrustSignatures, "00112233"));
}

// Хранилище Windows ROOT и CA — только чтение и только по вызову; якоря источника Windows с выбранными
// флагами.
TEST(trust_store_full, import_from_windows_store) {
    OT_FEATURE("sign-34");
    OT_FEATURE("sign-08");
    const std::vector<ot::sign::Bytes> roots =
        ot::sign::windowsStoreCertificates(ot::sign::WindowsStore::Root);
    ASSERT_FALSE(roots.empty()) << "в Windows всегда есть корневые сертификаты";
    const std::vector<ot::sign::Bytes> cas = ot::sign::windowsStoreCertificates(ot::sign::WindowsStore::Ca);
    TrustStore store;
    const int added = store.importWindows(true, false, kTrustSignatures | kTrustCertification);
    EXPECT_GT(added, 0);
    EXPECT_LE(added, static_cast<int>(roots.size()));
    for (const ot::sign::TrustAnchor& a : store.anchors()) {
        EXPECT_EQ(a.source, ot::sign::TrustSource::Windows);
        EXPECT_EQ(a.flags, kTrustSignatures | kTrustCertification);
    }
    const int withCa = store.importWindows(false, true);
    EXPECT_GE(withCa, 0);
    EXPECT_LE(withCa, static_cast<int>(cas.size()));
    EXPECT_EQ(store.importWindows(true, false), 0) << "повторно — без повторов";
}

// Флаги и экспорт: .cer — DER якоря, .p7c — все якоря, .fdf — Acrobat; импорт обратно даёт то же.
TEST(trust_store_full, flags_and_export) {
    OT_FEATURE("sign-30");
    TrustStore store;
    ASSERT_TRUE(store.add(pki("root.pem")));
    ASSERT_TRUE(store.add(pki("inter.pem"), kTrustSignatures | kTrustCertification));
    const std::string root = sha256Of("root.pem");
    EXPECT_TRUE(store.setFlags(root, kTrustSignatures | kTrustDynamicContent));
    EXPECT_EQ(store.find(root)->flags, kTrustSignatures | kTrustDynamicContent);

    EXPECT_EQ(store.exportAnchor(root, ot::sign::CertFormat::Cer), pki("root.pem"));
    EXPECT_TRUE(store.exportAnchor("00", ot::sign::CertFormat::Cer).empty());
    const ot::sign::Bytes p7c = store.exportAll();
    TrustStore back;
    EXPECT_EQ(back.import(p7c), 2);
    const ot::sign::Bytes fdf = store.exportAnchor(root, ot::sign::CertFormat::Fdf);
    TrustStore fromFdf;
    EXPECT_EQ(fromFdf.import(fdf, kTrustCertification), 1);
    EXPECT_EQ(fromFdf.anchors()[0].sha256, root);
    EXPECT_EQ(fromFdf.anchors()[0].flags, kTrustCertification);
}

// Корни Windows по назначениям (Acrobat iMSStoreTrusted): только подписи, только сертификация; сохраняются.
TEST(trust_store_full, windows_roots_by_purpose) {
    OT_FEATURE("sign-08");
    ot::test::TempDir dir;
    TrustStore store;
    EXPECT_EQ(store.windowsRootsFlags(), 0u);
    EXPECT_EQ(objects(store, kTrustSignatures), 0);
    store.setWindowsRootsFlags(kTrustCertification);
    EXPECT_EQ(objects(store, kTrustSignatures), 0);
    EXPECT_GT(objects(store, kTrustCertification), 0);
    store.setUseWindowsRoots(true);
    EXPECT_EQ(store.windowsRootsFlags(), kTrustSignatures);
    EXPECT_GT(objects(store, kTrustSignatures), 0);
    store.setWindowsRootsFlags(kTrustSignatures | kTrustCertification);
    store.save(dir.path());
    const TrustStore loaded = TrustStore::load(dir.path());
    EXPECT_EQ(loaded.windowsRootsFlags(), kTrustSignatures | kTrustCertification);
    EXPECT_TRUE(loaded.useWindowsRoots());
}

// «Сертифицированные документы»: якорь только для подписей — сертификация не доверена; с флагом сертификации
// — доверена; динамический контент и JavaScript — по флагам якоря.
TEST(trust_store_full, certified_documents_need_certification_trust) {
    OT_FEATURE("sign-30");
    ot::test::TempDir dir;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"сертифицирован.pdf");
    {
        ot::sign::SignRequest r;
        r.identity = ot::test::pkiIdentity();
        r.certify = ot::sign::MdpPermission::FormFilling;
        auto doc = Document::openOrThrow(file);
        (void)ot::sign::signDocument(*doc, r);
    }
    TrustStore signaturesOnly;
    signaturesOnly.add(pki("root.pem"), kTrustSignatures);
    const ot::sign::DocumentVerification v1 = verify(file, &signaturesOnly);
    ASSERT_EQ(v1.signatures.size(), 1u);
    EXPECT_EQ(v1.signatures[0].verdict, Verdict::ValidUntrusted);
    const ot::sign::CertifiedPrivileges p1 = ot::sign::certifiedPrivileges(v1, signaturesOnly);
    EXPECT_TRUE(p1.certified);
    EXPECT_FALSE(p1.trusted);
    EXPECT_FALSE(p1.dynamicContent);

    TrustStore certification;
    certification.add(pki("root.pem"), kTrustSignatures | kTrustCertification | kTrustDynamicContent);
    const ot::sign::DocumentVerification v2 = verify(file, &certification);
    EXPECT_EQ(v2.signatures[0].verdict, Verdict::Valid);
    const ot::sign::CertifiedPrivileges p2 = ot::sign::certifiedPrivileges(v2, certification);
    EXPECT_TRUE(p2.certified);
    EXPECT_TRUE(p2.trusted);
    EXPECT_TRUE(p2.dynamicContent);
    EXPECT_FALSE(p2.javaScript);
    EXPECT_EQ(p2.anchor, sha256Of("root.pem"));

    certification.setFlags(sha256Of("root.pem"), kTrustCertification | kTrustJavaScript);
    EXPECT_TRUE(ot::sign::certifiedPrivileges(verify(file, &certification), certification).javaScript);

    const fs::path approval = signedFile(dir.path(), L"утверждающая.pdf");
    const ot::sign::CertifiedPrivileges p3 =
        ot::sign::certifiedPrivileges(verify(approval, &certification), certification);
    EXPECT_FALSE(p3.certified);
    EXPECT_FALSE(p3.trusted);
}
