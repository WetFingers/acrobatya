// unit.pkcs12 — удостоверение из файла PKCS#12 (план 4.1, п. 3; эталон 3.8; sign-06 — «Digital ID from a
// file»): .pfx тестовой PKI читаются из памяти (файл открывает вызывающий — QFile в services/ui),
// AES-256/SHA-256 и legacy RC2-40 (провайдер legacy по требованию), пароль UTF-8; неверный пароль и
// испорченный файл — свои ошибки.
#include <gtest/gtest.h>

#include <openssl/x509.h>

#include <string>

#include "core/sign/pkcs12.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "support/pki.h"

namespace {

using ot::sign::Pkcs12Error;
using ot::sign::Pkcs12Status;

std::vector<std::uint8_t> pfx(const char* name) {
    const std::string bytes = ot::test::readPki(name);
    return {bytes.begin(), bytes.end()};
}

Pkcs12Status statusOf(const char* name, std::string_view password) {
    try {
        (void)ot::sign::loadPkcs12(pfx(name), password);
    } catch (const Pkcs12Error& e) {
        return e.status();
    }
    return Pkcs12Status::Ok;
}

}  // namespace

TEST(pkcs12, aes_rsa_and_ec) {
    OT_FEATURE("sign-06");
    for (const char* name : {"signer-rsa.pfx", "signer-ec.pfx"}) {
        SCOPED_TRACE(name);
        const ot::sign::SigningKey id = ot::sign::loadPkcs12(pfx(name), "acrobatya");
        ASSERT_TRUE(id.key && id.cert && id.chain);
        EXPECT_EQ(X509_check_private_key(id.cert.get(), id.key.get()), 1);
        EXPECT_EQ(ot::sign::commonName(X509_get_subject_name(id.cert.get())), "АкроБатя тест");
        EXPECT_EQ(sk_X509_num(id.chain.get()), 2);  // выпускающий ЦС и корень
        EXPECT_EQ(id.friendlyName,
                  std::string("AcroBatya test ") + (name[7] == 'r' ? "signer-rsa" : "signer-ec"));
    }
    const ot::sign::SigningKey ec = ot::sign::loadPkcs12(pfx("signer-ec.pfx"), "acrobatya");
    EXPECT_EQ(EVP_PKEY_get_base_id(ec.key.get()), EVP_PKEY_EC);
}

TEST(pkcs12, legacy_rc2_is_read) {
    const ot::sign::SigningKey id = ot::sign::loadPkcs12(pfx("signer-rsa-legacy.pfx"), "acrobatya");
    ASSERT_TRUE(id.key && id.cert);
    EXPECT_EQ(X509_check_private_key(id.cert.get(), id.key.get()), 1);
    EXPECT_EQ(EVP_PKEY_get_base_id(id.key.get()), EVP_PKEY_RSA);
}

TEST(pkcs12, utf8_password) {
    const ot::sign::SigningKey id = ot::sign::loadPkcs12(pfx("signer-rsa-cyr.pfx"), "пароль");
    ASSERT_TRUE(id.key && id.cert);
    EXPECT_EQ(statusOf("signer-rsa-cyr.pfx", "parol"), Pkcs12Status::BadPassword);
}

TEST(pkcs12, wrong_password_and_damaged) {
    EXPECT_EQ(statusOf("signer-rsa.pfx", "wrong"), Pkcs12Status::BadPassword);
    EXPECT_EQ(statusOf("signer-rsa.pfx", ""), Pkcs12Status::BadPassword);
    EXPECT_EQ(statusOf("signer-rsa-legacy.pfx", "wrong"), Pkcs12Status::BadPassword);
    EXPECT_EQ(statusOf("signer-ec.pfx", "acrobatya"), Pkcs12Status::Ok);

    std::vector<std::uint8_t> broken = pfx("signer-rsa.pfx");
    broken.resize(broken.size() / 2);
    EXPECT_THROW(
        {
            try {
                (void)ot::sign::loadPkcs12(broken, "acrobatya");
            } catch (const Pkcs12Error& e) {
                EXPECT_EQ(e.status(), Pkcs12Status::Damaged);
                throw;
            }
        },
        Pkcs12Error);
    const std::vector<std::uint8_t> garbage = {'n', 'o', 't', ' ', 'p', 'f', 'x'};
    EXPECT_THROW((void)ot::sign::loadPkcs12(garbage, "x"), Pkcs12Error);
    EXPECT_THROW((void)ot::sign::loadPkcs12({}, "x"), Pkcs12Error);
}
