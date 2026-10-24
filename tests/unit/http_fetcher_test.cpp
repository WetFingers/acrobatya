// unit.http_fetcher — единая точка сети (план 4.1, п. 4; эталон 7.4; security-36): QtHttpFetcher над
// QNetworkAccessManager синхронно, таймаут, без перенаправлений на другой хост, предел размера ответа,
// выключатель «Сеть: разрешена / только подписи / выключена» (trust/network), подмена на время теста; живой
// сервер — tools/pki/tsa_serve.py на 127.0.0.1. FakeHttpFetcher — ответы по адресу и запись запросов.
#include <gtest/gtest.h>

#include <QString>

#include <chrono>
#include <string>

#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "services/http_fetcher.h"
#include "services/settings.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"
#include "support/tsa_server.h"

namespace {

using ot::net::Purpose;
using ot::net::Request;
using ot::net::Status;

Request get(const std::string& url) {
    Request r;
    r.method = "GET";
    r.url = url;
    return r;
}

Request tsaQuery(const std::string& url) {
    Request r;
    r.method = "POST";
    r.url = url;
    r.contentType = "application/timestamp-query";
    r.body = ot::sign::ossl::sha256(std::string_view("not a query"));  // TSA ответит 500
    return r;
}

}  // namespace

TEST(http_fetcher, network_switch) {
    OT_FEATURE("security-36");
    ot::test::qtApp();
    // Выключено — ни одного запроса, статус Disabled (адрес не важен: до сети дело не доходит).
    ot::QtHttpFetcher off(ot::NetworkAccess::Off);
    Request r = get("http://pki.acrobatya.test/root.crl");
    EXPECT_EQ(off.fetch(r).status, Status::Disabled);
    r.purpose = Purpose::General;
    EXPECT_EQ(off.fetch(r).status, Status::Disabled);
    // Только подписи — общие запросы запрещены.
    ot::QtHttpFetcher signaturesOnly(ot::NetworkAccess::SignaturesOnly);
    EXPECT_EQ(signaturesOnly.fetch(r).status, Status::Disabled);

    // Настройка trust/network.
    ot::test::TempDir dir;
    ot::Settings s(QString::fromStdWString((dir.path() / "settings.ini").wstring()));
    EXPECT_EQ(ot::networkAccess(s), ot::NetworkAccess::Allowed);
    s.set(ot::settings::kNetworkAccess, QStringLiteral("signaturesOnly"));
    EXPECT_EQ(ot::networkAccess(s), ot::NetworkAccess::SignaturesOnly);
    s.set(ot::settings::kNetworkAccess, QStringLiteral("off"));
    EXPECT_EQ(ot::networkAccess(s), ot::NetworkAccess::Off);
    EXPECT_TRUE(ot::Settings::allowed(ot::settings::kNetworkAccess,
                                      QString::fromUtf8(ot::settings::kNetworkAccess.def)));
}

TEST(http_fetcher, bad_urls) {
    ot::test::qtApp();
    ot::QtHttpFetcher f(ot::NetworkAccess::Allowed);
    EXPECT_EQ(f.fetch(get("ftp://example.invalid/x")).status, Status::BadUrl);
    EXPECT_EQ(f.fetch(get("not a url")).status, Status::BadUrl);
    EXPECT_EQ(f.fetch(get("file:///C:/Windows/win.ini")).status, Status::BadUrl);
    Request put = get("http://127.0.0.1:1/");
    put.method = "PUT";
    EXPECT_EQ(f.fetch(put).status, Status::BadUrl);
}

TEST(http_fetcher, scoped_instance) {
    ot::test::qtApp();
    ot::net::HttpFetcher& real = ot::httpFetcher();
    EXPECT_NE(dynamic_cast<ot::QtHttpFetcher*>(&real), nullptr);
    ot::net::FakeHttpFetcher fake;
    fake.addBody("http://pki.acrobatya.test/root.cer", {1, 2, 3}, "Application/PKIX-Cert; x=y");
    {
        ot::ScopedHttpFetcher scope(fake);
        EXPECT_EQ(&ot::httpFetcher(), &fake);
        const ot::net::Response r = ot::httpFetcher().fetch(get("http://pki.acrobatya.test/root.cer"));
        EXPECT_TRUE(r.ok());
        EXPECT_EQ(r.contentType, "application/pkix-cert");
        EXPECT_EQ(r.body, (std::vector<std::uint8_t>{1, 2, 3}));
        EXPECT_EQ(ot::httpFetcher().fetch(get("http://pki.acrobatya.test/other")).status,
                  Status::NetworkError);
    }
    EXPECT_EQ(&ot::httpFetcher(), &real);
    EXPECT_EQ(fake.requestCount(), 2U);
    EXPECT_EQ(fake.requests()[1].url, "http://pki.acrobatya.test/other");
}

TEST(http_fetcher, live_requests) {
    std::string reason;
    auto server = ot::test::TsaServer::start(&reason);
    if (!server) {
        GTEST_SKIP() << reason;
    }
    ot::QtHttpFetcher f(ot::NetworkAccess::Allowed);
    const ot::net::Response home = f.fetch(get(server->url("/")));
    EXPECT_TRUE(home.ok()) << home.error;
    EXPECT_EQ(std::string(home.body.begin(), home.body.end()), "AcroBatya test TSA\n");
    EXPECT_EQ(home.contentType, "text/plain");

    // Не 2xx — HttpError с кодом и телом.
    const ot::net::Response failed = f.fetch(tsaQuery(server->url("/http500")));
    EXPECT_EQ(failed.status, Status::HttpError);
    EXPECT_EQ(failed.httpCode, 500);
    EXPECT_FALSE(failed.ok());
    Request wrongType = tsaQuery(server->url("/tsr"));
    wrongType.contentType = "text/plain";
    const ot::net::Response unsupported = f.fetch(wrongType);
    EXPECT_EQ(unsupported.status, Status::HttpError);
    EXPECT_EQ(unsupported.httpCode, 415);
    // Соединение отвергнуто — NetworkError.
    EXPECT_EQ(f.fetch(get("http://127.0.0.1:1/")).status, Status::NetworkError);
}

TEST(http_fetcher, redirects_timeout_and_size) {
    std::string reason;
    auto server = ot::test::TsaServer::start(&reason);
    if (!server) {
        GTEST_SKIP() << reason;
    }
    ot::QtHttpFetcher f(ot::NetworkAccess::Allowed);
    // Тот же хост — перенаправление выполняется (307 сохраняет POST и тело: openssl отвечает 500 на мусор).
    const ot::net::Response same = f.fetch(tsaQuery(server->url("/redirect?to=" + server->url("/http500"))));
    EXPECT_EQ(same.httpCode, 500);
    EXPECT_EQ(same.finalUrl, server->url("/http500"));
    // Другой хост (localhost ≠ 127.0.0.1) — отказ, адрес отказа виден.
    const std::string other = "http://localhost:" + std::to_string(server->port()) + "/";
    const ot::net::Response refused = f.fetch(get(server->url("/redirect?to=" + other)));
    EXPECT_EQ(refused.status, Status::RedirectRefused);
    EXPECT_EQ(refused.finalUrl, other);
    // Без ограничения — можно и на другой хост (запрос дальше может не пройти — главное, не RedirectRefused).
    Request open = get(server->url("/redirect?to=" + other));
    open.sameHostRedirectsOnly = false;
    EXPECT_NE(f.fetch(open).status, Status::RedirectRefused);

    // Таймаут.
    Request slow = tsaQuery(server->url("/slow?ms=4000"));
    slow.timeoutMs = 700;
    const auto t0 = std::chrono::steady_clock::now();
    EXPECT_EQ(f.fetch(slow).status, Status::Timeout);
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::milliseconds(3000));

    // Предел размера ответа.
    Request big = get(server->url("/"));
    big.maxResponseBytes = 5;
    EXPECT_EQ(f.fetch(big).status, Status::TooLarge);
}
