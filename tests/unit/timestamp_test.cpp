// unit.timestamp — метки времени (план 4.2, п. 2; эталон 3.8 «Механика», «LTV и B-LTA», «Доверие»; sign-19,
// sign-20): B-T — токен службы времени PKI (в процессе за FakeHttpFetcher) лежит unsigned-атрибутом в CMS,
// imprint — хэш значения подписи, токен проверяется по цепочке до корня PKI, подпись цела; служба
// недоступна — без колбэка отказ (TimestampFailed, документ и файл как были), с колбэком «да» — подпись
// B-B; DocTimeStamp — отдельная секция: невидимое поле, /Type /DocTimeStamp, /SubFilter /ETSI.RFC3161, без
// /M, /Name, /Reason, /Location, /Reference, токен по хэшу /ByteRange; повторяемо — прежние подписи и метки
// целы; серверы времени — по умолчанию DigiCert, JSON без паролей, «проверить» — тестовый запрос.
// sign_compat.DISABLED_write_t пишет файлы для compat.sign.t и compat.sign.dts (pyHanko).
#include <gtest/gtest.h>

#include <QString>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/revisions.h"
#include "core/sign/cms.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/ossl.h"
#include "core/sign/signer.h"
#include "core/sign/tsa.h"
#include "core/sign/tsa_servers.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/tsa_settings.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::SignError;
using ot::sign::SignRequest;
using ot::sign::SignResult;
using ot::sign::SignStatus;
using ot::sign::TsaClient;
using ot::sign::TsaServer;
using ot::sign::TsaServers;
using ot::sign::TsaStatus;
using ot::test::DocView;
using ot::test::SigView;

namespace {

// Служба времени PKI в процессе.
std::shared_ptr<ot::net::FakeHttpFetcher> localTsa() {
    auto fake = std::make_shared<ot::net::FakeHttpFetcher>();
    fake->setHandler([](const ot::net::Request& r) {
        ot::net::Response resp;
        resp.status = ot::net::Status::Ok;
        resp.httpCode = 200;
        resp.contentType = "application/timestamp-reply";
        resp.body = ot::test::localTsaReply(r.body);
        resp.finalUrl = r.url;
        return resp;
    });
    return fake;
}

// TsaClient держит fetcher ссылкой — живут вместе.
struct Tsa {
    std::shared_ptr<ot::net::FakeHttpFetcher> fetcher;
    std::shared_ptr<TsaClient> client;
};

Tsa makeTsa(std::shared_ptr<ot::net::FakeHttpFetcher> fetcher = localTsa()) {
    ot::sign::TsaConfig config;
    config.url = "http://tsa.acrobatya.test/tsr";
    Tsa t;
    t.fetcher = std::move(fetcher);
    t.client = std::make_shared<TsaClient>(*t.fetcher, config);
    return t;
}

Tsa brokenTsa() {
    auto fake = std::make_shared<ot::net::FakeHttpFetcher>();
    ot::net::Response resp;
    resp.status = ot::net::Status::Ok;
    resp.httpCode = 500;
    resp.contentType = "text/plain";
    fake->add("http://tsa.acrobatya.test/tsr", resp);
    return makeTsa(fake);
}

SignRequest request(const Tsa& tsa) {
    SignRequest r;
    r.identity = ot::test::pkiIdentity("signer-rsa.pfx");
    r.reason = "Согласовано";
    r.placement.rect = ot::RectF{72, 72, 272, 132};
    r.timestamp = tsa.client;
    return r;
}

// Метка подписи: токен — unsigned-атрибут, imprint — SHA-256 значения подписи, цепочка TSA до корня PKI.
void expectSignatureTimestamp(const SigView& s) {
    const ot::sign::CmsInfo info = ot::sign::parseCms(s.contents);
    ASSERT_FALSE(info.timestampToken.empty()) << "в CMS нет id-aa-timeStampToken";
    const auto store = ot::test::pkiRootStore();
    std::string error;
    ot::sign::TimestampInfo ts;
    EXPECT_TRUE(ot::sign::verifyTimestampToken(info.timestampToken, ot::sign::ossl::sha256(info.signature),
                                               store.get(), &error, &ts))
        << error;
    EXPECT_TRUE(ts.trusted);
}

// Метка документа: /Contents — токен по хэшу /ByteRange.
void expectDocTimestamp(const fs::path& file, const SigView& s) {
    EXPECT_EQ(s.type, "DocTimeStamp");
    EXPECT_EQ(s.filter, "Adobe.PPKLite");
    EXPECT_EQ(s.subFilter, "ETSI.RFC3161");
    EXPECT_EQ(s.m, "");
    EXPECT_EQ(s.name, "");
    EXPECT_EQ(s.reason, "");
    EXPECT_EQ(s.location, "");
    EXPECT_FALSE(s.reference);
    for (float v : s.rect) {
        EXPECT_EQ(v, 0.0f);
    }
    const auto store = ot::test::pkiRootStore();
    std::string error;
    EXPECT_TRUE(ot::sign::verifyTimestampToken(s.contents, ot::sign::ossl::sha256Ranges(file, s.ranges),
                                               store.get(), &error))
        << error;
}

}  // namespace

TEST(timestamp, bt_signature_carries_verified_token) {
    OT_FEATURE("sign-19");
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"bt.pdf");
    const Tsa tsa = makeTsa();
    {
        auto doc = Document::openOrThrow(path);
        const SignResult r = ot::sign::signDocument(*doc, request(tsa));
        EXPECT_TRUE(r.timestamped);
        EXPECT_FALSE(r.timestamp.genTime.empty());
        EXPECT_EQ(r.timestamp.policy, "1.2.3.4.1");
        EXPECT_TRUE(r.timestampError.empty());
        EXPECT_LT(r.contentsSize, ot::sign::PdfSigner::kMaxContentsSize);
    }
    EXPECT_EQ(tsa.fetcher->requestCount(), 1u);
    const DocView view = ot::test::readSignatures(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    ot::test::expectCmsValid(path, view.sigs[0]);
    expectSignatureTimestamp(view.sigs[0]);
}

TEST(timestamp, unavailable_service_refuses_and_leaves_file) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"нет-службы.pdf");
    const std::string before = ot::paths::readFile(path);
    auto doc = Document::openOrThrow(path);
    try {
        (void)ot::sign::signDocument(*doc, request(brokenTsa()));
        ADD_FAILURE() << "без метки и без согласия — отказ";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::TimestampFailed);
        EXPECT_EQ(e.tsaStatus(), TsaStatus::HttpError);
    }
    EXPECT_EQ(ot::paths::readFile(path), before);
    EXPECT_FALSE(doc->isSigned());
    EXPECT_EQ(doc->undoStack().size(), 0);
    EXPECT_EQ(doc->pageCount(), 1);
}

TEST(timestamp, unavailable_service_with_consent_signs_bb) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"без-метки.pdf");
    const Tsa broken = brokenTsa();  // TsaClient держит fetcher ссылкой
    SignRequest r = request(broken);
    int asked = 0;
    r.withoutTimestamp = [&](const ot::sign::TsaError& e) {
        ++asked;
        EXPECT_EQ(e.status(), TsaStatus::HttpError);
        return true;
    };
    {
        auto doc = Document::openOrThrow(path);
        const SignResult res = ot::sign::signDocument(*doc, r);
        EXPECT_FALSE(res.timestamped);
        EXPECT_FALSE(res.timestampError.empty());
    }
    EXPECT_EQ(asked, 1);
    const DocView view = ot::test::readSignatures(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    ot::test::expectCmsValid(path, view.sigs[0]);
    EXPECT_TRUE(ot::sign::parseCms(view.sigs[0].contents).timestampToken.empty());
}

TEST(timestamp, document_timestamp_is_a_separate_increment) {
    OT_FEATURE("sign-20");
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"метка.pdf");
    const std::string before = ot::paths::readFile(path);
    const Tsa tsa = makeTsa();
    {
        auto doc = Document::openOrThrow(path);
        const ot::sign::DocTimestampResult r = ot::sign::addDocumentTimestamp(*doc, tsa.client);
        EXPECT_EQ(r.field, "Timestamp1");
        EXPECT_FALSE(r.timestamp.genTime.empty());
        EXPECT_EQ(r.save.mode, ot::SaveMode::Incremental);
        EXPECT_TRUE(doc->isSigned()) << "поле с /V — Saver пишет только инкремент";
    }
    const std::string after = ot::paths::readFile(path);
    EXPECT_EQ(after.compare(0, before.size(), before), 0);
    const DocView view = ot::test::readSignatures(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    EXPECT_EQ(view.sigs[0].field, "Timestamp1");
    EXPECT_GE(view.esicLevel, 1);
    EXPECT_TRUE(ot::test::coversFile(path, view.sigs[0]));
    expectDocTimestamp(path, view.sigs[0]);
}

TEST(timestamp, document_timestamps_repeat_after_signature) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"продление.pdf");
    const Tsa tsa = makeTsa();
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, request(tsa));
        EXPECT_EQ(ot::sign::addDocumentTimestamp(*doc, tsa.client).field, "Timestamp1");
        EXPECT_EQ(ot::sign::addDocumentTimestamp(*doc, tsa.client).field, "Timestamp2");
        EXPECT_EQ(ot::revisions::versionCount(*doc), 4);
    }
    const DocView view = ot::test::readSignatures(path);
    ASSERT_EQ(view.sigs.size(), 3u);
    EXPECT_EQ(view.esicLevel, 2) << "уровень CAdES не понижается";
    ot::test::expectCmsValid(path, view.sigs[0]);
    expectSignatureTimestamp(view.sigs[0]);
    expectDocTimestamp(path, view.sigs[1]);
    expectDocTimestamp(path, view.sigs[2]);
    EXPECT_FALSE(ot::test::coversFile(path, view.sigs[1]));
    EXPECT_TRUE(ot::test::coversFile(path, view.sigs[2]));
}

TEST(timestamp, document_timestamp_failure_keeps_document) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"сбой.pdf");
    const std::string before = ot::paths::readFile(path);
    auto doc = Document::openOrThrow(path);
    try {
        (void)ot::sign::addDocumentTimestamp(*doc, brokenTsa().client);
        ADD_FAILURE() << "служба недоступна";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::TimestampFailed);
    }
    EXPECT_EQ(ot::paths::readFile(path), before);
    EXPECT_FALSE(doc->isSigned());
}

TEST(timestamp, tsa_servers_defaults_json_and_edit) {
    TsaServers servers = TsaServers::defaults();
    ASSERT_NE(servers.defaultServer(), nullptr);
    EXPECT_EQ(servers.defaultServer()->name, "DigiCert");
    EXPECT_EQ(servers.defaultServer()->url, "http://timestamp.digicert.com");
    EXPECT_NE(servers.find("Sectigo"), nullptr);
    EXPECT_NE(servers.find("GlobalSign"), nullptr);

    TsaServer own;
    own.name = "Своя служба";
    own.url = "https://tsa.example.test/tsr";
    own.user = "иван";
    own.password = "секрет";
    EXPECT_TRUE(servers.add(own));
    EXPECT_FALSE(servers.add(own)) << "имя занято";
    TsaServer noUrl;
    noUrl.name = "Пусто";
    EXPECT_FALSE(servers.add(noUrl));
    EXPECT_TRUE(servers.setDefault("Своя служба"));
    EXPECT_FALSE(servers.setDefault("Нет такой"));

    const std::string json = servers.toJson();
    EXPECT_EQ(json.find("секрет"), std::string::npos) << "пароль — не в JSON";
    TsaServers back = TsaServers::fromJson(json);
    ASSERT_EQ(back.servers().size(), 4u);
    EXPECT_EQ(back.defaultServer()->name, "Своя служба");
    EXPECT_EQ(back.defaultServer()->user, "иван");
    EXPECT_TRUE(back.defaultServer()->password.empty());
    const ot::sign::TsaConfig config = TsaServers::config(*servers.find("Своя служба"));
    EXPECT_EQ(config.url, "https://tsa.example.test/tsr");
    EXPECT_EQ(config.password, "секрет");

    EXPECT_TRUE(back.remove("Своя служба"));
    EXPECT_EQ(back.defaultServer()->name, "DigiCert") << "по умолчанию — первый оставшийся";
    EXPECT_EQ(TsaServers::fromJson("{испорчено").servers().size(), 3u) << "испорченный JSON — умолчания";

    EXPECT_EQ(ot::sign::parseFallback("withoutTimestamp"), ot::sign::TimestampFallback::WithoutTimestamp);
    EXPECT_EQ(ot::sign::parseFallback("чужое"), ot::sign::TimestampFallback::Ask);
    EXPECT_STREQ(ot::sign::fallbackName(ot::sign::TimestampFallback::Cancel), "cancel");
}

TEST(timestamp, check_server_makes_test_request) {
    TsaServer local;
    local.name = "PKI";
    local.url = "http://tsa.acrobatya.test/tsr";
    auto fake = localTsa();
    const ot::sign::TsaCheck ok = ot::sign::checkTsaServer(*fake, local);
    EXPECT_TRUE(ok.ok) << ok.message;
    EXPECT_EQ(ok.status, TsaStatus::Ok);
    EXPECT_FALSE(ok.info.genTime.empty());
    EXPECT_EQ(fake->requestCount(), 1u);

    const Tsa broken = brokenTsa();
    const ot::sign::TsaCheck bad = ot::sign::checkTsaServer(*broken.fetcher, local);
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.status, TsaStatus::HttpError);
    EXPECT_EQ(bad.httpCode, 500);
}

// Настройки: signatures/tsaServers (пусто — умолчания), «служба недоступна», клиент службы по умолчанию.
TEST(timestamp, tsa_settings_roundtrip) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    ot::Settings settings(QString::fromStdWString((dir.path() / L"settings.ini").wstring()));
    EXPECT_EQ(ot::loadTsaServers(settings).defaultServer()->name, "DigiCert");
    EXPECT_EQ(ot::timestampFallback(settings), ot::sign::TimestampFallback::Ask);
    TsaServers servers = TsaServers::defaults();
    ASSERT_TRUE(servers.setDefault("Sectigo"));
    ot::saveTsaServers(settings, servers);
    settings.set(ot::settings::kTimestampFallback, QStringLiteral("cancel"));
    EXPECT_EQ(ot::loadTsaServers(settings).defaultName(), "Sectigo");
    EXPECT_EQ(ot::timestampFallback(settings), ot::sign::TimestampFallback::Cancel);
    const auto client = ot::defaultTsaClient(settings);
    ASSERT_NE(client, nullptr);
    EXPECT_EQ(client->config().url, "http://timestamp.sectigo.com");
}

// DocumentSession::timestampDocument — задача потока документа, документ переоткрыт.
TEST(timestamp, session_adds_document_timestamp) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"сессия-метка.pdf");
    const Tsa tsa = makeTsa();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(path.wstring())));
    const ot::sign::DocTimestampResult r = session.timestampDocument(tsa.client).result();
    EXPECT_TRUE(r.save.ok());
    EXPECT_EQ(r.field, "Timestamp1");
    EXPECT_TRUE(session.summary().isSigned);
    EXPECT_EQ(ot::test::readSignatures(path).sigs.size(), 1u);
}

// compat.sign.t: B-T (RSA, ECDSA) — pyHanko INTACT, TRUSTED, UNTOUCHED, TIMESTAMPED, TIMESTAMP_TRUSTED;
// compat.sign.dts: метка документа на неподписанном и после B-T — DOC_TIMESTAMP INTACT, TRUSTED.
TEST(sign_compat, DISABLED_write_t) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path out = fs::path(ot::paths::fromUtf8(outEnv)) / L"sign";
    const fs::path t = out / L"t";
    const fs::path dts = out / L"dts";
    for (const fs::path& d : {t, dts}) {
        fs::remove_all(d);
        fs::create_directories(d);
    }
    const Tsa tsa = makeTsa();
    const auto sign = [&](const fs::path& file, SignRequest r) {
        auto doc = Document::openOrThrow(file);
        (void)ot::sign::signDocument(*doc, r);
    };
    sign(ot::test::unsignedCopy(t, L"rsa-bt.pdf"), request(tsa));
    SignRequest ec = request(tsa);
    ec.identity = ot::test::pkiIdentity("signer-ec.pfx");
    sign(ot::test::unsignedCopy(t, L"ec-bt.pdf"), ec);

    {
        auto doc = Document::openOrThrow(ot::test::unsignedCopy(dts, L"doc-timestamp.pdf"));
        (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
    }
    const fs::path both = ot::test::unsignedCopy(dts, L"bt-then-doc-timestamp.pdf");
    sign(both, request(tsa));
    auto doc = Document::openOrThrow(both);
    (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
}
