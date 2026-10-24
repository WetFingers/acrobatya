// unit.cng_signer — подпись ключом хранилища Windows через CNG (план 4.3, п. 2; эталон 3.8 «CNG-путь»; 11
// риск 2; sign-08, sign-06 — ключ на устройстве). Без UI и без касания хранилищ человека: PFXImportCertStore(
// PKCS12_NO_PERSIST_KEY | PKCS12_ALWAYS_CNG_KSP) над тестовой PKI, silent. RSA PKCS#1 v1.5 через
// NCryptSignHash совпадает с OpenSSL на том же ключе, PSS проверяется открытым ключом сертификата, ECDSA
// P-256 — IEEE P1363 → DER и проверка ECDSA; ключ старого CSP переводится в CNG; сертификат без ключа —
// честная CngError::NoPrivateKey, чужой отпечаток — NotFound; CAdES-B через CNG проходит CMS_verify с
// цепочкой до корня PKI и внешний openssl cms -verify -cades, а его signed-атрибуты и (для RSA) весь DER
// равны OpenSSL-пути (.pfx) байт в байт.
#include <gtest/gtest.h>

#include <windows.h>
#include <wincrypt.h>

#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include <string>

#include "core/sign/cms.h"
#include "core/sign/cng_signer.h"
#include "core/sign/cng_store.h"
#include "core/sign/identity.h"
#include "core/sign/pkcs12.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/test_pdf.h"

namespace {

using ot::sign::Bytes;
using ot::sign::CngError;
using ot::sign::CngIdentity;
using ot::sign::CngKey;
using ot::sign::CngOptions;
using ot::sign::CngPadding;
using ot::sign::DigestAlgorithm;
using ot::sign::Ossl;
using ot::sign::cng::CertStore;
using ot::sign::cng::KeyStorage;

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

CngOptions silent() {
    CngOptions o;
    o.silent = true;
    return o;
}

CertStore importPki(const char* pfx) {
    return CertStore::importPfx(bytesOf(ot::test::readPki(pfx)), "acrobatya", KeyStorage::Memory);
}

std::string thumbprintOf(const char* pem) {
    const Ossl<X509> cert = ot::test::pkiCert(pem);
    return ot::sign::certificateThumbprint(cert.get());
}

ot::sign::SigningKey pfxIdentity(const char* pfx) {
    return ot::sign::loadPkcs12(bytesOf(ot::test::readPki(pfx)), "acrobatya");
}

Bytes sha256(std::string_view s) { return ot::sign::ossl::sha256(s); }

bool verifyWith(X509* cert, const Bytes& hash, const Bytes& sig, int padding) {
    Ossl<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(X509_get0_pubkey(cert), nullptr));
    EVP_PKEY_verify_init(ctx.get());
    EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256());
    if (padding != 0) {
        EVP_PKEY_CTX_set_rsa_padding(ctx.get(), padding);
        if (padding == RSA_PKCS1_PSS_PADDING) {
            EVP_PKEY_CTX_set_rsa_pss_saltlen(ctx.get(), RSA_PSS_SALTLEN_DIGEST);
        }
    }
    return EVP_PKEY_verify(ctx.get(), sig.data(), sig.size(), hash.data(), hash.size()) == 1;
}

std::string sample() {
    std::string data = "%PDF-1.7\n";
    for (int i = 0; i < 300; ++i) {
        data += "подпись ключом Windows " + std::to_string(i) + "\n";
    }
    return data;
}

}  // namespace

TEST(cng_signer, rsa_pkcs1_equals_openssl) {
    OT_FEATURE("sign-08");
    OT_FEATURE("sign-06");
    const CertStore store = importPki("signer-rsa.pfx");
    const CngIdentity id = CngIdentity::open(store, thumbprintOf("signer-rsa.pem"), silent());
    EXPECT_EQ(id.key->kind(), ot::sign::CngKeyKind::Rsa);
    const Bytes hash = sha256("АкроБатя");
    const Bytes sig = id.key->signHash(hash, DigestAlgorithm::Sha256);
    EXPECT_TRUE(verifyWith(id.cert.get(), hash, sig, RSA_PKCS1_PADDING));
    // Тот же ключ в OpenSSL — та же подпись (PKCS#1 v1.5 детерминирован).
    const ot::sign::SigningKey pfx = pfxIdentity("signer-rsa.pfx");
    Ossl<EVP_PKEY_CTX> ctx(EVP_PKEY_CTX_new(pfx.key.get(), nullptr));
    ASSERT_EQ(EVP_PKEY_sign_init(ctx.get()), 1);
    EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256());
    EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING);
    size_t len = 0;
    EVP_PKEY_sign(ctx.get(), nullptr, &len, hash.data(), hash.size());
    Bytes expected(len);
    ASSERT_EQ(EVP_PKEY_sign(ctx.get(), expected.data(), &len, hash.data(), hash.size()), 1);
    expected.resize(len);
    EXPECT_EQ(sig, expected);
}

TEST(cng_signer, rsa_pss_verifies) {
    const CertStore store = importPki("signer-rsa.pfx");
    const CngIdentity id = CngIdentity::open(store, thumbprintOf("signer-rsa.pem"), silent());
    const Bytes hash = sha256("PSS");
    const Bytes sig = id.key->signHash(hash, DigestAlgorithm::Sha256, CngPadding::Pss);
    EXPECT_TRUE(verifyWith(id.cert.get(), hash, sig, RSA_PKCS1_PSS_PADDING));
    EXPECT_FALSE(verifyWith(id.cert.get(), hash, sig, RSA_PKCS1_PADDING));
    EXPECT_THROW((void)id.key->signHash(Bytes(20, 1), DigestAlgorithm::Sha256), std::invalid_argument);
}

TEST(cng_signer, ecdsa_p1363_to_der_verifies) {
    const CertStore store = importPki("signer-ec.pfx");
    const CngIdentity id = CngIdentity::open(store, thumbprintOf("signer-ec.pem"), silent());
    EXPECT_EQ(id.key->kind(), ot::sign::CngKeyKind::Ec);
    const Bytes hash = sha256("ECDSA");
    const Bytes sig = id.key->signHash(hash, DigestAlgorithm::Sha256);
    ASSERT_GT(sig.size(), 8u);
    EXPECT_EQ(sig[0], 0x30);  // DER ECDSA-Sig-Value, не r‖s
    const unsigned char* p = sig.data();
    ECDSA_SIG* parsed = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(sig.size()));
    ASSERT_NE(parsed, nullptr);
    ECDSA_SIG_free(parsed);
    EXPECT_TRUE(verifyWith(id.cert.get(), hash, sig, 0));
}

TEST(cng_signer, p1363_conversion) {
    // r со старшим битом — ведущий 0 в INTEGER; s с ведущими нулями — без них.
    Bytes rs(64, 0);
    rs[0] = 0x80;
    rs[31] = 0x01;
    rs[63] = 0x05;
    const Bytes der = ot::sign::ecdsaP1363ToDer(rs);
    Bytes expected = {0x30, 0x26, 0x02, 0x21, 0x00, 0x80};
    expected.insert(expected.end(), 30, 0x00);
    expected.insert(expected.end(), {0x01, 0x02, 0x01, 0x05});
    EXPECT_EQ(der, expected);
    EXPECT_THROW((void)ot::sign::ecdsaP1363ToDer(Bytes(3, 1)), std::invalid_argument);
    EXPECT_THROW((void)ot::sign::ecdsaP1363ToDer(Bytes()), std::invalid_argument);
}

TEST(cng_signer, no_private_key_is_honest_error) {
    CertStore mem = CertStore::openMemory();
    const Ossl<X509> pem = ot::test::pkiCert("signer-rsa.pem");
    const ot::sign::cng::CertContext noKey = mem.addDer(ot::sign::toDerCertificate(pem.get()));
    try {
        (void)CngKey::acquire(noKey, silent());
        FAIL() << "key acquired without a private key";
    } catch (const CngError& e) {
        EXPECT_EQ(e.status(), CngError::NoPrivateKey) << e.what();
    }
    try {
        (void)CngIdentity::open(mem, thumbprintOf("signer-rsa.pem"), silent());
        FAIL() << "identity opened without a private key";
    } catch (const CngError& e) {
        EXPECT_EQ(e.status(), CngError::NoPrivateKey) << e.what();
    }
    try {
        (void)CngIdentity::open(mem, std::string(40, 'A'), silent());
        FAIL() << "unknown thumbprint opened";
    } catch (const CngError& e) {
        EXPECT_EQ(e.status(), CngError::NotFound);
    }
}

TEST(cng_signer, legacy_csp_key_signs_through_cryptoapi) {
    // Ключ старого CSP «Enhanced RSA and AES» (PROV_RSA_AES), который не переводится в CNG (эфемерный
    // контейнер): подпись — CryptoAPI CryptSignHash (младший байт вперёд → разворот), проверяется OpenSSL.
    HCRYPTPROV prov = 0;
    ASSERT_TRUE(
        CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT))
        << GetLastError();
    const Ossl<EVP_PKEY> pkey = ot::test::pkiKey("signer-rsa.key");
    Ossl<BIO> blobBio(BIO_new(BIO_s_mem()));
    ASSERT_GT(i2b_PrivateKey_bio(blobBio.get(), pkey.get()), 0);
    const Bytes blob = ot::sign::ossl::bioBytes(blobBio.get());
    HCRYPTKEY imported = 0;
    ASSERT_TRUE(CryptImportKey(prov, blob.data(), static_cast<DWORD>(blob.size()), 0, 0, &imported))
        << GetLastError();
    CryptDestroyKey(imported);
    CertStore mem = CertStore::openMemory();
    const Ossl<X509> pem = ot::test::pkiCert("signer-rsa.pem");
    const ot::sign::cng::CertContext cert = mem.addDer(ot::sign::toDerCertificate(pem.get()));
    CERT_KEY_CONTEXT kc{sizeof(CERT_KEY_CONTEXT), {prov}, AT_KEYEXCHANGE};  // контекст освободит prov
    ASSERT_TRUE(CertSetCertificateContextProperty(static_cast<PCCERT_CONTEXT>(cert.get()),
                                                  CERT_KEY_CONTEXT_PROP_ID, 0, &kc))
        << GetLastError();
    EXPECT_TRUE(cert.hasPrivateKey());
    const CngKey key = CngKey::acquire(cert, silent());
    const Bytes digest = sha256("CSP");
    const Bytes sig = key.signHash(digest, DigestAlgorithm::Sha256);
    EXPECT_TRUE(verifyWith(pem.get(), digest, sig, RSA_PKCS1_PADDING));
    try {
        (void)key.signHash(digest, DigestAlgorithm::Sha256, CngPadding::Pss);
        FAIL() << "PSS through CryptoAPI";
    } catch (const CngError& e) {
        EXPECT_EQ(e.status(), CngError::LegacyProvider);
    }
}

TEST(cng_signer, legacy_full_csp_without_container_is_honest) {
    // Без PKCS12_ALWAYS_CNG_KSP ключ из .pfx уходит в «Enhanced Cryptographic Provider» (PROV_RSA_FULL, без
    // SHA-2); в памяти у него нет контейнера, чтобы открыть его через PROV_RSA_AES, — честный LegacyProvider
    // (или подпись, если Windows всё же отдаст ключ CNG).
    const std::string pfx = ot::test::readPki("signer-rsa.pfx");
    CRYPT_DATA_BLOB blob{static_cast<DWORD>(pfx.size()),
                         reinterpret_cast<BYTE*>(const_cast<char*>(pfx.data()))};
    HCERTSTORE raw = PFXImportCertStore(&blob, L"acrobatya", CRYPT_USER_KEYSET | PKCS12_NO_PERSIST_KEY);
    ASSERT_NE(raw, nullptr) << GetLastError();
    const std::string tp = thumbprintOf("signer-rsa.pem");
    BYTE hash[20];
    for (int i = 0; i < 20; ++i) {
        hash[i] = static_cast<BYTE>(std::stoi(tp.substr(static_cast<std::size_t>(i) * 2, 2), nullptr, 16));
    }
    CRYPT_HASH_BLOB hb{sizeof hash, hash};
    const ot::sign::cng::CertContext cert(
        CertFindCertificateInStore(raw, X509_ASN_ENCODING, 0, CERT_FIND_SHA1_HASH, &hb, nullptr));
    CertCloseStore(raw, 0);
    ASSERT_TRUE(cert);
    const CngKey key = CngKey::acquire(cert, silent());
    const Bytes digest = sha256("CSP");
    try {
        const Bytes sig = key.signHash(digest, DigestAlgorithm::Sha256);
        const Ossl<X509> x = cert.x509();
        EXPECT_TRUE(verifyWith(x.get(), digest, sig, RSA_PKCS1_PADDING));
    } catch (const CngError& e) {
        EXPECT_EQ(e.status(), CngError::LegacyProvider) << e.what();
    }
}

TEST(cng_signer, cms_via_cng_matches_openssl_path) {
    const ot::test::TempDir dir;
    const std::string data = sample();
    const Bytes digest = sha256(data);
    for (const char* name : {"signer-rsa", "signer-ec"}) {
        SCOPED_TRACE(name);
        const std::string pfxName = std::string(name) + ".pfx";
        const std::string pemName = std::string(name) + ".pem";
        const CertStore store = importPki(pfxName.c_str());
        const CngIdentity id = CngIdentity::open(store, thumbprintOf(pemName.c_str()), silent());
        const Bytes der = ot::sign::cngCmsFromDigest(id, digest);

        // Своя проверка с цепочкой до корня PKI.
        const auto root = ot::test::pkiRootStore();
        Ossl<BIO> bio(BIO_new_mem_buf(data.data(), static_cast<int>(data.size())));
        std::string error;
        EXPECT_TRUE(ot::sign::cmsVerifyDetached(der, bio.get(), root.get(), &error)) << error;

        // OpenSSL-путь (.pfx): атрибуты байт в байт; RSA — весь DER.
        const ot::sign::SigningKey pfx = pfxIdentity(pfxName.c_str());
        ot::sign::CmsBuilder reference(pfx);
        const Bytes expected = reference.buildFromDigest(digest);
        const ot::sign::CmsInfo ours = ot::sign::parseCms(der);
        const ot::sign::CmsInfo theirs = ot::sign::parseCms(expected);
        EXPECT_EQ(ours.signedAttributesDer, theirs.signedAttributesDer);
        EXPECT_EQ(ours.certificates.size(), theirs.certificates.size());
        EXPECT_EQ(ours.signatureAlgorithm, theirs.signatureAlgorithm);
        if (std::string(name) == "signer-rsa") {
            EXPECT_EQ(der, expected);
        }

        // Внешняя проверка: openssl cms -verify -cades.
        const auto exe = ot::test::opensslExe();
        if (!exe) {
            continue;
        }
        const auto sig = dir.path() / (ot::paths::widen(name) + L"-cng.p7s");
        const auto content = dir.path() / L"данные.bin";
        const auto out = dir.path() / L"out.bin";
        ot::paths::writeFile(sig, std::string_view(reinterpret_cast<const char*>(der.data()), der.size()));
        ot::paths::writeFile(content, data);
        const auto r = ot::test::runProcess(
            *exe, {L"cms", L"-verify", L"-cades", L"-binary", L"-inform", L"DER", L"-in", sig.wstring(),
                   L"-content", content.wstring(), L"-CAfile", (ot::test::pkiDir() / "root.pem").wstring(),
                   L"-purpose", L"smimesign", L"-out", out.wstring()});
        EXPECT_EQ(r.exitCode, 0) << r.output;
    }
}
