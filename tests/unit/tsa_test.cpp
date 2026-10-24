// unit.tsa — метки времени RFC 3161 (план 4.1, п. 4; эталон 3.8, 7.4; sign-19 — ядро): запрос с imprint,
// nonce и certReq; токен от службы времени тестовой PKI (в процессе — TS_RESP_CTX за FakeHttpFetcher, и живой
// tools/pki/tsa_serve.py через QtHttpFetcher) проверяется по корню PKI; nonce и imprint сверяются — ответ на
// чужой запрос и чужой хэш — отказ; сбои сервера и сети — свои статусы; токен встаёт в CMS unsigned-атрибутом
// и проходит внешний `openssl ts -verify`. compat: публичные TSA (DigiCert, Sectigo, GlobalSign) — только с
// OTTISK_LIVE_NET=1.
#include <gtest/gtest.h>

#include <openssl/ts.h>

#include <QDateTime>
#include <QString>

#include <chrono>
#include <cstdlib>
#include <string>

#include "core/sign/cms.h"
#include "core/sign/pkcs12.h"
#include "core/sign/tsa.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "core/util/paths.h"
#include "services/http_fetcher.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"
#include "support/tsa_server.h"

namespace {

using ot::sign::Bytes;
using ot::sign::TsaClient;
using ot::sign::TsaConfig;
using ot::sign::TsaError;
using ot::sign::TsaStatus;

Bytes digestOf(std::string_view text) { return ot::sign::ossl::sha256(text); }

// FakeHttpFetcher, отвечающий службой времени PKI в процессе.
void serveLocalTsa(ot::net::FakeHttpFetcher& fake) {
    fake.setHandler([](const ot::net::Request& r) {
        ot::net::Response resp;
        resp.status = ot::net::Status::Ok;
        resp.httpCode = 200;
        resp.contentType = "application/timestamp-reply";
        resp.body = ot::test::localTsaReply(r.body);
        resp.finalUrl = r.url;
        return resp;
    });
}

TsaStatus statusOf(TsaClient& client, const Bytes& digest) {
    try {
        (void)client.timestamp(digest);
    } catch (const TsaError& e) {
        return e.status();
    }
    return TsaStatus::Ok;
}

int secondsFromNow(const std::string& iso) {
    const QDateTime t = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    return static_cast<int>(std::llabs(t.secsTo(QDateTime::currentDateTimeUtc())));
}

}  // namespace

TEST(tsa, request_structure) {
    const Bytes digest = digestOf("request");
    const ot::sign::TsaRequest a = ot::sign::makeTsaRequest(digest);
    const ot::sign::TsaRequest b =
        ot::sign::makeTsaRequest(digest, ot::sign::DigestAlgorithm::Sha256, "1.2.3.4.5.6");
    EXPECT_NE(a.nonce, b.nonce);  // 64 случайных бита
    EXPECT_GE(a.nonce.size(), 7U);
    EXPECT_EQ(a.imprint, digest);
    const unsigned char* p = a.der.data();
    ot::sign::Ossl<TS_REQ> req(d2i_TS_REQ(nullptr, &p, static_cast<long>(a.der.size())));
    ASSERT_TRUE(req);
    EXPECT_EQ(TS_REQ_get_version(req.get()), 1);
    EXPECT_EQ(TS_REQ_get_cert_req(req.get()), 1);
    EXPECT_EQ(TS_REQ_get_policy_id(req.get()), nullptr);
    const ASN1_OCTET_STRING* msg = TS_MSG_IMPRINT_get_msg(TS_REQ_get_msg_imprint(req.get()));
    EXPECT_EQ(Bytes(ASN1_STRING_get0_data(msg), ASN1_STRING_get0_data(msg) + ASN1_STRING_length(msg)),
              digest);
    p = b.der.data();
    ot::sign::Ossl<TS_REQ> withPolicy(d2i_TS_REQ(nullptr, &p, static_cast<long>(b.der.size())));
    ASSERT_NE(TS_REQ_get_policy_id(withPolicy.get()), nullptr);
    EXPECT_THROW((void)ot::sign::makeTsaRequest(Bytes(20, 0)), std::invalid_argument);
}

TEST(tsa, token_from_pki_tsa_via_fake_fetcher) {
    OT_FEATURE("sign-19");
    ot::net::FakeHttpFetcher fake;
    serveLocalTsa(fake);
    TsaConfig config;
    config.url = "http://tsa.acrobatya.test/tsr";
    config.user = "user";
    config.password = "pass";
    TsaClient client(fake, config);
    auto store = ot::test::pkiRootStore();
    client.setTrustStore(store.get());
    const Bytes digest = digestOf("подпись");
    const Bytes token = client.timestamp(digest);
    ASSERT_FALSE(token.empty());

    // Запрос: POST application/timestamp-query, Basic-аутентификация, без перенаправлений на другой хост.
    const auto requests = fake.requests();
    ASSERT_EQ(requests.size(), 1U);
    const ot::net::Request& r = requests[0];
    EXPECT_EQ(r.method, "POST");
    EXPECT_EQ(r.url, config.url);
    EXPECT_EQ(r.contentType, "application/timestamp-query");
    EXPECT_EQ(r.timeoutMs, 15000);
    EXPECT_TRUE(r.sameHostRedirectsOnly);
    EXPECT_EQ(r.purpose, ot::net::Purpose::Signatures);
    bool auth = false;
    for (const auto& [k, v] : r.headers) {
        auth = auth || (k == "Authorization" && v == "Basic dXNlcjpwYXNz");
    }
    EXPECT_TRUE(auth);

    const ot::sign::TimestampInfo& info = client.lastInfo();
    EXPECT_TRUE(info.trusted);
    EXPECT_EQ(info.policy, "1.2.3.4.1");
    EXPECT_EQ(info.hashAlgorithm, std::string(ot::sign::oid::kSha256));
    EXPECT_EQ(info.imprint, digest);
    EXPECT_FALSE(info.nonce.empty());
    EXPECT_FALSE(info.serial.empty());
    EXPECT_DOUBLE_EQ(info.accuracySeconds, 1.0);
    EXPECT_LT(secondsFromNow(info.genTime), 300) << info.genTime;
    EXPECT_EQ(info.signer.commonName, "АкроБатя тест — служба времени");
    EXPECT_NE(info.tsaName.find("CN=АкроБатя тест — служба времени"), std::string::npos) << info.tsaName;

    // Сведения из токена — те же.
    const ot::sign::TimestampInfo parsed = ot::sign::parseTimestampToken(token);
    EXPECT_EQ(parsed.genTime, info.genTime);
    EXPECT_EQ(parsed.serial, info.serial);
    EXPECT_EQ(parsed.imprint, digest);
    EXPECT_EQ(parsed.signer.commonName, info.signer.commonName);

    // Без якорей — только согласованность: подпись верна, доверия нет.
    TsaClient untrusted(fake, config);
    (void)untrusted.timestamp(digest);
    EXPECT_FALSE(untrusted.lastInfo().trusted);

    // Проверка готового токена: свой хэш — да, чужой — нет.
    std::string error;
    EXPECT_TRUE(ot::sign::verifyTimestampToken(token, digest, store.get(), &error)) << error;
    EXPECT_TRUE(ot::sign::verifyTimestampToken(token, digest, nullptr, &error)) << error;
    EXPECT_FALSE(ot::sign::verifyTimestampToken(token, digestOf("другое"), store.get(), &error));
    EXPECT_FALSE(error.empty());
    // Чужой корень — не доверен.
    ot::sign::Ossl<X509_STORE> foreign(X509_STORE_new());
    const std::string selfSigned =
        ot::paths::readFile(ot::test::sourceDir() / "tests" / "fixtures" / "sig" / "cert.pem");
    X509_STORE_add_cert(foreign.get(), ot::sign::loadCertificate(
                                           std::span(reinterpret_cast<const std::uint8_t*>(selfSigned.data()),
                                                     selfSigned.size()))
                                           .get());
    EXPECT_FALSE(ot::sign::verifyTimestampToken(token, digest, foreign.get(), &error));
}

TEST(tsa, nonce_and_imprint_are_checked) {
    auto store = ot::test::pkiRootStore();
    const Bytes digest = digestOf("документ");
    const ot::sign::TsaRequest req = ot::sign::makeTsaRequest(digest);
    const Bytes reply = ot::test::localTsaReply(req.der);
    ot::sign::TimestampInfo info;
    EXPECT_NO_THROW((void)ot::sign::checkTsaResponse(reply, req, store.get(), &info));
    EXPECT_EQ(info.nonce, req.nonce);

    const auto failure = [&](const ot::sign::TsaRequest& other) {
        try {
            (void)ot::sign::checkTsaResponse(reply, other, store.get());
        } catch (const TsaError& e) {
            return e.status();
        }
        return TsaStatus::Ok;
    };
    // Тот же хэш, другой nonce — ответ не на этот запрос.
    EXPECT_EQ(failure(ot::sign::makeTsaRequest(digest)), TsaStatus::VerifyFailed);
    // Чужой imprint — отказ.
    ot::sign::TsaRequest foreignImprint = req;
    foreignImprint.imprint = digestOf("подменённый");
    const ot::sign::TsaRequest rebuilt = ot::sign::makeTsaRequest(foreignImprint.imprint);
    EXPECT_EQ(failure(rebuilt), TsaStatus::VerifyFailed);
    // Испорченный ответ.
    Bytes broken = reply;
    broken[broken.size() / 2] ^= 0xFF;
    try {
        (void)ot::sign::checkTsaResponse(broken, req, store.get());
        ADD_FAILURE() << "broken reply accepted";
    } catch (const TsaError& e) {
        EXPECT_TRUE(e.status() == TsaStatus::BadResponse || e.status() == TsaStatus::VerifyFailed);
    }
}

TEST(tsa, fetcher_failures_map_to_statuses) {
    const Bytes digest = digestOf("x");
    TsaConfig config;
    config.url = "http://tsa.acrobatya.test/tsr";
    const auto withResponse = [&](ot::net::Response r) {
        ot::net::FakeHttpFetcher fake;
        fake.add(config.url, std::move(r));
        TsaClient client(fake, config);
        return statusOf(client, digest);
    };
    ot::net::Response disabled;
    disabled.status = ot::net::Status::Disabled;
    EXPECT_EQ(withResponse(disabled), TsaStatus::NetworkDisabled);
    ot::net::Response timeout;
    timeout.status = ot::net::Status::Timeout;
    EXPECT_EQ(withResponse(timeout), TsaStatus::Timeout);
    ot::net::Response redirect;
    redirect.status = ot::net::Status::RedirectRefused;
    EXPECT_EQ(withResponse(redirect), TsaStatus::NetworkError);
    ot::net::Response http;
    http.status = ot::net::Status::HttpError;
    http.httpCode = 503;
    EXPECT_EQ(withResponse(http), TsaStatus::HttpError);
    ot::net::Response garbage;
    garbage.status = ot::net::Status::Ok;
    garbage.httpCode = 200;
    garbage.contentType = "application/timestamp-reply";
    garbage.body = {'n', 'o', 't', ' ', 'd', 'e', 'r'};
    EXPECT_EQ(withResponse(garbage), TsaStatus::BadResponse);
    ot::net::Response html = garbage;
    html.contentType = "text/html";
    EXPECT_EQ(withResponse(html), TsaStatus::BadResponse);
    ot::net::Response rejected = garbage;
    rejected.body = {0x30, 0x05, 0x30, 0x03, 0x02, 0x01, 0x02};  // PKIStatus rejection
    EXPECT_EQ(withResponse(rejected), TsaStatus::Rejected);
    // «application/timestamp-response» (опечатка RFC 3161 §3.4) — принимается.
    ot::net::FakeHttpFetcher typo;
    typo.setHandler([](const ot::net::Request& r) {
        ot::net::Response resp;
        resp.status = ot::net::Status::Ok;
        resp.httpCode = 200;
        resp.contentType = "application/timestamp-response";
        resp.body = ot::test::localTsaReply(r.body);
        return resp;
    });
    TsaClient client(typo, config);
    EXPECT_EQ(statusOf(client, digest), TsaStatus::Ok);
}

TEST(tsa, live_local_server) {
    OT_FEATURE("sign-19");
    std::string reason;
    auto server = ot::test::TsaServer::start(&reason);
    if (!server) {
        GTEST_SKIP() << reason;
    }
    ot::QtHttpFetcher fetcher(ot::NetworkAccess::SignaturesOnly);
    TsaConfig config;
    config.url = server->url("/tsr");
    TsaClient client(fetcher, config);
    auto store = ot::test::pkiRootStore();
    client.setTrustStore(store.get());

    // B-T: метка на хэш значения подписи CMS, токен — unsigned-атрибутом.
    const std::string bytes = ot::test::readPki("signer-rsa.pfx");
    const auto id = ot::sign::loadPkcs12(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), "acrobatya");
    ot::sign::CmsBuilder builder(id);
    const std::string data = "АкроБатя: подписанные байты";
    (void)builder.buildFromDigest(digestOf(data));
    const Bytes sigDigest = ot::sign::ossl::sha256(builder.signatureValue());
    const Bytes token = client.timestamp(sigDigest);
    EXPECT_TRUE(client.lastInfo().trusted);
    EXPECT_EQ(client.lastInfo().policy, "1.2.3.4.1");
    EXPECT_EQ(client.lastInfo().imprint, sigDigest);
    EXPECT_LT(secondsFromNow(client.lastInfo().genTime), 300);
    const Bytes stamped = builder.addUnsignedTimestamp(token);
    EXPECT_LT(stamped.size(), 16384U);  // резерв /Contents
    EXPECT_EQ(ot::sign::parseCms(stamped).timestampToken, token);
    std::string error;
    EXPECT_TRUE(ot::sign::verifyTimestampToken(ot::sign::parseCms(stamped).timestampToken, sigDigest,
                                               store.get(), &error))
        << error;
    EXPECT_FALSE(ot::sign::verifyTimestampToken(token, digestOf(data), store.get(), &error));

    // Независимая проверка токена внешним openssl ts -verify.
    const auto exe = ot::test::opensslExe();
    if (!exe) {
        return;
    }
    ot::test::TempDir dir;
    const auto tokenFile = dir.path() / L"метка.tst";
    ot::paths::writeFile(tokenFile,
                         std::string_view(reinterpret_cast<const char*>(token.data()), token.size()));
    const auto res =
        ot::test::runProcess(*exe, {L"ts", L"-verify", L"-in", tokenFile.wstring(), L"-token_in", L"-digest",
                                    ot::paths::widen(ot::sign::ossl::toHex(sigDigest)), L"-CAfile",
                                    (ot::test::pkiDir() / "root.pem").wstring()});
    EXPECT_EQ(res.exitCode, 0) << res.output;
    EXPECT_NE(res.output.find("Verification: OK"), std::string::npos) << res.output;
}

TEST(tsa, live_server_failures) {
    std::string reason;
    auto server = ot::test::TsaServer::start(&reason);
    if (!server) {
        GTEST_SKIP() << reason;
    }
    ot::QtHttpFetcher fetcher(ot::NetworkAccess::Allowed);
    const Bytes digest = digestOf("сбой");
    const auto status = [&](const std::string& path, int timeoutMs = 15000) {
        TsaConfig config;
        config.url = server->url(path);
        config.timeoutMs = timeoutMs;
        TsaClient client(fetcher, config);
        return statusOf(client, digest);
    };
    EXPECT_EQ(status("/tsr"), TsaStatus::Ok);
    EXPECT_EQ(status("/http500"), TsaStatus::HttpError);
    EXPECT_EQ(status("/garbage"), TsaStatus::BadResponse);
    EXPECT_EQ(status("/wrongtype"), TsaStatus::BadResponse);
    EXPECT_EQ(status("/reject"), TsaStatus::Rejected);
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(status("/slow?ms=4000", 800), TsaStatus::Timeout);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(3500));
    // Перенаправление на тот же хост — идём, на другой — нет.
    EXPECT_EQ(status("/redirect?to=" + server->url("/tsr")), TsaStatus::Ok);
    EXPECT_EQ(status("/redirect?to=http://localhost:" + std::to_string(server->port()) + "/tsr"),
              TsaStatus::NetworkError);
    // Сеть выключена — запроса нет.
    ot::QtHttpFetcher off(ot::NetworkAccess::Off);
    TsaConfig config;
    config.url = server->url("/tsr");
    TsaClient client(off, config);
    EXPECT_EQ(statusOf(client, digest), TsaStatus::NetworkDisabled);
}

// compat.tsa.public: живые публичные службы времени (эталон 3.8 — DigiCert по умолчанию, Sectigo,
// GlobalSign). Только с OTTISK_LIVE_NET=1 (сети в тестах очереди нет); якорей их корней у нас нет — проверка
// согласованности.
TEST(tsa_compat, DISABLED_public_servers) {
    const char* live = std::getenv("OTTISK_LIVE_NET");
    if (live == nullptr || std::string(live) != "1") {
        GTEST_SKIP() << "OTTISK_LIVE_NET != 1";
    }
    ot::test::qtApp();
    ot::QtHttpFetcher fetcher(ot::NetworkAccess::SignaturesOnly);
    const std::pair<const char*, const char*> servers[] = {
        {"http://timestamp.digicert.com", "2.16.840.1.114412.7.1"},
        {"http://timestamp.sectigo.com", "1.3.6.1.4.1.6449.2.1.1"},
        {"http://timestamp.globalsign.com/tsa/r6advanced1", "1.3.6.1.4.1.4146.2.3.2.2"},
    };
    for (const auto& [url, policy] : servers) {
        SCOPED_TRACE(url);
        TsaConfig config;
        config.url = url;
        TsaClient client(fetcher, config);
        const Bytes digest = digestOf(std::string("АкроБатя ") + url);
        Bytes token;
        try {
            token = client.timestamp(digest);
        } catch (const TsaError& e) {
            ADD_FAILURE() << e.what();
            continue;
        }
        EXPECT_LT(token.size(), 12000U);  // влезает в 16 КиБ вместе с CMS
        EXPECT_EQ(client.lastInfo().imprint, digest);
        EXPECT_EQ(client.lastInfo().policy, policy);
        EXPECT_LT(secondsFromNow(client.lastInfo().genTime), 600);
        EXPECT_FALSE(client.lastInfo().signer.commonName.empty());
        std::printf("%s: %s, %zu bytes, %s\n", url, client.lastInfo().genTime.c_str(), token.size(),
                    client.lastInfo().signer.commonName.c_str());
    }
}
