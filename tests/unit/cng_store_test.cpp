// unit.cng_store — хранилище сертификатов Windows (план 4.3, п. 1; эталон 3.8 «Диалог подписания» — хранилище
// Windows с фильтром «есть закрытый ключ и Digital Signature», «CNG-путь»; sign-08). Хранилища человека не
// трогаются: только CERT_STORE_PROV_MEMORY и PFXImportCertStore(PKCS12_NO_PERSIST_KEY |
// PKCS12_ALWAYS_CNG_KSP) над тестовой PKI — ключ живёт в памяти процесса. Перечисление пустого хранилища не
// падает; фильтр отсекает сертификаты без ключа и без права подписи; отпечаток SHA-1 совпадает с OpenSSL и
// находится в любом написании (как его копируют из certmgr — с пробелами и невидимым U+200E); список для
// диалога — ссылки на подходящие; импорт удостоверения по команде кладёт сертификат с ключом в целевое
// хранилище; диалог выбора в offscreen выключен и ничего не открывает.
#include <gtest/gtest.h>

#include <openssl/evp.h>
#include <openssl/x509v3.h>

#include <ctime>
#include <string>
#include <vector>

#include "core/sign/cng_store.h"
#include "core/sign/identity.h"
#include "core/sign/pkcs12.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "services/cert_picker.h"
#include "support/pki.h"

namespace {

using ot::sign::Bytes;
using ot::sign::Ossl;
using ot::sign::cng::CertContext;
using ot::sign::cng::CertEntry;
using ot::sign::cng::CertFilter;
using ot::sign::cng::CertStore;
using ot::sign::cng::KeyStorage;

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

CertStore importPki(const char* pfx = "signer-rsa.pfx") {
    return CertStore::importPfx(bytesOf(ot::test::readPki(pfx)), "acrobatya", KeyStorage::Memory);
}

std::string pkiThumbprint(const char* pem) {
    const Ossl<X509> cert = ot::test::pkiCert(pem);
    return ot::sign::certificateThumbprint(cert.get());
}

Bytes certDer(X509* cert) { return ot::sign::ossl::toDer(cert, i2d_X509, "i2d_X509"); }

// Удостоверение с ключом, но без права подписи (KU keyAgreement) — PKCS#12 для фильтра.
ot::sign::SigningKey keyAgreementIdentity() {
    ot::sign::SigningKey out;
    out.key.reset(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"));
    out.cert.reset(X509_new());
    X509* c = out.cert.get();
    X509_set_version(c, X509_VERSION_3);
    ASN1_INTEGER_set(X509_get_serialNumber(c), 77);
    X509_gmtime_adj(X509_getm_notBefore(c), -3600);
    X509_gmtime_adj(X509_getm_notAfter(c), 3600L * 24 * 365);
    X509_NAME* name = X509_get_subject_name(c);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_UTF8,
                               reinterpret_cast<const unsigned char*>("Key agreement only"), -1, -1, 0);
    X509_set_issuer_name(c, name);
    X509_set_pubkey(c, out.key.get());
    X509V3_CTX v3;
    X509V3_set_ctx_nodb(&v3);
    X509V3_set_ctx(&v3, c, c, nullptr, nullptr, 0);
    X509_EXTENSION* ku = X509V3_EXT_conf_nid(nullptr, &v3, NID_key_usage, "critical,keyAgreement");
    X509_add_ext(c, ku, -1);
    X509_EXTENSION_free(ku);
    X509_sign(c, out.key.get(), EVP_sha256());
    out.chain.reset(sk_X509_new_null());
    return out;
}

}  // namespace

TEST(cng_store, empty_memory_store) {
    const CertStore store = CertStore::openMemory();
    ASSERT_TRUE(store);
    EXPECT_TRUE(store.list().empty());
    EXPECT_TRUE(store.list(CertFilter{}).empty());
    EXPECT_FALSE(store.find(pkiThumbprint("signer-rsa.pem")));
    EXPECT_TRUE(ot::sign::cng::filtered(store, CertFilter{}).list().empty());
}

TEST(cng_store, pfx_import_lists_signer_with_key) {
    OT_FEATURE("sign-08");
    const CertStore store = importPki();
    const std::vector<CertEntry> all = store.list();
    ASSERT_GE(all.size(), 1u);
    const std::vector<CertEntry> signing = store.list(CertFilter{});
    ASSERT_EQ(signing.size(), 1u);
    const CertEntry& e = signing.front();
    EXPECT_EQ(e.thumbprint, pkiThumbprint("signer-rsa.pem"));
    EXPECT_TRUE(e.hasPrivateKey);
    EXPECT_TRUE(e.signingUsage);
    EXPECT_FALSE(e.expired);
    EXPECT_FALSE(e.smartCard);
    EXPECT_EQ(e.summary.commonName, "АкроБатя тест");
    EXPECT_EQ(e.summary.notAfter, "2036-01-01T00:00:00Z");
    const Ossl<X509> pem = ot::test::pkiCert("signer-rsa.pem");
    EXPECT_EQ(e.context.der(), certDer(pem.get()));
    EXPECT_EQ(certDer(e.context.x509().get()), certDer(pem.get()));
    // Остальные сертификаты файла (ЦС) — без ключа и без права подписи документов.
    for (const CertEntry& other : all) {
        if (other.thumbprint != e.thumbprint) {
            EXPECT_FALSE(other.hasPrivateKey) << other.summary.subject;
            EXPECT_TRUE(other.summary.ca) << other.summary.subject;
        }
    }
}

TEST(cng_store, filter_needs_key_and_signing_usage) {
    CertStore mem = CertStore::openMemory();
    // Тот же сертификат подписанта без ключа (DER).
    const Ossl<X509> pem = ot::test::pkiCert("signer-rsa.pem");
    const CertContext noKey = mem.addDer(certDer(pem.get()));
    ASSERT_TRUE(noKey);
    EXPECT_FALSE(noKey.hasPrivateKey());
    EXPECT_EQ(mem.list().size(), 1u);
    EXPECT_TRUE(mem.list(CertFilter{}).empty());
    CertFilter anyKey;
    anyKey.requirePrivateKey = false;
    EXPECT_EQ(mem.list(anyKey).size(), 1u);

    // С ключом, но без digitalSignature/nonRepudiation — не для подписи.
    const ot::sign::SigningKey ka = keyAgreementIdentity();
    CertStore kaStore = CertStore::importPfx(ot::sign::exportPkcs12(ka, "x"), "x", KeyStorage::Memory);
    ASSERT_EQ(kaStore.list().size(), 1u);
    EXPECT_TRUE(kaStore.list().front().hasPrivateKey);
    EXPECT_FALSE(kaStore.list().front().signingUsage);
    EXPECT_TRUE(kaStore.list(CertFilter{}).empty());
    CertFilter usageAny;
    usageAny.requireSigningUsage = false;
    EXPECT_EQ(kaStore.list(usageAny).size(), 1u);

    // Истёкшие остаются с пометкой, пока фильтр их не исключит.
    CertEntry e = ot::sign::cng::describe(importPki().list(CertFilter{}).front().context,
                                          /*2037-01-01*/ 2114380800);
    EXPECT_TRUE(e.expired);
    EXPECT_TRUE(ot::sign::cng::matches(e, CertFilter{}));
    CertFilter fresh;
    fresh.excludeExpired = true;
    EXPECT_FALSE(ot::sign::cng::matches(e, fresh));
}

TEST(cng_store, find_by_thumbprint_any_spelling) {
    const CertStore store = importPki();
    const std::string tp = pkiThumbprint("signer-rsa.pem");
    ASSERT_EQ(tp.size(), 40u);
    EXPECT_TRUE(store.find(tp));
    // Как в certmgr: строчные пары через пробел с невидимым U+200E в начале.
    std::string spaced = "\xE2\x80\x8E";
    for (std::size_t i = 0; i < tp.size(); i += 2) {
        spaced += static_cast<char>(std::tolower(static_cast<unsigned char>(tp[i])));
        spaced += static_cast<char>(std::tolower(static_cast<unsigned char>(tp[i + 1])));
        spaced += ' ';
    }
    EXPECT_EQ(ot::sign::cng::normalizeThumbprint(spaced), tp);
    const CertContext found = store.find(spaced);
    ASSERT_TRUE(found);
    EXPECT_EQ(found.thumbprint(), tp);
    EXPECT_TRUE(found.hasPrivateKey());
    EXPECT_FALSE(store.find(std::string(40, '0')));
    EXPECT_FALSE(store.find("не отпечаток"));
    EXPECT_TRUE(ot::sign::cng::normalizeThumbprint("ABC").empty());
}

TEST(cng_store, filtered_store_for_dialog) {
    const CertStore source = importPki();
    const CertStore list = ot::sign::cng::filtered(source, CertFilter{});
    const std::vector<CertEntry> entries = list.list();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().thumbprint, pkiThumbprint("signer-rsa.pem"));
    EXPECT_TRUE(entries.front().hasPrivateKey);  // ссылка — свойства и ключ исходного
}

TEST(cng_store, import_identity_by_command) {
    ot::sign::SelfSignedOptions o;
    o.commonName = "Импорт по команде";
    o.algorithm = ot::sign::KeyAlgorithm::EcdsaP256;
    const ot::sign::SigningKey key = ot::sign::createSelfSigned(o);
    CertStore target = CertStore::openMemory();
    const CertEntry e = ot::sign::cng::importIdentity(key, target, KeyStorage::Memory);
    EXPECT_EQ(e.thumbprint, ot::sign::certificateThumbprint(key.cert.get()));
    EXPECT_EQ(e.summary.commonName, "Импорт по команде");
    EXPECT_TRUE(e.hasPrivateKey);
    const std::vector<CertEntry> listed = target.list(CertFilter{});
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed.front().thumbprint, e.thumbprint);
}

TEST(cng_store, bad_pfx_password) {
    try {
        (void)CertStore::importPfx(bytesOf(ot::test::readPki("signer-rsa.pfx")), "wrong", KeyStorage::Memory);
        FAIL() << "wrong password accepted";
    } catch (const ot::sign::cng::StoreError& e) {
        EXPECT_NE(e.code(), 0u);
    }
}

TEST(cert_picker, disabled_without_screen) {
    // Тесты — без экрана (QCoreApplication или offscreen): диалог Windows не открывается.
    EXPECT_FALSE(ot::CertPicker::available());
    EXPECT_FALSE(ot::CertPicker::pick(0, QStringLiteral("t"), QStringLiteral("p")).has_value());
}
