// unit.sign_cng_flow — подпись документа сертификатом хранилища Windows (план 4.3, п. 3; эталон 3.8
// «CNG-путь», «Диалог подписания» — хранилище Windows; sign-08 сквозной, sign-12 — поставить подпись
// удостоверением из хранилища). SignRequest::identity = WindowsCert{отпечаток} над PFX-импортом тестовой PKI
// в памяти процесса (без окон и без касания хранилищ человека): AcroBatyaSigner идёт по CNG-ветке, CMS
// проходит проверку с цепочкой до корня PKI; для RSA CMS в файле равна OpenSSL-пути (.pfx) байт в байт (тот
// же DER-формат); B-T — метка службы PKI по значению CNG-подписи; ECDSA P-256; две подписи подряд — .pfx и
// CNG — обе целы; нет сертификата или ключа — SignError до записи, файл не тронут; DocumentSession::sign
// принимает WindowsCert. sign_compat.DISABLED_write_cng пишет файлы для compat.sign.cng (pyHanko INTACT,
// TRUSTED).
#include <gtest/gtest.h>

#include <QString>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/sign/cms.h"
#include "core/sign/cms_external.h"
#include "core/sign/cng_store.h"
#include "core/sign/signer.h"
#include "core/sign/tsa.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::SignError;
using ot::sign::SignRequest;
using ot::sign::SignResult;
using ot::sign::SignStatus;
using ot::sign::WindowsCert;
using ot::test::DocView;
using ot::test::SigView;

namespace {

SignRequest request(ot::sign::SigningIdentity identity) {
    SignRequest r;
    r.identity = std::move(identity);
    r.reason = "Согласовано";
    r.location = "Москва";
    r.placement.rect = ot::RectF{72, 72, 272, 132};
    r.signingTime = 1793088000;  // 2026-10-27T08:00:00Z
    return r;
}

SignResult signFile(const fs::path& file, const SignRequest& r) {
    auto doc = Document::openOrThrow(file);
    return ot::sign::signDocument(*doc, r);
}

void expectTimestamp(const SigView& s) {
    const ot::sign::CmsInfo info = ot::sign::parseCms(s.contents);
    ASSERT_FALSE(info.timestampToken.empty()) << "в CMS нет id-aa-timeStampToken";
    const auto store = ot::test::pkiRootStore();
    std::string error;
    ot::sign::TimestampInfo ts;
    EXPECT_TRUE(ot::sign::verifyTimestampToken(info.timestampToken, ot::sign::ossl::sha256(info.signature),
                                               store.get(), &error, &ts))
        << error;
    EXPECT_TRUE(ts.trusted);
}

}  // namespace

TEST(sign_cng_flow, windows_cert_signs_like_pfx) {
    OT_FEATURE("sign-08");
    OT_FEATURE("sign-12");
    ot::test::TempDir dir;
    const fs::path viaCng = ot::test::unsignedCopy(dir.path(), L"хранилище.pdf");
    const fs::path viaPfx = ot::test::unsignedCopy(dir.path(), L"pfx.pdf");
    const SignResult r = signFile(viaCng, request(ot::test::windowsIdentity()));
    EXPECT_TRUE(r.save.ok());
    EXPECT_FALSE(r.timestamped);
    (void)signFile(viaPfx, request(ot::test::pkiIdentity()));

    const DocView view = ot::test::readSignatures(viaCng);
    ASSERT_EQ(view.sigs.size(), 1u);
    const SigView& s = view.sigs.front();
    ot::test::expectCmsValid(viaCng, s);
    EXPECT_TRUE(ot::test::coversFile(viaCng, s));
    EXPECT_EQ(s.subFilter, "ETSI.CAdES.detached");
    EXPECT_EQ(s.name, "АкроБатя тест");
    const ot::sign::CmsInfo info = ot::sign::parseCms(s.contents);
    ASSERT_GE(info.signerCertIndex, 0);
    EXPECT_EQ(info.certificates[static_cast<std::size_t>(info.signerCertIndex)].commonName, "АкроБатя тест");
    // Тот же DER-формат: OpenSSL-путь (.pfx) над тем же хэшем /ByteRange даёт ту же CMS байт в байт (RSA
    // PKCS#1 v1.5 детерминирован). Файлы целиком различаются только вторым /ID трейлера — MuPDF обновляет его
    // при записи.
    const Bytes digest = ot::sign::ossl::sha256Ranges(viaCng, s.ranges);
    ot::sign::CmsBuilder reference(*ot::test::pkiIdentity());
    EXPECT_EQ(reference.buildFromDigest(digest), s.contents);
    const DocView pfxView = ot::test::readSignatures(viaPfx);
    ASSERT_EQ(pfxView.sigs.size(), 1u);
    EXPECT_EQ(ot::sign::parseCms(pfxView.sigs.front().contents).signedAttributesDer.size(),
              info.signedAttributesDer.size());
    EXPECT_EQ(pfxView.sigs.front().ranges[0].length, s.ranges[0].length);
}

TEST(sign_cng_flow, windows_cert_bt) {
    ot::test::TempDir dir;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"хранилище-метка.pdf");
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    SignRequest r = request(ot::test::windowsIdentity());
    r.timestamp = tsa.client;
    const SignResult result = signFile(file, r);
    EXPECT_TRUE(result.timestamped);
    EXPECT_FALSE(result.timestamp.genTime.empty());
    const DocView view = ot::test::readSignatures(file);
    ASSERT_EQ(view.sigs.size(), 1u);
    ot::test::expectCmsValid(file, view.sigs.front());
    expectTimestamp(view.sigs.front());
}

TEST(sign_cng_flow, ecdsa_windows_cert) {
    ot::test::TempDir dir;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"хранилище-ec.pdf");
    (void)signFile(file, request(ot::test::windowsIdentity("signer-ec.pfx")));
    const DocView view = ot::test::readSignatures(file);
    ASSERT_EQ(view.sigs.size(), 1u);
    ot::test::expectCmsValid(file, view.sigs.front());
    EXPECT_EQ(ot::sign::parseCms(view.sigs.front().contents).signatureAlgorithm,
              ot::sign::oid::kEcdsaWithSha256);
}

TEST(sign_cng_flow, pfx_then_windows_cert) {
    ot::test::TempDir dir;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"две-подписи.pdf");
    (void)signFile(file, request(ot::test::pkiIdentity()));
    SignRequest second = request(ot::test::windowsIdentity("signer-ec.pfx"));
    second.placement.rect = ot::RectF{300, 72, 500, 132};
    const SignResult r = signFile(file, second);
    EXPECT_EQ(r.field, "Signature2");
    const DocView view = ot::test::readSignatures(file);
    ASSERT_EQ(view.sigs.size(), 2u);
    for (const SigView& s : view.sigs) {
        SCOPED_TRACE(s.field);
        ot::test::expectCmsValid(file, s);
    }
    EXPECT_FALSE(ot::test::coversFile(file, view.sigs[0]));
    EXPECT_TRUE(ot::test::coversFile(file, view.sigs[1]));
}

TEST(sign_cng_flow, missing_certificate_or_key_leaves_file) {
    ot::test::TempDir dir;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"без-ключа.pdf");
    const std::string before = ot::paths::readFile(file);
    const auto expectStatus = [&](const WindowsCert& cert, SignStatus status) {
        auto doc = Document::openOrThrow(file);
        try {
            (void)ot::sign::signDocument(*doc, request(cert));
            ADD_FAILURE() << "signed without a usable Windows identity";
        } catch (const SignError& e) {
            EXPECT_EQ(e.status(), status) << e.what();
        }
        EXPECT_FALSE(doc->undoStack().canUndo());
        EXPECT_EQ(ot::paths::readFile(file), before);
    };
    WindowsCert unknown = ot::test::windowsIdentity();
    unknown.thumbprint = std::string(40, 'A');
    expectStatus(unknown, SignStatus::NoIdentity);
    WindowsCert garbage = unknown;
    garbage.thumbprint = "не отпечаток";
    expectStatus(garbage, SignStatus::NoIdentity);

    // Сертификат без закрытого ключа.
    auto store = std::make_shared<ot::sign::cng::CertStore>(ot::sign::cng::CertStore::openMemory());
    const auto pem = ot::test::pkiCert("signer-rsa.pem");
    (void)store->addDer(ot::sign::ossl::toDer(pem.get(), i2d_X509, "i2d_X509"));
    WindowsCert noKey;
    noKey.thumbprint = ot::test::windowsIdentity().thumbprint;  // подписант — но без ключа
    noKey.silent = true;
    noKey.store = store;
    expectStatus(noKey, SignStatus::NoPrivateKey);
}

TEST(sign_cng_flow, session_signs_with_windows_cert) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"сессия-хранилище.pdf");
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(path.wstring())));
    const SignResult r = session.sign(request(ot::test::windowsIdentity())).result();
    EXPECT_TRUE(r.save.ok());
    EXPECT_TRUE(session.summary().isSigned);
    const DocView view = ot::test::readSignatures(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    ot::test::expectCmsValid(path, view.sigs.front());
}

// compat.sign.cng: подписи сертификатом «хранилища» (CNG) — RSA и ECDSA B-B, RSA B-T, .pfx затем CNG; pyHanko
// INTACT, TRUSTED; qpdf, pdfcpu strict, mutool, pdfium.
TEST(sign_compat, DISABLED_write_cng) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path out = fs::path(ot::paths::fromUtf8(outEnv)) / L"sign" / L"cng";
    fs::remove_all(out);
    fs::create_directories(out);
    (void)signFile(ot::test::unsignedCopy(out, L"rsa-cng-bb.pdf"), request(ot::test::windowsIdentity()));
    (void)signFile(ot::test::unsignedCopy(out, L"ec-cng-bb.pdf"),
                   request(ot::test::windowsIdentity("signer-ec.pfx")));
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    SignRequest bt = request(ot::test::windowsIdentity());
    bt.timestamp = tsa.client;
    (void)signFile(ot::test::unsignedCopy(out, L"rsa-cng-bt.pdf"), bt);
    const fs::path two = ot::test::unsignedCopy(out, L"pfx-then-cng.pdf");
    (void)signFile(two, request(ot::test::pkiIdentity()));
    SignRequest second = request(ot::test::windowsIdentity("signer-ec.pfx"));
    second.placement.rect = ot::RectF{300, 72, 500, 132};
    (void)signFile(two, second);
}
