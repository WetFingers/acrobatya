// unit.cms — CMS CAdES-B для PAdES (план 4.1, п. 3; эталон 3.8 «Механика»; sign-18 — ETSI.CAdES.detached,
// SHA-256): detached SignedData от RSA и ECDSA по потоку и по готовому хэшу проходит независимую проверку
// внешним `openssl cms -verify -cades` и CMS_verify своим кодом; подписанные атрибуты (свой DER-разбор, не
// OpenSSL CMS): content-type, message-digest = хэш данных, signing-certificate-v2 = SHA-256 сертификата
// подписанта, signing-time и S/MIME capabilities — нет; метка времени — unsigned-атрибут
// id-aa-timeStampToken, подпись не меняется; чужие CMS (openssl cms -sign: signing-time, ESS v1 на SHA-1,
// RSA-PSS) читаются.
#include <gtest/gtest.h>

#include <openssl/pem.h>

#include <string>

#include "core/sign/cms.h"
#include "core/sign/der.h"
#include "core/sign/pkcs12.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/test_pdf.h"

namespace {

using ot::sign::Bytes;
using ot::sign::CmsBuilder;
using ot::sign::CmsInfo;
using ot::sign::Ossl;
namespace oid = ot::sign::oid;

ot::sign::SigningKey identity(const char* pfx) {
    const std::string bytes = ot::test::readPki(pfx);
    return ot::sign::loadPkcs12(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), "acrobatya");
}

Ossl<X509_STORE> rootStore() {
    const std::string pem = ot::test::readPki("root.pem");
    Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    Ossl<X509> root(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
    Ossl<X509_STORE> store(X509_STORE_new());
    X509_STORE_add_cert(store.get(), root.get());
    return store;
}

Bytes bytesOf(std::string_view s) { return {s.begin(), s.end()}; }

std::string sampleData() {
    std::string data = "%PDF-1.7\n";
    for (int i = 0; i < 2000; ++i) {
        data += "АкроБатя подписывает байты " + std::to_string(i) + "\n";
    }
    return data;
}

Bytes certDer(X509* cert) { return ot::sign::ossl::toDer(cert, i2d_X509, "i2d_X509"); }

// Внешняя проверка: openssl cms -verify -cades -binary -inform DER -content … -CAfile root.pem.
struct ExternalCheck {
    bool available = false;
    int exitCode = -1;
    std::string output;
};

ExternalCheck opensslVerify(const ot::test::TempDir& dir, const Bytes& cms, std::string_view content,
                            bool cades = true) {
    ExternalCheck r;
    const auto exe = ot::test::opensslExe();
    if (!exe) {
        return r;
    }
    r.available = true;
    const auto sig = dir.path() / L"подпись.p7s";
    const auto data = dir.path() / L"данные.bin";
    const auto out = dir.path() / L"out.bin";
    ot::paths::writeFile(sig, std::string_view(reinterpret_cast<const char*>(cms.data()), cms.size()));
    ot::paths::writeFile(data, content);
    std::vector<std::wstring> args = {L"cms",
                                      L"-verify",
                                      L"-binary",
                                      L"-inform",
                                      L"DER",
                                      L"-in",
                                      sig.wstring(),
                                      L"-content",
                                      data.wstring(),
                                      L"-CAfile",
                                      (ot::test::pkiDir() / "root.pem").wstring(),
                                      L"-purpose",
                                      L"smimesign",
                                      L"-out",
                                      out.wstring()};
    if (cades) {
        args.insert(args.begin() + 2, L"-cades");
    }
    const auto res = ot::test::runProcess(*exe, args);
    r.exitCode = res.exitCode;
    r.output = res.output;
    return r;
}

// openssl cms -sign чужими средствами (для проверки разбора).
Bytes opensslSign(const ot::test::TempDir& dir, std::string_view content,
                  const std::vector<std::wstring>& extra) {
    const auto exe = ot::test::opensslExe();
    if (!exe) {
        return {};
    }
    const auto data = dir.path() / L"чужие.bin";
    const auto sig = dir.path() / L"чужая.p7s";
    ot::paths::writeFile(data, content);
    std::vector<std::wstring> args = {L"cms",
                                      L"-sign",
                                      L"-binary",
                                      L"-md",
                                      L"sha256",
                                      L"-signer",
                                      (ot::test::pkiDir() / "signer-rsa.pem").wstring(),
                                      L"-inkey",
                                      (ot::test::pkiDir() / "signer-rsa.key").wstring(),
                                      L"-in",
                                      data.wstring(),
                                      L"-outform",
                                      L"DER",
                                      L"-out",
                                      sig.wstring()};
    args.insert(args.end(), extra.begin(), extra.end());
    const auto res = ot::test::runProcess(*exe, args);
    EXPECT_EQ(res.exitCode, 0) << res.output;
    const std::string bytes = ot::paths::readFile(sig);
    return {bytes.begin(), bytes.end()};
}

void expectCadesB(const CmsInfo& info, X509* signer, const Bytes& digest) {
    EXPECT_TRUE(info.detached);
    EXPECT_EQ(info.contentType, std::string(oid::kData));
    EXPECT_EQ(info.signerCount, 1);
    EXPECT_EQ(info.digestAlgorithm, std::string(oid::kSha256));
    // Подписанные атрибуты: content-type, message-digest, signing-certificate-v2 — и ничего больше.
    ASSERT_NE(info.signedAttribute(oid::kContentType), nullptr);
    ASSERT_NE(info.signedAttribute(oid::kMessageDigest), nullptr);
    ASSERT_NE(info.signedAttribute(oid::kSigningCertificateV2), nullptr);
    EXPECT_EQ(info.signedAttribute(oid::kSigningTime), nullptr);
    EXPECT_FALSE(info.signingTime.has_value());
    EXPECT_EQ(info.signedAttribute(oid::kSmimeCapabilities), nullptr);
    EXPECT_EQ(info.signedAttribute(oid::kSigningCertificate), nullptr);
    EXPECT_EQ(info.signedAttributes.size(), 3U);
    for (const auto& a : info.signedAttributes) {
        EXPECT_EQ(a.values, 1) << a.oid;
    }
    EXPECT_EQ(info.messageDigest, digest);
    ASSERT_TRUE(info.signingCertificateV2.has_value());
    EXPECT_EQ(info.signingCertificateV2->hashAlgorithm, std::string(oid::kSha256));
    EXPECT_EQ(info.signingCertificateV2->certHash, ot::sign::ossl::sha256(certDer(signer)));
    // Сертификаты: подписант и цепочка из .pfx.
    EXPECT_EQ(info.certificates.size(), 3U);
    ASSERT_GE(info.signerCertIndex, 0);
    EXPECT_EQ(info.certificates[static_cast<std::size_t>(info.signerCertIndex)].der, certDer(signer));
    EXPECT_EQ(info.certificates[static_cast<std::size_t>(info.signerCertIndex)].commonName, "АкроБатя тест");
    EXPECT_FALSE(info.signature.empty());
    EXPECT_TRUE(info.unsignedAttributes.empty());
    EXPECT_TRUE(info.timestampToken.empty());
}

}  // namespace

TEST(cms, rsa_cades_b_over_stream) {
    OT_FEATURE("sign-18");
    const auto id = identity("signer-rsa.pfx");
    const std::string data = sampleData();
    CmsBuilder builder(id);
    auto in = ot::sign::ossl::memBio(bytesOf(data));
    const Bytes der = builder.build(in.get());
    ASSERT_FALSE(der.empty());
    EXPECT_LT(der.size(), 16384U);  // резерв /Contents (эталон 3.8)

    const CmsInfo info = ot::sign::parseCms(der);
    expectCadesB(info, id.cert.get(), ot::sign::ossl::sha256(std::string_view(data)));
    EXPECT_EQ(info.signatureAlgorithm, std::string(oid::kRsaEncryption));
    EXPECT_EQ(ot::sign::algorithmName(info.signatureAlgorithm), "RSA");
    EXPECT_EQ(ot::sign::algorithmName(info.digestAlgorithm), "SHA-256");
    EXPECT_EQ(info.signature, builder.signatureValue());

    // Своя проверка: целостность (без цепочки) и с цепочкой до корня (CAdES: ESS сверяется).
    std::string error;
    auto c1 = ot::sign::ossl::memBio(bytesOf(data));
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(der, c1.get(), nullptr, &error)) << error;
    auto store = rootStore();
    auto c2 = ot::sign::ossl::memBio(bytesOf(data));
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(der, c2.get(), store.get(), &error)) << error;
    auto tampered = ot::sign::ossl::memBio(bytesOf(data + " "));
    EXPECT_FALSE(ot::sign::cmsVerifyDetached(der, tampered.get(), nullptr, &error));
    EXPECT_FALSE(error.empty());

    // Независимая проверка внешним openssl.
    ot::test::TempDir dir;
    const ExternalCheck ext = opensslVerify(dir, der, data);
    if (!ext.available) {
        GTEST_SKIP() << "openssl.exe not found";
    }
    EXPECT_EQ(ext.exitCode, 0) << ext.output;
    EXPECT_NE(ext.output.find("Verification successful"), std::string::npos) << ext.output;
    const ExternalCheck bad = opensslVerify(dir, der, data + "x");
    EXPECT_NE(bad.exitCode, 0) << bad.output;
}

TEST(cms, ecdsa_from_digest_of_byte_ranges) {
    OT_FEATURE("sign-18");
    const auto id = identity("signer-ec.pfx");
    // Как у PDF: файл, «дыра» под /Contents и хэш двух частей.
    ot::test::TempDir dir;
    const auto file = dir.path() / L"документ.pdf";
    const std::string whole = sampleData();
    ot::paths::writeFile(file, whole);
    const std::vector<ot::sign::ByteRange> ranges = {{0, 1000},
                                                     {5000, static_cast<std::int64_t>(whole.size()) - 5000}};
    const Bytes digest = ot::sign::ossl::sha256Ranges(file, ranges);
    const std::string signedPart = whole.substr(0, 1000) + whole.substr(5000);

    CmsBuilder builder(id);
    const Bytes der = builder.buildFromDigest(digest);
    const CmsInfo info = ot::sign::parseCms(der);
    expectCadesB(info, id.cert.get(), digest);
    EXPECT_EQ(info.signatureAlgorithm, std::string(oid::kEcdsaWithSha256));
    EXPECT_EQ(ot::sign::algorithmName(info.signatureAlgorithm), "ECDSA with SHA-256");

    std::string error;
    auto content = ot::sign::ossl::openRangeBio(file, ranges);
    auto store = rootStore();
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(der, content.get(), store.get(), &error)) << error;

    const ExternalCheck ext = opensslVerify(dir, der, signedPart);
    if (!ext.available) {
        GTEST_SKIP() << "openssl.exe not found";
    }
    EXPECT_EQ(ext.exitCode, 0) << ext.output;
}

TEST(cms, stream_and_digest_give_same_attributes) {
    const auto id = identity("signer-rsa.pfx");
    const std::string data = sampleData();
    CmsBuilder a(id);
    auto in = ot::sign::ossl::memBio(bytesOf(data));
    const CmsInfo byStream = ot::sign::parseCms(a.build(in.get()));
    CmsBuilder b(id);
    const CmsInfo byDigest =
        ot::sign::parseCms(b.buildFromDigest(ot::sign::ossl::sha256(std::string_view(data))));
    ASSERT_EQ(byStream.signedAttributes.size(), byDigest.signedAttributes.size());
    for (std::size_t i = 0; i < byStream.signedAttributes.size(); ++i) {
        EXPECT_EQ(byStream.signedAttributes[i].oid, byDigest.signedAttributes[i].oid);
        EXPECT_EQ(byStream.signedAttributes[i].value, byDigest.signedAttributes[i].value);
    }
    // RSA PKCS#1 v1.5 детерминирована — подпись та же.
    EXPECT_EQ(byStream.signature, byDigest.signature);
}

TEST(cms, unsigned_timestamp_attribute) {
    const auto id = identity("signer-rsa.pfx");
    const std::string data = sampleData();
    CmsBuilder builder(id);
    const Bytes plain = builder.buildFromDigest(ot::sign::ossl::sha256(std::string_view(data)));
    const Bytes signature = builder.signatureValue();
    // Токен — любой ContentInfo в DER; настоящий от TSA — unit.tsa.
    const Bytes token = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                         0xf7, 0x0d, 0x01, 0x07, 0x02, 0xa0, 0x00};
    const Bytes stamped = builder.addUnsignedTimestamp(token);
    EXPECT_GT(stamped.size(), plain.size());
    const CmsInfo info = ot::sign::parseCms(stamped);
    ASSERT_EQ(info.unsignedAttributes.size(), 1U);
    EXPECT_EQ(info.unsignedAttributes[0].oid, std::string(oid::kTimeStampToken));
    EXPECT_EQ(info.timestampToken, token);
    EXPECT_EQ(info.signature, signature);  // подпись не меняется
    EXPECT_EQ(info.signedAttributes.size(), 3U);
    // Свободная функция над DER — то же.
    EXPECT_EQ(ot::sign::cmsSignatureValue(stamped), signature);
    EXPECT_EQ(ot::sign::parseCms(ot::sign::cmsAddUnsignedTimestamp(plain, token)).timestampToken, token);

    std::string error;
    auto content = ot::sign::ossl::memBio(bytesOf(data));
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(stamped, content.get(), nullptr, &error)) << error;
    ot::test::TempDir dir;
    const ExternalCheck ext = opensslVerify(dir, stamped, data);
    if (!ext.available) {
        GTEST_SKIP() << "openssl.exe not found";
    }
    EXPECT_EQ(ext.exitCode, 0) << ext.output;
}

TEST(cms, rejects_bad_input) {
    const auto rsa = identity("signer-rsa.pfx");
    const auto ec = identity("signer-ec.pfx");
    // Ключ не от этого сертификата.
    CmsBuilder mismatched(rsa.cert.get(), ec.key.get(), rsa.chain.get());
    EXPECT_THROW((void)mismatched.buildFromDigest(Bytes(32, 1)), std::runtime_error);
    // Длина хэша не та.
    CmsBuilder ok(rsa);
    EXPECT_THROW((void)ok.buildFromDigest(Bytes(20, 1)), std::invalid_argument);
    // Метка до подписи.
    EXPECT_THROW((void)ok.addUnsignedTimestamp(Bytes{0x30, 0x00}), std::logic_error);
    // Не CMS.
    EXPECT_THROW((void)ot::sign::parseCms(Bytes{0x30, 0x03, 0x02, 0x01, 0x01}), std::runtime_error);
    EXPECT_THROW((void)ot::sign::parseCms(Bytes{}), std::runtime_error);
    EXPECT_THROW((void)ot::sign::parseCms(Bytes{0x30, 0x84, 0xff}), std::runtime_error);
}

TEST(cms, foreign_cms_with_signing_time_and_ess_v1) {
    ot::test::TempDir dir;
    const std::string data = "foreign signer";
    // CAdES от openssl без -no_signing_time: signing-time есть, ESS v2.
    const Bytes withTime = opensslSign(dir, data, {L"-cades", L"-nodetach"});
    if (withTime.empty()) {
        GTEST_SKIP() << "openssl.exe not found";
    }
    const CmsInfo a = ot::sign::parseCms(withTime);
    EXPECT_FALSE(a.detached);
    ASSERT_TRUE(a.signingTime.has_value());
    EXPECT_EQ(a.signingTime->size(), 20U) << *a.signingTime;  // 2026-10-24T12:00:00Z
    EXPECT_TRUE(a.signingCertificateV2.has_value());
    EXPECT_NE(a.signedAttribute(oid::kSmimeCapabilities), nullptr);
    EXPECT_EQ(a.messageDigest, ot::sign::ossl::sha256(std::string_view(data)));

    // Старый ESS v1 (SHA-1) — читается, v2 нет.
    const Bytes v1 = opensslSign(dir, data, {L"-cades", L"-md", L"sha1"});
    const CmsInfo b = ot::sign::parseCms(v1);
    EXPECT_FALSE(b.signingCertificateV2.has_value());
    EXPECT_TRUE(b.hasSigningCertificateV1);
    EXPECT_EQ(ot::sign::algorithmName(b.digestAlgorithm), "SHA-1");
}

TEST(cms, rsa_pss_is_read_only) {
    ot::test::TempDir dir;
    const std::string data = "pss signer";
    const Bytes pss = opensslSign(dir, data, {L"-keyopt", L"rsa_padding_mode:pss", L"-nosmimecap"});
    if (pss.empty()) {
        GTEST_SKIP() << "openssl.exe not found";
    }
    const CmsInfo info = ot::sign::parseCms(pss);
    EXPECT_EQ(info.signatureAlgorithm, std::string(oid::kRsaPss));
    EXPECT_EQ(ot::sign::algorithmName(info.signatureAlgorithm), "RSASSA-PSS");
    std::string error;
    auto content = ot::sign::ossl::memBio(bytesOf(data));
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(pss, content.get(), nullptr, &error)) << error;
}

TEST(der, reader_basics) {
    // SEQUENCE { INTEGER 5, OID 1.2.840.113549.1.9.4, [0] { NULL } }
    const Bytes data = {0x30, 0x12, 0x02, 0x01, 0x05, 0x06, 0x09, 0x2a, 0x86, 0x48,
                        0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04, 0xa0, 0x02, 0x05, 0x00};
    ot::sign::der::Reader top(data);
    const ot::sign::der::Node seq = top.expect(0x30, "seq");
    EXPECT_TRUE(top.atEnd());
    ot::sign::der::Reader in(seq.value);
    EXPECT_EQ(ot::sign::der::toInt(in.expect(0x02, "int")), 5);
    EXPECT_EQ(ot::sign::der::toOid(in.expect(0x06, "oid").value), std::string(oid::kMessageDigest));
    ot::sign::der::Node tagged;
    EXPECT_TRUE(in.nextIf(0xa0, tagged));
    EXPECT_TRUE(in.atEnd());
    EXPECT_THROW(ot::sign::der::Reader(Bytes{0x30, 0x05, 0x02}).next(), std::runtime_error);
    // BER: неопределённая длина у составного.
    const Bytes ber = {0x30, 0x80, 0x02, 0x01, 0x07, 0x00, 0x00, 0x05, 0x00};
    ot::sign::der::Reader r(ber);
    const ot::sign::der::Node n = r.next();
    EXPECT_EQ(n.encoded.size(), 7U);
    ot::sign::der::Reader inner(n.value);
    EXPECT_EQ(ot::sign::der::toInt(inner.next()), 7);
    EXPECT_TRUE(inner.atEnd());
    EXPECT_EQ(r.next().tag, 0x05);
    // Время.
    const Bytes utc = {'2', '6', '0', '6', '0', '1', '1', '2', '3', '0', '0', '0', 'Z'};
    EXPECT_EQ(ot::sign::der::toTime(0x17, utc), "2026-06-01T12:30:00Z");
    const Bytes gen = {'2', '0', '5', '1', '0', '1', '0', '2', '0', '3', '0', '4', '0', '5', 'Z'};
    EXPECT_EQ(ot::sign::der::toTime(0x18, gen), "2051-01-02T03:04:05Z");
}
