// unit.chain — построение и проверка цепочки сертификатов (план 4.1, п. 5; эталон 3.8 «Валидатор» — доверие;
// sign-33 — ядро): подписант тестовой PKI строится до корня через выпускающий ЦС; без якоря — «не доверен»;
// доверенный промежуточный — якорь только с partialChain; момент проверки — до и после сроков; назначение
// подписи и метки времени; недостающий промежуточный — по AIA caIssuers через HttpFetcher; CRL в цепочке
// (CRL_CHECK_ALL): отозванный — revoked, без CRL — «отзыв неизвестен».
#include <gtest/gtest.h>

#include <ctime>
#include <string>

#include "core/sign/chain.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/test_pdf.h"

namespace {

using ot::sign::ChainBuilder;
using ot::sign::ChainPurpose;
using ot::sign::ChainResult;
using ot::sign::ChainStatus;
using ot::sign::Ossl;

std::time_t at(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    return _mkgmtime(&tm);
}

Ossl<X509_CRL> pkiCrl(const char* name) {
    const std::string der = ot::test::readPki(name);
    const auto* p = reinterpret_cast<const unsigned char*>(der.data());
    return Ossl<X509_CRL>(d2i_X509_CRL(nullptr, &p, static_cast<long>(der.size())));
}

ChainResult buildFor(const char* leaf, bool withInter = true,
                     ChainPurpose purpose = ChainPurpose::DocumentSigning) {
    auto store = ot::test::pkiRootStore();
    ChainBuilder b(store.get());
    if (withInter) {
        b.addUntrusted(ot::test::pkiCert("inter.pem").get());
    }
    b.setPurpose(purpose);
    return b.build(ot::test::pkiCert(leaf).get());
}

}  // namespace

TEST(chain, signer_to_test_root) {
    OT_FEATURE("sign-33");
    for (const char* leaf : {"signer-rsa.pem", "signer-ec.pem", "revoked.pem"}) {
        SCOPED_TRACE(leaf);
        const ChainResult r = buildFor(leaf);
        EXPECT_EQ(r.status, ChainStatus::Valid) << r.error;
        EXPECT_TRUE(r.trusted());
        ASSERT_EQ(r.chain.size(), 3U);
        EXPECT_EQ(r.chain[0].commonName, "АкроБатя тест");
        EXPECT_EQ(r.chain[1].commonName, "АкроБатя тест — выпускающий ЦС");
        EXPECT_EQ(r.chain[2].commonName, "АкроБатя тест — корневой ЦС");
        ASSERT_TRUE(r.certs);
        EXPECT_EQ(sk_X509_num(r.certs.get()), 3);
        EXPECT_EQ(r.opensslError, 0);
    }
    // Служба времени — со своим назначением.
    EXPECT_EQ(buildFor("tsa.pem", true, ChainPurpose::Timestamping).status, ChainStatus::Valid);
    EXPECT_EQ(buildFor("ocsp.pem", true, ChainPurpose::Any).status, ChainStatus::Valid);
}

TEST(chain, untrusted_without_anchor_or_intermediate) {
    // Нет промежуточного и сети — не построить.
    const ChainResult noInter = buildFor("signer-rsa.pem", false);
    EXPECT_EQ(noInter.status, ChainStatus::Untrusted);
    EXPECT_FALSE(noInter.trusted());
    EXPECT_EQ(noInter.errorDepth, 0);
    EXPECT_FALSE(noInter.error.empty());
    // Самоподписанный не из якорей.
    const std::string pem =
        ot::paths::readFile(ot::test::sourceDir() / "tests" / "fixtures" / "sig" / "cert.pem");
    auto self =
        ot::sign::loadCertificate(std::span(reinterpret_cast<const std::uint8_t*>(pem.data()), pem.size()));
    auto store = ot::test::pkiRootStore();
    ChainBuilder b(store.get());
    EXPECT_EQ(b.build(self.get()).status, ChainStatus::Untrusted);
    // Он же как якорь — доверен.
    ot::sign::Ossl<X509_STORE> own(X509_STORE_new());
    X509_STORE_add_cert(own.get(), self.get());
    ChainBuilder ownBuilder(own.get());
    const ChainResult r = ownBuilder.build(self.get());
    EXPECT_EQ(r.status, ChainStatus::Valid) << r.error;
    EXPECT_EQ(r.chain.size(), 1U);
    // Без хранилища — ничему не доверяем.
    ChainBuilder none(nullptr);
    none.addUntrusted(ot::test::pkiCert("inter.pem").get());
    EXPECT_EQ(none.build(ot::test::pkiCert("signer-rsa.pem").get()).status, ChainStatus::Untrusted);
}

TEST(chain, partial_chain_trusts_intermediate) {
    ot::sign::Ossl<X509_STORE> interOnly(X509_STORE_new());
    X509_STORE_add_cert(interOnly.get(), ot::test::pkiCert("inter.pem").get());
    ChainBuilder strict(interOnly.get());
    EXPECT_EQ(strict.build(ot::test::pkiCert("signer-rsa.pem").get()).status, ChainStatus::Untrusted);
    ChainBuilder partial(interOnly.get(), std::nullopt, true);
    const ChainResult r = partial.build(ot::test::pkiCert("signer-rsa.pem").get());
    EXPECT_EQ(r.status, ChainStatus::Valid) << r.error;
    EXPECT_EQ(r.chain.size(), 2U);
}

TEST(chain, validation_time) {
    auto store = ot::test::pkiRootStore();
    const auto leaf = ot::test::pkiCert("signer-rsa.pem");
    const auto inter = ot::test::pkiCert("inter.pem");
    const auto statusAt = [&](std::time_t t) {
        ChainBuilder b(store.get(), t);
        b.addUntrusted(inter.get());
        return b.build(leaf.get()).status;
    };
    EXPECT_EQ(statusAt(at(2026, 3, 1)), ChainStatus::Valid);
    EXPECT_EQ(statusAt(at(2035, 12, 1)), ChainStatus::Valid);
    EXPECT_EQ(statusAt(at(2036, 6, 1)), ChainStatus::Expired);
    EXPECT_EQ(statusAt(at(2025, 6, 1)), ChainStatus::NotYetValid);
}

TEST(chain, purpose) {
    // Сертификат службы времени (единственный EKU timeStamping) — не для подписи документа.
    EXPECT_EQ(buildFor("tsa.pem", true, ChainPurpose::DocumentSigning).status, ChainStatus::InvalidPurpose);
    // OCSP-респондер — тоже.
    EXPECT_EQ(buildFor("ocsp.pem", true, ChainPurpose::DocumentSigning).status, ChainStatus::InvalidPurpose);
    // Подписант — не служба времени.
    EXPECT_EQ(buildFor("signer-rsa.pem", true, ChainPurpose::Timestamping).status,
              ChainStatus::InvalidPurpose);
    // ЦС как конечный сертификат подписи — нет (keyUsage без digitalSignature).
    EXPECT_EQ(buildFor("inter.pem", false, ChainPurpose::DocumentSigning).status,
              ChainStatus::InvalidPurpose);
}

TEST(chain, missing_intermediate_from_aia) {
    ot::net::FakeHttpFetcher fake;
    const std::string cer = ot::test::readPki("inter.cer");
    fake.addBody("http://pki.acrobatya.test/inter.cer", std::vector<std::uint8_t>(cer.begin(), cer.end()),
                 "application/pkix-cert");
    auto store = ot::test::pkiRootStore();
    ChainBuilder b(store.get());
    b.setFetcher(&fake);
    const ChainResult r = b.build(ot::test::pkiCert("signer-rsa.pem").get());
    EXPECT_EQ(r.status, ChainStatus::Valid) << r.error;
    EXPECT_EQ(r.chain.size(), 3U);
    ASSERT_EQ(fake.requestCount(), 1U);
    EXPECT_EQ(fake.requests()[0].url, "http://pki.acrobatya.test/inter.cer");
    EXPECT_EQ(fake.requests()[0].purpose, ot::net::Purpose::Signatures);
    EXPECT_EQ(r.fetched.size(), 1U);  // скачанные — для DSS (4.5)

    // Сеть не ответила — «не доверен», без падения.
    ot::net::FakeHttpFetcher silent;
    ChainBuilder c(store.get());
    c.setFetcher(&silent);
    EXPECT_EQ(c.build(ot::test::pkiCert("signer-rsa.pem").get()).status, ChainStatus::Untrusted);
    EXPECT_EQ(silent.requestCount(), 1U);
}

TEST(chain, crl_check_in_chain) {
    auto store = ot::test::pkiRootStore();
    const auto run = [&](const char* leaf, std::initializer_list<const char*> crls, std::time_t t) {
        ChainBuilder b(store.get(), t);
        b.addUntrusted(ot::test::pkiCert("inter.pem").get());
        b.setCrlCheck(ot::sign::CrlCheck::All);
        for (const char* c : crls) {
            b.addCrl(pkiCrl(c).get());
        }
        return b.build(ot::test::pkiCert(leaf).get());
    };
    const std::time_t now = std::time(nullptr);
    EXPECT_EQ(run("signer-rsa.pem", {"root.crl", "inter-revoked.crl"}, now).status, ChainStatus::Valid);
    const ChainResult revoked = run("revoked.pem", {"root.crl", "inter-revoked.crl"}, now);
    EXPECT_EQ(revoked.status, ChainStatus::Revoked) << revoked.error;
    EXPECT_EQ(revoked.errorDepth, 0);
    EXPECT_EQ(run("revoked.pem", {"root.crl", "inter-empty.crl"}, now).status, ChainStatus::Valid);
    // Без данных об отзыве — неизвестно, не «недействителен».
    EXPECT_EQ(run("revoked.pem", {}, now).status, ChainStatus::RevocationUnknown);
    // Нет CRL корня: для промежуточного отзыв неизвестен (отозванный конечный OpenSSL находит раньше —
    // revoked).
    EXPECT_EQ(run("signer-rsa.pem", {"inter-revoked.crl"}, now).status, ChainStatus::RevocationUnknown);
    EXPECT_EQ(run("revoked.pem", {"inter-revoked.crl"}, now).status, ChainStatus::Revoked);
}

TEST(chain, status_names) {
    EXPECT_STREQ(ot::sign::chainStatusName(ChainStatus::Valid), "valid");
    EXPECT_STREQ(ot::sign::chainStatusName(ChainStatus::Revoked), "revoked");
    EXPECT_STREQ(ot::sign::chainStatusName(ChainStatus::RevocationUnknown), "revocation unknown");
}
