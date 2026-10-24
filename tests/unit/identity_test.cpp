// unit.identity — удостоверения (план 4.2, п. 4; эталон 3.8 «Диалог подписания» — удостоверения, «Доверие» —
// «Удостоверения»; 4.7 — ids\; вопрос 12.5; sign-05, sign-06, sign-10, sign-11): самоподписанное RSA-3072 и
// ECDSA P-256 на 5 лет (X.509 v3, basicConstraints CA:false, KU digitalSignature + nonRepudiation) в PKCS#12
// AES-256-CBC с MAC SHA-256 — читается loadPkcs12 (и openssl pkcs12 -info), импортируется в Windows
// (PFXImportCertStore без сохранения ключа) и подписывает PDF; IdentityStore над state.sqlite — добавить .pfx
// (копия в ids\), создать самоподписанное, по умолчанию, пароль: аргумент → кэш с тайм-аутом → DPAPI по
// флажку (здесь — защитник-заглушка), смена пароля (перезапись .pfx), удаление; экспорт сертификата .cer,
// .p7c, .fdf с /Certs (и обратное чтение FDF).
#include <gtest/gtest.h>

#include <openssl/pkcs12.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>
#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/sign/identity.h"
#include "core/sign/pkcs12.h"
#include "core/sign/signer.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::sign::Bytes;
using ot::sign::IdentityError;
using ot::sign::IdentityKind;
using ot::sign::IdentityRecord;
using ot::sign::IdentityStatus;
using ot::sign::IdentityStore;
using ot::sign::KeyAlgorithm;
using ot::sign::SelfSignedOptions;
using ot::sign::SigningKey;

namespace {

Bytes bytesOf(const std::string& s) { return Bytes(s.begin(), s.end()); }

// Защитник-заглушка вместо DPAPI (DPAPI — dpapi_test): «шифр» — XOR с меткой.
class FakeProtector : public ot::sign::SecretProtector {
public:
    Bytes protect(std::span<const std::uint8_t> plain) override {
        Bytes out{'F', 'K'};
        for (std::uint8_t b : plain) {
            out.push_back(static_cast<std::uint8_t>(b ^ 0x5A));
        }
        ++protects;
        return out;
    }
    std::optional<Bytes> unprotect(std::span<const std::uint8_t> secret) override {
        if (secret.size() < 2 || secret[0] != 'F' || secret[1] != 'K') {
            return std::nullopt;
        }
        Bytes out;
        for (std::size_t i = 2; i < secret.size(); ++i) {
            out.push_back(static_cast<std::uint8_t>(secret[i] ^ 0x5A));
        }
        return out;
    }
    int protects = 0;
};

struct Db {
    sqlite3* db = nullptr;
    Db() {
        sqlite3_open(":memory:", &db);
        IdentityStore::createSchema(db);
    }
    ~Db() { sqlite3_close(db); }
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
};

SelfSignedOptions options(KeyAlgorithm algorithm) {
    SelfSignedOptions o;
    o.commonName = "Иван Петров";
    o.organization = "Семья";
    o.email = "ivan@example.test";
    o.country = "RU";
    o.algorithm = algorithm;
    return o;
}

}  // namespace

TEST(identity, self_signed_rsa_and_ecdsa_certificates) {
    OT_FEATURE("sign-05");
    for (const KeyAlgorithm algorithm : {KeyAlgorithm::Rsa3072, KeyAlgorithm::EcdsaP256}) {
        const SigningKey key = ot::sign::createSelfSigned(options(algorithm));
        ASSERT_TRUE(key.key && key.cert);
        X509* cert = key.cert.get();
        EXPECT_EQ(X509_get_version(cert), X509_VERSION_3);
        const ot::sign::CertSummary s = ot::sign::summarize(cert);
        EXPECT_EQ(s.commonName, "Иван Петров");
        EXPECT_EQ(s.subject, s.issuer) << "самоподписанный";
        EXPECT_FALSE(s.ca);
        if (algorithm == KeyAlgorithm::Rsa3072) {
            EXPECT_EQ(s.keyAlgorithm, "RSA");
            EXPECT_EQ(s.keyBits, 3072);
        } else {
            EXPECT_EQ(s.keyAlgorithm, "EC");
            EXPECT_EQ(s.keyBits, 256);
        }
        // 5 лет: от 1825 до 1827 дней.
        int days = 0;
        int secs = 0;
        ASSERT_EQ(ASN1_TIME_diff(&days, &secs, X509_get0_notBefore(cert), X509_get0_notAfter(cert)), 1);
        EXPECT_GE(days, 1825);
        EXPECT_LE(days, 1827);
        // basicConstraints CA:false — критическое; KU — подпись и неотрекаемость.
        int crit = -1;
        auto* bc =
            static_cast<BASIC_CONSTRAINTS*>(X509_get_ext_d2i(cert, NID_basic_constraints, &crit, nullptr));
        ASSERT_NE(bc, nullptr);
        EXPECT_EQ(bc->ca, 0);
        EXPECT_EQ(crit, 1);
        BASIC_CONSTRAINTS_free(bc);
        const uint32_t ku = X509_get_key_usage(cert);
        EXPECT_NE(ku & KU_DIGITAL_SIGNATURE, 0u);
        EXPECT_NE(ku & KU_NON_REPUDIATION, 0u);
        EXPECT_EQ(X509_verify(cert, X509_get0_pubkey(cert)), 1);
    }
}

TEST(identity, pkcs12_aes_sha256_roundtrip_and_windows_import) {
    OT_FEATURE("sign-06");
    const SigningKey key = ot::sign::createSelfSigned(options(KeyAlgorithm::Rsa3072));
    const Bytes pfx = ot::sign::exportPkcs12(key, "пароль-1");
    // OpenSSL: MAC SHA-256, читается нашим loadPkcs12.
    const unsigned char* p = pfx.data();
    ot::sign::Ossl<PKCS12> p12(d2i_PKCS12(nullptr, &p, static_cast<long>(pfx.size())));
    ASSERT_TRUE(p12);
    const X509_ALGOR* macAlg = nullptr;
    PKCS12_get0_mac(nullptr, &macAlg, nullptr, nullptr, p12.get());
    ASSERT_NE(macAlg, nullptr);
    const ASN1_OBJECT* macOid = nullptr;
    X509_ALGOR_get0(&macOid, nullptr, nullptr, macAlg);
    EXPECT_EQ(OBJ_obj2nid(macOid), NID_sha256);
    const SigningKey back = ot::sign::loadPkcs12(pfx, "пароль-1");
    EXPECT_EQ(X509_cmp(back.cert.get(), key.cert.get()), 0);
    EXPECT_EQ(back.friendlyName, "Иван Петров");
    try {
        (void)ot::sign::loadPkcs12(pfx, "не тот");
        ADD_FAILURE() << "неверный пароль";
    } catch (const ot::sign::Pkcs12Error& e) {
        EXPECT_EQ(e.status(), ot::sign::Pkcs12Status::BadPassword);
    }
    // Внешний openssl: PBES2 с AES-256-CBC для ключа и сертификатов.
    if (const auto openssl = ot::test::opensslExe()) {
        ot::test::TempDir dir;
        const fs::path file = dir.path() / L"self.pfx";
        ot::paths::writeFile(file, std::string(pfx.begin(), pfx.end()));
        const ot::test::ProcessResult r = ot::test::runProcess(
            *openssl, {L"pkcs12", L"-info", L"-noout", L"-in", file.wstring(), L"-passin", L"pass:пароль-1"});
        EXPECT_EQ(r.exitCode, 0) << r.output;
        EXPECT_NE(r.output.find("AES-256-CBC"), std::string::npos) << r.output;
        EXPECT_NE(r.output.find("sha256"), std::string::npos) << r.output;
    }
    // Windows: PFXImportCertStore без сохранения ключа.
    const ot::sign::WindowsImport imported = ot::sign::importToWindows(pfx, "пароль-1", false);
    EXPECT_TRUE(imported.ok) << imported.error;
    EXPECT_TRUE(imported.hasKey);
    EXPECT_EQ(imported.thumbprint, ot::sign::certificateThumbprint(key.cert.get()));
    EXPECT_FALSE(ot::sign::importToWindows(pfx, "не тот", false).ok);
}

TEST(identity, self_signed_identity_signs_pdf) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"самоподписанная.pdf");
    auto identity =
        std::make_shared<SigningKey>(ot::sign::createSelfSigned(options(KeyAlgorithm::EcdsaP256)));
    ot::sign::SignRequest r;
    r.identity = identity;
    r.placement.rect = ot::RectF{72, 400, 312, 472};
    {
        auto doc = ot::Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, r);
    }
    const ot::test::DocView view = ot::test::readSignatures(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    EXPECT_EQ(view.sigs[0].name, "Иван Петров");
    auto bio = ot::sign::ossl::openRangeBio(path, view.sigs[0].ranges);
    std::string error;
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(view.sigs[0].contents, bio.get(), nullptr, &error)) << error;
}

TEST(identity, store_add_default_password_cache_and_remove) {
    OT_FEATURE("sign-10");
    ot::test::TempDir dir;
    Db db;
    FakeProtector protector;
    const fs::path ids = dir.path() / L"ids";
    IdentityStore store(db.db, ids, &protector);
    auto now = std::chrono::steady_clock::time_point{};
    store.setClock([&] { return now; });

    // Чужой .pfx — копия в ids\, пароль не запомнен.
    const fs::path source = dir.path() / L"внешний.pfx";
    ot::paths::writeFile(source, ot::test::readPki("signer-rsa.pfx"));
    const IdentityRecord added = store.addPkcs12(source, "acrobatya", false);
    EXPECT_EQ(added.kind, IdentityKind::Pkcs12File);
    EXPECT_EQ(added.name, "АкроБатя тест");
    EXPECT_EQ(added.file.parent_path(), ids);
    EXPECT_TRUE(fs::exists(added.file));
    EXPECT_TRUE(added.isDefault) << "первое — по умолчанию";
    EXPECT_FALSE(added.rememberPassword);
    try {
        (void)store.addPkcs12(source, "acrobatya", false);
        ADD_FAILURE() << "то же удостоверение второй раз";
    } catch (const IdentityError& e) {
        EXPECT_EQ(e.status(), IdentityStatus::Exists);
    }
    try {
        (void)store.addPkcs12(source, "не тот", false);
        ADD_FAILURE() << "неверный пароль";
    } catch (const IdentityError& e) {
        EXPECT_EQ(e.status(), IdentityStatus::BadPassword);
    }

    // Самоподписанное с запомненным паролем (DPAPI по флажку — вопрос 12.5).
    const IdentityRecord own = store.createSelfSigned(options(KeyAlgorithm::EcdsaP256), "секрет", true);
    EXPECT_EQ(own.name, "Иван Петров");
    EXPECT_TRUE(own.rememberPassword);
    EXPECT_FALSE(own.isDefault);
    EXPECT_EQ(protector.protects, 1);
    EXPECT_TRUE(fs::exists(own.file));
    ASSERT_EQ(store.list().size(), 2u);

    EXPECT_TRUE(store.setDefault(own.id));
    ASSERT_TRUE(store.defaultIdentity().has_value());
    EXPECT_EQ(store.defaultIdentity()->id, own.id);
    EXPECT_FALSE(store.find(added.id)->isDefault);

    // Пароль: запомненный — без вопроса; незапомненный — нужен, затем кэш до тайм-аута.
    EXPECT_TRUE(store.open(own.id).key);
    try {
        (void)store.open(added.id);
        ADD_FAILURE() << "пароль не запомнен";
    } catch (const IdentityError& e) {
        EXPECT_EQ(e.status(), IdentityStatus::NeedPassword);
    }
    store.setPasswordTimeout(added.id, 120);
    EXPECT_TRUE(store.open(added.id, std::string("acrobatya")).key);
    now += std::chrono::seconds(60);
    EXPECT_TRUE(store.open(added.id).key) << "в кэше";
    now += std::chrono::seconds(61);
    EXPECT_THROW((void)store.open(added.id), IdentityError) << "тайм-аут прошёл";
    store.setPasswordTimeout(added.id, 0);  // «спрашивать всегда»
    EXPECT_TRUE(store.open(added.id, std::string("acrobatya")).key);
    EXPECT_THROW((void)store.open(added.id), IdentityError);
    try {
        (void)store.open(added.id, std::string("не тот"));
        ADD_FAILURE() << "неверный пароль";
    } catch (const IdentityError& e) {
        EXPECT_EQ(e.status(), IdentityStatus::BadPassword);
    }

    // Записи переживают новый объект над той же базой.
    IdentityStore again(db.db, ids, &protector);
    ASSERT_EQ(again.list().size(), 2u);
    EXPECT_EQ(again.defaultIdentity()->id, own.id);
    EXPECT_TRUE(again.open(own.id).key) << "запомненный пароль — из базы";

    // Удалить: по умолчанию переходит к оставшемуся, файл в ids\ — по желанию.
    EXPECT_TRUE(store.remove(own.id, true));
    EXPECT_FALSE(fs::exists(own.file));
    EXPECT_EQ(store.list().size(), 1u);
    EXPECT_EQ(store.defaultIdentity()->id, added.id);
    EXPECT_FALSE(store.remove("нет-такого", true));
}

TEST(identity, change_password_rewrites_pfx) {
    ot::test::TempDir dir;
    Db db;
    FakeProtector protector;
    IdentityStore store(db.db, dir.path() / L"ids", &protector);
    const IdentityRecord rec = store.createSelfSigned(options(KeyAlgorithm::Rsa3072), "старый", true);
    const std::string before = ot::paths::readFile(rec.file);
    try {
        store.changePassword(rec.id, "не тот", "новый");
        ADD_FAILURE() << "старый пароль неверен";
    } catch (const IdentityError& e) {
        EXPECT_EQ(e.status(), IdentityStatus::BadPassword);
    }
    store.changePassword(rec.id, "старый", "новый");
    const std::string after = ot::paths::readFile(rec.file);
    EXPECT_NE(after, before);
    EXPECT_THROW((void)ot::sign::loadPkcs12(bytesOf(after), "старый"), ot::sign::Pkcs12Error);
    EXPECT_TRUE(ot::sign::loadPkcs12(bytesOf(after), "новый").key);
    EXPECT_TRUE(store.open(rec.id).key) << "запомненный пароль обновлён";
}

TEST(identity, export_certificate_cer_p7c_fdf) {
    OT_FEATURE("sign-11");
    ot::test::TempDir dir;
    Db db;
    FakeProtector protector;
    IdentityStore store(db.db, dir.path() / L"ids", &protector);
    const fs::path source = dir.path() / L"rsa.pfx";
    ot::paths::writeFile(source, ot::test::readPki("signer-rsa.pfx"));
    const IdentityRecord rec = store.addPkcs12(source, "acrobatya", true);
    const SigningKey key = store.open(rec.id);
    const Bytes der = ot::sign::toDerCertificate(key.cert.get());

    const fs::path cer = dir.path() / L"сертификат.cer";
    store.exportCertificate(rec.id, cer, ot::sign::CertFormat::Cer);
    EXPECT_EQ(bytesOf(ot::paths::readFile(cer)), der);

    const fs::path p7c = dir.path() / L"сертификат.p7c";
    store.exportCertificate(rec.id, p7c, ot::sign::CertFormat::P7c);
    const std::string p7 = ot::paths::readFile(p7c);
    const unsigned char* p = reinterpret_cast<const unsigned char*>(p7.data());
    ot::sign::Ossl<PKCS7> pkcs7(d2i_PKCS7(nullptr, &p, static_cast<long>(p7.size())));
    ASSERT_TRUE(pkcs7);
    ASSERT_EQ(OBJ_obj2nid(pkcs7->type), NID_pkcs7_signed);
    STACK_OF(X509)* certs = pkcs7->d.sign->cert;
    ASSERT_GE(sk_X509_num(certs), 1);
    EXPECT_EQ(X509_cmp(sk_X509_value(certs, 0), key.cert.get()), 0);

    const fs::path fdf = dir.path() / L"сертификат.fdf";
    store.exportCertificate(rec.id, fdf, ot::sign::CertFormat::Fdf);
    const std::string text = ot::paths::readFile(fdf);
    EXPECT_EQ(text.rfind("%FDF-1.2", 0), 0u);
    EXPECT_NE(text.find("/Certs"), std::string::npos);
    const std::vector<Bytes> read = ot::sign::readFdfCertificates(text);
    ASSERT_EQ(read.size(), 1u);
    EXPECT_EQ(read[0], der);
    // Литеральная строка с экранированием — тоже читается.
    const std::string literal = "%FDF-1.2\n1 0 obj\n<</FDF<</Certs[(a\\)b\\\\c\\101)]>>>>\nendobj\n";
    const std::vector<Bytes> lit = ot::sign::readFdfCertificates(literal);
    ASSERT_EQ(lit.size(), 1u);
    EXPECT_EQ(lit[0], bytesOf("a)b\\cA"));
}
