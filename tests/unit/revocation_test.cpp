// unit.revocation — отзыв сертификатов (план 4.1, п. 5; эталон 3.8 «Валидатор» — OCSP, затем CRL; sign-33 —
// ядро): OCSP-ответы фикстуры (openssl ocsp) разбираются, подпись респондера проверяется, статусы good /
// revoked (дата и причина по RFC 5280) / unknown; CRL с подписью издателя; без данных — unknown, а не
// revoked; момент проверки и режим retroactive (данные отзыва действуют сколь угодно назад, как у Acrobat и
// pyHanko --retroactive-revinfo); данные из сети — по AIA OCSP и CRL DP через HttpFetcher; выключенная сеть —
// unknown с причиной; вся цепочка разом.
#include <gtest/gtest.h>

#include <openssl/ocsp.h>

#include <ctime>
#include <string>

#include "core/sign/chain.h"
#include "core/sign/revocation.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "support/pki.h"

namespace {

using ot::sign::Bytes;
using ot::sign::RevocationChecker;
using ot::sign::RevocationData;
using ot::sign::RevocationReason;
using ot::sign::RevocationResult;
using ot::sign::RevocationSource;
using ot::sign::RevocationStatus;

std::time_t at(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = 12;
    return _mkgmtime(&tm);
}

Bytes pkiBytes(const char* name) {
    const std::string s = ot::test::readPki(name);
    return {s.begin(), s.end()};
}

RevocationResult check(RevocationData data, const char* cert, std::optional<std::time_t> t = std::nullopt,
                       bool retroactive = false) {
    RevocationChecker c(std::move(data), nullptr);
    if (t) {
        c.setTime(*t);
    }
    c.setRetroactive(retroactive);
    return c.check(ot::test::pkiCert(cert).get(), ot::test::pkiCert("inter.pem").get());
}

RevocationData ocsp(std::initializer_list<const char*> files) {
    RevocationData d;
    for (const char* f : files) {
        d.ocspResponses.push_back(pkiBytes(f));
    }
    return d;
}

RevocationData crl(std::initializer_list<const char*> files) {
    RevocationData d;
    for (const char* f : files) {
        d.crls.push_back(pkiBytes(f));
    }
    return d;
}

// Ответчик OCSP тестовой PKI за FakeHttpFetcher: ответ фикстуры по серийному номеру из запроса.
ot::net::Response ocspResponder(const ot::net::Request& r) {
    ot::net::Response resp;
    const unsigned char* p = r.body.data();
    ot::sign::Ossl<OCSP_REQUEST> req(d2i_OCSP_REQUEST(nullptr, &p, static_cast<long>(r.body.size())));
    if (!req || OCSP_request_onereq_count(req.get()) != 1) {
        resp.status = ot::net::Status::HttpError;
        resp.httpCode = 400;
        return resp;
    }
    ASN1_INTEGER* serial = nullptr;
    OCSP_id_get0_info(nullptr, nullptr, nullptr, &serial,
                      OCSP_onereq_get0_id(OCSP_request_onereq_get0(req.get(), 0)));
    const long s = ASN1_INTEGER_get(serial);
    const char* file = s == 0x2000   ? "ocsp-good.der"
                       : s == 0x2001 ? "ocsp-ec-good.der"
                       : s == 0x2002 ? "ocsp-revoked.der"
                                     : "ocsp-unknown.der";
    resp.status = ot::net::Status::Ok;
    resp.httpCode = 200;
    resp.contentType = "application/ocsp-response";
    resp.body = pkiBytes(file);
    return resp;
}

}  // namespace

TEST(revocation, ocsp_fixture_good) {
    OT_FEATURE("sign-33");
    const RevocationResult r = check(ocsp({"ocsp-good.der"}), "signer-rsa.pem");
    EXPECT_EQ(r.status, RevocationStatus::Good) << r.detail;
    EXPECT_EQ(r.source, RevocationSource::Ocsp);
    EXPECT_FALSE(r.thisUpdate.empty());
    EXPECT_EQ(r.nextUpdate.substr(0, 3), "203");
    EXPECT_EQ(r.ocspResponse, pkiBytes("ocsp-good.der"));  // для DSS (4.5)
    EXPECT_EQ(r.responder, "АкроБатя тест — OCSP");
    const RevocationResult ec = check(ocsp({"ocsp-good.der", "ocsp-ec-good.der"}), "signer-ec.pem");
    EXPECT_EQ(ec.status, RevocationStatus::Good) << ec.detail;
    EXPECT_EQ(ec.ocspResponse, pkiBytes("ocsp-ec-good.der"));
}

TEST(revocation, ocsp_revoked_with_reason_and_date) {
    const RevocationResult r = check(ocsp({"ocsp-revoked.der"}), "revoked.pem");
    EXPECT_EQ(r.status, RevocationStatus::Revoked) << r.detail;
    EXPECT_EQ(r.source, RevocationSource::Ocsp);
    EXPECT_EQ(r.reason, RevocationReason::KeyCompromise);
    EXPECT_STREQ(ot::sign::revocationReasonName(r.reason), "keyCompromise");
    EXPECT_EQ(r.revocationTime, "2026-06-01T00:00:00Z");
}

TEST(revocation, ocsp_unknown_status) {
    // Сертификат с серийным 0x0BAD от выпускающего ЦС — респондер его не знает.
    ot::sign::Ossl<X509> fake(X509_new());
    ot::sign::Ossl<ASN1_INTEGER> serial(ASN1_INTEGER_new());
    ASN1_INTEGER_set(serial.get(), 0x0BAD);
    X509_set_serialNumber(fake.get(), serial.get());
    X509_set_issuer_name(fake.get(), X509_get_subject_name(ot::test::pkiCert("inter.pem").get()));
    RevocationChecker c(ocsp({"ocsp-unknown.der"}), nullptr);
    const RevocationResult r = c.check(fake.get(), ot::test::pkiCert("inter.pem").get());
    EXPECT_EQ(r.status, RevocationStatus::Unknown);
    EXPECT_EQ(r.source, RevocationSource::Ocsp);
    EXPECT_NE(r.detail.find("unknown"), std::string::npos) << r.detail;
}

TEST(revocation, no_data_is_unknown_not_revoked) {
    const RevocationResult r = check({}, "revoked.pem");
    EXPECT_EQ(r.status, RevocationStatus::Unknown);
    EXPECT_EQ(r.source, RevocationSource::None);
    EXPECT_FALSE(r.detail.empty());
    // Ответ о другом сертификате — тоже нет данных.
    EXPECT_EQ(check(ocsp({"ocsp-good.der"}), "revoked.pem").status, RevocationStatus::Unknown);
}

TEST(revocation, crl) {
    const RevocationResult revoked = check(crl({"inter-revoked.crl"}), "revoked.pem");
    EXPECT_EQ(revoked.status, RevocationStatus::Revoked) << revoked.detail;
    EXPECT_EQ(revoked.source, RevocationSource::Crl);
    EXPECT_EQ(revoked.reason, RevocationReason::KeyCompromise);
    EXPECT_EQ(revoked.revocationTime, "2026-06-01T00:00:00Z");
    EXPECT_EQ(revoked.crl, pkiBytes("inter-revoked.crl"));
    const RevocationResult good = check(crl({"inter-revoked.crl"}), "signer-rsa.pem");
    EXPECT_EQ(good.status, RevocationStatus::Good) << good.detail;
    EXPECT_EQ(good.source, RevocationSource::Crl);
    EXPECT_EQ(good.thisUpdate, "2026-07-01T00:00:00Z");
    // Старая пустая и новая с отозванным — отзыв главнее.
    EXPECT_EQ(check(crl({"inter-empty.crl", "inter-revoked.crl"}), "revoked.pem").status,
              RevocationStatus::Revoked);
    // CRL чужого издателя (корня) к сертификату выпускающего ЦС не подходит.
    EXPECT_EQ(check(crl({"root.crl"}), "revoked.pem").status, RevocationStatus::Unknown);
}

TEST(revocation, validation_time_and_retroactive) {
    const std::time_t march = at(2026, 3, 1);  // до отзыва (2026-06-01) и до выпуска CRL/OCSP
    // Строго: данные, выпущенные позже момента проверки, на этот момент не действуют.
    const RevocationResult strictOcsp = check(ocsp({"ocsp-good.der"}), "signer-rsa.pem", march);
    EXPECT_EQ(strictOcsp.status, RevocationStatus::Unknown);
    EXPECT_NE(strictOcsp.detail.find("not yet valid"), std::string::npos) << strictOcsp.detail;
    EXPECT_EQ(check(crl({"inter-revoked.crl"}), "revoked.pem", march).status, RevocationStatus::Unknown);
    // Пустая CRL от 2026-01-01 действует в марте.
    EXPECT_EQ(check(crl({"inter-empty.crl", "inter-revoked.crl"}), "revoked.pem", march).status,
              RevocationStatus::Good);
    // retroactive: поздние данные годятся; отзыв после момента проверки — ещё не отозван.
    EXPECT_EQ(check(ocsp({"ocsp-good.der"}), "signer-rsa.pem", march, true).status, RevocationStatus::Good);
    const RevocationResult later = check(ocsp({"ocsp-revoked.der"}), "revoked.pem", march, true);
    EXPECT_EQ(later.status, RevocationStatus::Good) << later.detail;
    EXPECT_EQ(later.revocationTime, "2026-06-01T00:00:00Z");  // отозван позже — видно
    EXPECT_EQ(check(crl({"inter-revoked.crl"}), "revoked.pem", march, true).status, RevocationStatus::Good);
    // После отзыва — отозван.
    EXPECT_EQ(check(crl({"inter-revoked.crl"}), "revoked.pem", at(2026, 8, 1)).status,
              RevocationStatus::Revoked);
    // После nextUpdate (2036) данные устарели.
    EXPECT_EQ(check(crl({"inter-revoked.crl"}), "signer-rsa.pem", at(2036, 6, 1)).status,
              RevocationStatus::Unknown);
}

TEST(revocation, tampered_ocsp_is_ignored) {
    Bytes bad = pkiBytes("ocsp-good.der");
    bad[bad.size() - 10] ^= 0x55;  // подпись респондера
    RevocationData d;
    d.ocspResponses.push_back(bad);
    const RevocationResult r = check(d, "signer-rsa.pem");
    EXPECT_EQ(r.status, RevocationStatus::Unknown);
    EXPECT_NE(r.detail.find("OCSP"), std::string::npos) << r.detail;
    // Мусор вместо CRL — тоже не данные.
    RevocationData g;
    g.crls.push_back({1, 2, 3});
    EXPECT_EQ(check(g, "signer-rsa.pem").status, RevocationStatus::Unknown);
}

TEST(revocation, responder_with_nocheck) {
    const RevocationResult r = check({}, "ocsp.pem");
    EXPECT_EQ(r.status, RevocationStatus::Good);
    EXPECT_NE(r.detail.find("nocheck"), std::string::npos) << r.detail;
}

TEST(revocation, network_ocsp_then_crl) {
    ot::net::FakeHttpFetcher fake;
    fake.setHandler([](const ot::net::Request& r) {
        if (r.url == "http://pki.acrobatya.test/ocsp") {
            return ocspResponder(r);
        }
        ot::net::Response none;
        none.status = ot::net::Status::HttpError;
        none.httpCode = 404;
        return none;
    });
    fake.addBody("http://pki.acrobatya.test/inter.crl", pkiBytes("inter-revoked.crl"),
                 "application/pkix-crl");
    const auto inter = ot::test::pkiCert("inter.pem");

    RevocationChecker c({}, &fake);
    const RevocationResult good = c.check(ot::test::pkiCert("signer-rsa.pem").get(), inter.get());
    EXPECT_EQ(good.status, RevocationStatus::Good) << good.detail;
    EXPECT_EQ(good.source, RevocationSource::Ocsp);
    ASSERT_EQ(fake.requestCount(), 1U);
    const ot::net::Request req = fake.requests()[0];
    EXPECT_EQ(req.method, "POST");
    EXPECT_EQ(req.contentType, "application/ocsp-request");
    EXPECT_EQ(req.purpose, ot::net::Purpose::Signatures);
    EXPECT_TRUE(req.sameHostRedirectsOnly);
    EXPECT_EQ(c.check(ot::test::pkiCert("revoked.pem").get(), inter.get()).status, RevocationStatus::Revoked);

    // Только CRL — по CRL DP.
    fake.clearRequests();
    RevocationChecker crlOnly({}, &fake);
    crlOnly.setOcsp(false);
    const RevocationResult viaCrl = crlOnly.check(ot::test::pkiCert("revoked.pem").get(), inter.get());
    EXPECT_EQ(viaCrl.status, RevocationStatus::Revoked) << viaCrl.detail;
    EXPECT_EQ(viaCrl.source, RevocationSource::Crl);
    ASSERT_EQ(fake.requestCount(), 1U);
    EXPECT_EQ(fake.requests()[0].url, "http://pki.acrobatya.test/inter.crl");

    // Известные данные — без сети.
    fake.clearRequests();
    RevocationChecker known(ocsp({"ocsp-good.der"}), &fake);
    EXPECT_EQ(known.check(ot::test::pkiCert("signer-rsa.pem").get(), inter.get()).status,
              RevocationStatus::Good);
    EXPECT_EQ(fake.requestCount(), 0U);

    // Сеть выключена — неизвестно, с причиной.
    ot::net::FakeHttpFetcher off;
    off.setHandler([](const ot::net::Request&) {
        ot::net::Response r;
        r.status = ot::net::Status::Disabled;
        return r;
    });
    RevocationChecker offline({}, &off);
    const RevocationResult unknown = offline.check(ot::test::pkiCert("signer-rsa.pem").get(), inter.get());
    EXPECT_EQ(unknown.status, RevocationStatus::Unknown);
    EXPECT_NE(unknown.detail.find("disabled"), std::string::npos) << unknown.detail;
    EXPECT_EQ(off.requestCount(), 2U);  // OCSP и CRL
}

TEST(revocation, whole_chain) {
    auto store = ot::test::pkiRootStore();
    ot::sign::ChainBuilder builder(store.get());
    builder.addUntrusted(ot::test::pkiCert("inter.pem").get());
    const ot::sign::ChainResult chain = builder.build(ot::test::pkiCert("signer-rsa.pem").get());
    ASSERT_TRUE(chain.trusted());
    RevocationData data = ocsp({"ocsp-good.der"});
    data.crls.push_back(pkiBytes("root.crl"));
    RevocationChecker c(std::move(data), nullptr);
    const std::vector<RevocationResult> all = c.checkChain(chain);
    ASSERT_EQ(all.size(), 2U);  // подписант и выпускающий ЦС; корень — якорь
    EXPECT_EQ(all[0].status, RevocationStatus::Good);
    EXPECT_EQ(all[0].source, RevocationSource::Ocsp);
    EXPECT_EQ(all[1].status, RevocationStatus::Good);
    EXPECT_EQ(all[1].source, RevocationSource::Crl);
    EXPECT_EQ(ot::sign::worstStatus(all), RevocationStatus::Good);
    // Без CRL корня статус выпускающего ЦС неизвестен — и цепочка в целом.
    RevocationChecker partial(ocsp({"ocsp-good.der"}), nullptr);
    EXPECT_EQ(ot::sign::worstStatus(partial.checkChain(chain)), RevocationStatus::Unknown);
}
