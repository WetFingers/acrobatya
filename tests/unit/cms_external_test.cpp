// unit.cms_external — CMS с внешней подписью (план 4.3, п. 2; эталон 3.8 «CNG-путь»: «сравнение атрибутов
// CNG-пути с OpenSSL-путём байт в байт»; 11 риск 2): signed-атрибуты своим кодом (content-type,
// message-digest, ESS signing-certificate-v2; без signing-time) равны тем, что собирает OpenSSL CMS
// (CmsBuilder), байт в байт — RSA и ECDSA, SHA-256 и SHA-384; PKCS7-путь с подписью «снаружи» (здесь — ключ
// .pfx через EVP_PKEY_sign, CNG — cng_signer_test) даёт для RSA тот же DER целиком, для ECDSA — проходящий
// CMS_verify с цепочкой и внешний openssl cms -verify -cades; метка времени добавляется unsigned-атрибутом,
// подпись не меняется; ошибка подписанта выходит наружу.
#include <gtest/gtest.h>

#include <openssl/evp.h>

#include <stdexcept>
#include <string>

#include "core/sign/cms.h"
#include "core/sign/cms_external.h"
#include "core/sign/der.h"
#include "core/sign/pkcs12.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/test_pdf.h"

namespace {

using ot::sign::Bytes;
using ot::sign::CmsBuilder;
using ot::sign::DigestAlgorithm;
using ot::sign::ExternalCmsBuilder;
using ot::sign::Ossl;
using ot::sign::SigningKey;

SigningKey identity(const char* pfx) {
    const std::string bytes = ot::test::readPki(pfx);
    return ot::sign::loadPkcs12(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), "acrobatya");
}

Bytes digestOf(std::string_view data, DigestAlgorithm alg) {
    Bytes out(EVP_MAX_MD_SIZE);
    unsigned int len = 0;
    EVP_Digest(data.data(), data.size(), out.data(), &len, ot::sign::evpDigest(alg), nullptr);
    out.resize(len);
    return out;
}

// «Внешний» подписант на ключе .pfx: хэш атрибутов → PKCS#1 v1.5 (RSA) или DER (ECDSA).
ot::sign::ExternalSignFn opensslSigner(EVP_PKEY* key, DigestAlgorithm alg, int* calls = nullptr) {
    return [key, alg, calls](std::span<const std::uint8_t> hash) {
        if (calls != nullptr) {
            ++*calls;
        }
        Ossl<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(key, nullptr));
        EXPECT_EQ(EVP_PKEY_sign_init(ctx.get()), 1);
        EXPECT_EQ(EVP_PKEY_CTX_set_signature_md(ctx.get(), ot::sign::evpDigest(alg)), 1);
        if (EVP_PKEY_is_a(key, "RSA")) {
            EXPECT_EQ(EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING), 1);
        }
        size_t len = 0;
        EXPECT_EQ(EVP_PKEY_sign(ctx.get(), nullptr, &len, hash.data(), hash.size()), 1);
        Bytes sig(len);
        EXPECT_EQ(EVP_PKEY_sign(ctx.get(), sig.data(), &len, hash.data(), hash.size()), 1);
        sig.resize(len);
        return sig;
    };
}

std::string sample() {
    std::string data = "%PDF-1.7\n";
    for (int i = 0; i < 500; ++i) {
        data += "внешняя подпись " + std::to_string(i) + "\n";
    }
    return data;
}

bool opensslVerify(const ot::test::TempDir& dir, const Bytes& cms, std::string_view content,
                   std::string* out) {
    const auto exe = ot::test::opensslExe();
    if (!exe) {
        return true;  // нет openssl — внешняя проверка пропущена (своя CMS_verify — ниже)
    }
    const auto sig = dir.path() / L"внешняя.p7s";
    const auto data = dir.path() / L"данные.bin";
    const auto res = dir.path() / L"out.bin";
    ot::paths::writeFile(sig, std::string_view(reinterpret_cast<const char*>(cms.data()), cms.size()));
    ot::paths::writeFile(data, content);
    const auto r = ot::test::runProcess(
        *exe, {L"cms", L"-verify", L"-cades", L"-binary", L"-inform", L"DER", L"-in", sig.wstring(),
               L"-content", data.wstring(), L"-CAfile", (ot::test::pkiDir() / "root.pem").wstring(),
               L"-purpose", L"smimesign", L"-out", res.wstring()});
    *out = r.output;
    return r.exitCode == 0;
}

bool cmsValid(const Bytes& cms, std::string_view content, std::string* error) {
    const auto store = ot::test::pkiRootStore();
    Ossl<BIO> bio(BIO_new_mem_buf(content.data(), static_cast<int>(content.size())));
    return ot::sign::cmsVerifyDetached(cms, bio.get(), store.get(), error);
}

}  // namespace

TEST(cms_external, signed_attributes_match_openssl_byte_for_byte) {
    OT_FEATURE("sign-08");
    const std::string data = sample();
    for (const char* pfx : {"signer-rsa.pfx", "signer-ec.pfx"}) {
        for (const DigestAlgorithm alg : {DigestAlgorithm::Sha256, DigestAlgorithm::Sha384}) {
            SCOPED_TRACE(std::string(pfx) + (alg == DigestAlgorithm::Sha256 ? " sha256" : " sha384"));
            const SigningKey id = identity(pfx);
            const Bytes digest = digestOf(data, alg);
            CmsBuilder builder(id, alg);
            const ot::sign::CmsInfo reference = ot::sign::parseCms(builder.buildFromDigest(digest));
            const Bytes ours = ot::sign::cadesSignedAttributes(id.cert.get(), alg, digest);
            EXPECT_EQ(ours, reference.signedAttributesDer);
            ASSERT_NE(reference.signedAttribute(ot::sign::oid::kSigningCertificateV2), nullptr);
            EXPECT_EQ(ot::sign::essSigningCertificateV2(id.cert.get(), alg),
                      reference.signedAttribute(ot::sign::oid::kSigningCertificateV2)->value);
        }
    }
}

TEST(cms_external, rsa_pkcs7_path_equals_cms_path) {
    const std::string data = sample();
    const SigningKey id = identity("signer-rsa.pfx");
    const Bytes digest = digestOf(data, DigestAlgorithm::Sha256);
    CmsBuilder reference(id);
    const Bytes expected = reference.buildFromDigest(digest);
    int calls = 0;
    ExternalCmsBuilder external(id.cert.get(), id.chain.get(),
                                opensslSigner(id.key.get(), DigestAlgorithm::Sha256, &calls));
    const Bytes der = external.buildFromDigest(digest);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(der, expected);  // RSA PKCS#1 v1.5 детерминирован — один DER-формат
    EXPECT_EQ(external.der(), der);
    EXPECT_EQ(external.signatureValue(), reference.signatureValue());
    const ot::sign::CmsInfo info = ot::sign::parseCms(der);
    EXPECT_EQ(external.signedAttributes(), info.signedAttributesDer);
    EXPECT_EQ(info.digestAlgorithm, ot::sign::oid::kSha256);
    EXPECT_EQ(info.signatureAlgorithm, ot::sign::oid::kRsaEncryption);
    EXPECT_FALSE(info.signingTime.has_value());
    EXPECT_TRUE(info.detached);
    std::string error;
    EXPECT_TRUE(cmsValid(der, data, &error)) << error;
}

TEST(cms_external, ecdsa_pkcs7_path_verifies) {
    const ot::test::TempDir dir;
    const std::string data = sample();
    const SigningKey id = identity("signer-ec.pfx");
    for (const DigestAlgorithm alg : {DigestAlgorithm::Sha256, DigestAlgorithm::Sha384}) {
        const Bytes digest = digestOf(data, alg);
        ExternalCmsBuilder external(id.cert.get(), id.chain.get(), opensslSigner(id.key.get(), alg), alg);
        const Bytes der = external.buildFromDigest(digest);
        const ot::sign::CmsInfo info = ot::sign::parseCms(der);
        EXPECT_EQ(info.signatureAlgorithm, alg == DigestAlgorithm::Sha256 ? ot::sign::oid::kEcdsaWithSha256
                                                                          : ot::sign::oid::kEcdsaWithSha384);
        EXPECT_EQ(info.messageDigest, digest);
        EXPECT_GE(info.certificates.size(), 2u);
        EXPECT_GE(info.signerCertIndex, 0);
        std::string error;
        EXPECT_TRUE(cmsValid(der, data, &error)) << error;
        std::string output;
        EXPECT_TRUE(opensslVerify(dir, der, data, &output)) << output;
    }
}

TEST(cms_external, timestamp_attribute_keeps_signature) {
    const std::string data = sample();
    const SigningKey id = identity("signer-ec.pfx");
    ExternalCmsBuilder external(id.cert.get(), id.chain.get(),
                                opensslSigner(id.key.get(), DigestAlgorithm::Sha256));
    const Bytes der = external.buildFromDigest(digestOf(data, DigestAlgorithm::Sha256));
    // Токен-заглушка (SEQUENCE): здесь важно лишь место атрибута, проверка токенов — unit.tsa.
    const Bytes token = ot::sign::der::encode(ot::sign::der::kSequence, ot::sign::der::encodeOid("1.2.3.4"));
    const Bytes stamped = ot::sign::cmsAddUnsignedTimestamp(der, token);
    const ot::sign::CmsInfo info = ot::sign::parseCms(stamped);
    EXPECT_EQ(info.timestampToken, token);
    EXPECT_EQ(info.signature, external.signatureValue());
    EXPECT_EQ(info.signedAttributesDer, external.signedAttributes());
    std::string error;
    EXPECT_TRUE(cmsValid(stamped, data, &error)) << error;
}

TEST(cms_external, signer_errors_propagate) {
    const SigningKey id = identity("signer-rsa.pfx");
    const Bytes digest = digestOf("x", DigestAlgorithm::Sha256);
    ExternalCmsBuilder failing(id.cert.get(), nullptr, [](std::span<const std::uint8_t>) -> Bytes {
        throw std::runtime_error("PIN");
    });
    EXPECT_THROW((void)failing.buildFromDigest(digest), std::runtime_error);
    EXPECT_THROW((void)failing.der(), std::logic_error);
    ExternalCmsBuilder empty(id.cert.get(), nullptr, [](std::span<const std::uint8_t>) { return Bytes(); });
    EXPECT_THROW((void)empty.buildFromDigest(digest), ot::sign::OsslError);
    ExternalCmsBuilder ok(id.cert.get(), nullptr, opensslSigner(id.key.get(), DigestAlgorithm::Sha256));
    EXPECT_THROW((void)ok.buildFromDigest(Bytes(20, 0)), std::invalid_argument);
}

TEST(der_writer, lengths_sets_and_oids) {
    namespace der = ot::sign::der;
    EXPECT_EQ(der::encode(0x04, Bytes(3, 0xAA)), (Bytes{0x04, 0x03, 0xAA, 0xAA, 0xAA}));
    const Bytes long200 = der::encode(0x04, Bytes(200, 1));
    EXPECT_EQ(long200[1], 0x81);
    EXPECT_EQ(long200[2], 200);
    const Bytes long300 = der::encode(0x04, Bytes(300, 1));
    EXPECT_EQ(long300[1], 0x82);
    EXPECT_EQ(long300[2], 0x01);
    EXPECT_EQ(long300[3], 0x2C);
    EXPECT_EQ(der::encodeOid("1.2.840.113549.1.9.4"),
              (Bytes{0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x09, 0x04}));
    const Bytes sha = der::encodeOid("2.16.840.1.101.3.4.2.1");
    EXPECT_EQ(der::toOid(std::span<const std::uint8_t>(sha).subspan(2)), "2.16.840.1.101.3.4.2.1");
    EXPECT_THROW((void)der::encodeOid("1"), der::DerError);
    EXPECT_THROW((void)der::encodeOid("1.2.x"), der::DerError);
    // SET OF — по возрастанию кодировок.
    const Bytes set = der::encodeSetOf({Bytes{0x04, 0x01, 0x02}, Bytes{0x02, 0x01, 0x05}, Bytes{0x04, 0x00}});
    EXPECT_EQ(set, (Bytes{0x31, 0x08, 0x02, 0x01, 0x05, 0x04, 0x00, 0x04, 0x01, 0x02}));
}
