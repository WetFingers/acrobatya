// unit.eutl — доверенные списки ЕС (план 4.5, п. 4; эталон 3.8 «Доверие»; sign-31): разбор урезанного LOTL и
// TL-DE (tests/fixtures/trust, tools/gen/make_trust_fixtures.py из снимка корпуса trust) QXmlStreamReader-ом,
// сборка якорей через HttpTrustListSource и FakeHttpFetcher — N сертификатов из expected.json, подписант LOTL
// — из pinned-списка ресурса :/trust/lotl_pins.json; просроченный NextUpdate, подписант TL вне указателя
// LOTL, чужие pins и недоступный TL — помечены; замена якорей источника не трогает свои. compat.trust.live
// (OTTISK_LIVE_NET=1) — настоящий LOTL: подписант по-прежнему из pins.
#include <gtest/gtest.h>

#include <QByteArray>
#include <QFile>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "core/sign/eutl.h"
#include "core/sign/ossl.h"
#include "core/sign/trust_store.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "core/util/paths.h"
#include "services/http_fetcher.h"
#include "services/trust_lists.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::sign::EutlResult;
using ot::sign::ListIssue;

namespace {

fs::path trustDir() { return ot::test::sourceDir() / L"tests" / L"fixtures" / L"trust"; }

std::string readFixture(const wchar_t* name) { return ot::paths::readFile(trustDir() / name); }

const nlohmann::json& expected() {
    static const nlohmann::json j = nlohmann::json::parse(readFixture(L"expected.json"));
    return j;
}

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

// Сеть со списками фикстуры: LOTL по адресу ЕС, TL-DE — по адресу из указателя.
std::shared_ptr<ot::net::FakeHttpFetcher> listsNet(const std::string& lotl, const std::string& tl) {
    auto fake = std::make_shared<ot::net::FakeHttpFetcher>();
    fake->addBody(ot::kLotlUrl, bytes(lotl), "application/vnd.etsi.tsl+xml");
    fake->addBody(expected()["tl"]["location"].get<std::string>(), bytes(tl), "application/vnd.etsi.tsl+xml");
    return fake;
}

std::time_t snapshotTime() { return ot::sign::isoToTime("2026-10-24T10:00:00Z"); }

EutlResult build(ot::net::HttpFetcher& net, const ot::sign::LotlPins& pins,
                 std::time_t now = snapshotTime()) {
    ot::HttpTrustListSource source(net);
    ot::sign::EutlOptions o;
    o.now = now;
    return ot::sign::buildEutl(source, pins, o);
}

std::string sha(const ot::sign::Bytes& der) { return ot::sign::ossl::toHex(ot::sign::ossl::sha256(der)); }

}  // namespace

// Разбор XML: номер, даты, указатели, подписанты; службы TL-DE без истории.
TEST(eutl, parses_lotl_and_trusted_list) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    const ot::sign::Lotl lotl = ot::parseLotl(QByteArray::fromStdString(readFixture(L"eu-lotl.xml")));
    EXPECT_EQ(lotl.sequence, expected()["lotl"]["sequence"].get<int>());
    EXPECT_EQ(lotl.issued, expected()["lotl"]["issued"].get<std::string>());
    EXPECT_EQ(lotl.nextUpdate, expected()["lotl"]["nextUpdate"].get<std::string>());
    ASSERT_EQ(lotl.pointers.size(), 2u);
    ASSERT_EQ(lotl.signers.size(), 1u);
    EXPECT_EQ(sha(lotl.signers[0]), expected()["lotl"]["signer"].get<std::string>());
    const auto de = std::find_if(lotl.pointers.begin(), lotl.pointers.end(),
                                 [](const ot::sign::ListPointer& p) { return p.territory == "DE"; });
    ASSERT_NE(de, lotl.pointers.end());
    EXPECT_EQ(de->location, expected()["tl"]["location"].get<std::string>());
    EXPECT_EQ(de->mimeType, "application/vnd.etsi.tsl+xml");
    EXPECT_EQ(de->signers.size(), 2u);

    const ot::sign::TrustedList tl =
        ot::parseTrustedList(QByteArray::fromStdString(readFixture(L"TL-DE.xml")));
    EXPECT_EQ(tl.territory, "DE");
    EXPECT_EQ(tl.sequence, expected()["tl"]["sequence"].get<int>());
    EXPECT_EQ(tl.nextUpdate, expected()["tl"]["nextUpdate"].get<std::string>());
    ASSERT_EQ(tl.signers.size(), 1u);
    EXPECT_EQ(sha(tl.signers[0]), expected()["tl"]["signer"].get<std::string>());
    EXPECT_FALSE(tl.services.empty());
    for (const ot::sign::TrustService& s : tl.services) {
        EXPECT_FALSE(s.provider.empty());
        EXPECT_FALSE(s.type.empty());
        EXPECT_FALSE(s.status.empty());
    }
    EXPECT_THROW((void)ot::parseLotl(QByteArray("<html>не список</html>")), std::runtime_error);
    EXPECT_THROW((void)ot::parseTrustedList(QByteArray("<TrustServiceStatusList><")), std::runtime_error);
}

// Фикстура даёт N якорей из expected.json; LOTL pinned и свежий; флаги — подписи и сертификация.
TEST(eutl, fixture_gives_expected_anchors) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    const auto net = listsNet(readFixture(L"eu-lotl.xml"), readFixture(L"TL-DE.xml"));
    const ot::sign::LotlPins pins = ot::lotlPins();
    EXPECT_EQ(pins.url, ot::kLotlUrl);
    EXPECT_FALSE(pins.snapshot.empty());
    const EutlResult r = build(*net, pins);
    EXPECT_TRUE(r.pinned);
    EXPECT_FALSE(r.lotlExpired);
    EXPECT_TRUE(r.trustworthy());
    ASSERT_EQ(r.territories.size(), 1u);
    EXPECT_EQ(r.territories[0].territory, "DE");
    EXPECT_EQ(r.territories[0].issue, ListIssue::None);
    std::vector<std::string> got;
    for (const ot::sign::ListedCertificate& c : r.certificates) {
        got.push_back(sha(c.der));
        EXPECT_EQ(c.flags, ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
        EXPECT_EQ(c.label.rfind("DE: ", 0), 0u) << c.label;
    }
    std::sort(got.begin(), got.end());
    const auto want = expected()["anchors"].get<std::vector<std::string>>();
    EXPECT_EQ(got, want);
    EXPECT_EQ(r.territories[0].certificates, static_cast<int>(want.size()));
    // Указатель самого LOTL (EU) в сеть не ходит — два запроса.
    EXPECT_EQ(net->requestCount(), 2u);
}

// Просроченный NextUpdate LOTL и TL — помечены, применять только по согласию.
TEST(eutl, expired_lists_are_flagged) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    const auto net = listsNet(readFixture(L"eu-lotl.xml"), readFixture(L"TL-DE.xml"));
    const EutlResult r = build(*net, ot::lotlPins(), ot::sign::isoToTime("2027-05-01T00:00:00Z"));
    EXPECT_TRUE(r.lotlExpired);
    EXPECT_FALSE(r.trustworthy());
    ASSERT_EQ(r.territories.size(), 1u);
    EXPECT_EQ(r.territories[0].issue, ListIssue::Expired);
}

// Подписант TL не объявлен в указателе LOTL — список отклонён; чужие pins — LOTL не pinned; TL недоступен.
TEST(eutl, signer_pins_and_availability) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    // Подменить сертификат подписи TL-DE сертификатом тестовой PKI.
    std::string tl = readFixture(L"TL-DE.xml");
    const std::size_t sig = tl.find("<ds:Signature");
    const std::size_t a = tl.find("X509Certificate>", sig) + 16;
    const std::size_t b = tl.find('<', a);
    const std::string pem = ot::test::readPki("root.pem");
    std::string b64;
    for (std::size_t i = pem.find('\n') + 1; i < pem.size() && pem.compare(i, 5, "-----") != 0; ++i) {
        if (pem[i] != '\n' && pem[i] != '\r') {
            b64 += pem[i];
        }
    }
    tl.replace(a, b - a, b64);
    const auto net = listsNet(readFixture(L"eu-lotl.xml"), tl);
    const EutlResult r = build(*net, ot::lotlPins());
    ASSERT_EQ(r.territories.size(), 1u);
    EXPECT_EQ(r.territories[0].issue, ListIssue::SignerNotDeclared);
    EXPECT_TRUE(r.certificates.empty());
    EXPECT_FALSE(r.trustworthy());

    const auto good = listsNet(readFixture(L"eu-lotl.xml"), readFixture(L"TL-DE.xml"));
    const EutlResult unpinned = build(*good, ot::sign::LotlPins{});
    EXPECT_FALSE(unpinned.pinned);
    EXPECT_FALSE(unpinned.trustworthy());

    auto missing = std::make_shared<ot::net::FakeHttpFetcher>();
    missing->addBody(ot::kLotlUrl, bytes(readFixture(L"eu-lotl.xml")), "application/xml");
    const EutlResult r404 = build(*missing, ot::lotlPins());
    ASSERT_EQ(r404.territories.size(), 1u);
    EXPECT_EQ(r404.territories[0].issue, ListIssue::Unavailable);
    EXPECT_TRUE(r404.certificates.empty());

    auto none = std::make_shared<ot::net::FakeHttpFetcher>();
    EXPECT_THROW((void)build(*none, ot::lotlPins()), std::runtime_error);
}

// Установка: якоря EUTL заменяются целиком, свой якорь остаётся своим; кеш eutl.json читается обратно.
TEST(eutl, install_replaces_source_and_keeps_user_anchors) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    ot::test::TempDir dir;
    const auto net = listsNet(readFixture(L"eu-lotl.xml"), readFixture(L"TL-DE.xml"));
    ot::TrustListUpdate u = ot::downloadTrustLists(*net, true, false, snapshotTime());
    ASSERT_TRUE(u.eutl.has_value()) << u.eutlError;
    ot::sign::TrustStore store;
    const std::string root = ot::test::readPki("root.pem");
    ASSERT_TRUE(store.add(ot::sign::Bytes(root.begin(), root.end())));
    // Свой якорь, совпадающий с якорем списка, остаётся своим.
    ASSERT_TRUE(store.add(u.eutl->certificates[0].der));
    const QString qdir = QString::fromStdWString(dir.path().wstring());
    const int n = ot::installTrustLists(u, store, qdir);
    EXPECT_EQ(n, static_cast<int>(u.eutl->certificates.size()) - 1);
    EXPECT_EQ(store.count(ot::sign::kTrustSignatures), static_cast<int>(u.eutl->certificates.size()) + 1);
    int eutl = 0;
    for (const ot::sign::TrustAnchor& a : store.anchors()) {
        eutl += a.source == ot::sign::TrustSource::Eutl ? 1 : 0;
    }
    EXPECT_EQ(eutl, n);
    const auto cache = ot::loadTrustListCache(qdir, QStringLiteral("eutl"));
    ASSERT_TRUE(cache.has_value());
    EXPECT_EQ(cache->certificates.size(), u.eutl->certificates.size());
    EXPECT_EQ(cache->sequence, expected()["lotl"]["sequence"].get<int>());
    // Повторная установка того же — то же число якорей, без повторов.
    (void)ot::installTrustLists(u, store, qdir);
    EXPECT_EQ(store.count(ot::sign::kTrustSignatures), static_cast<int>(u.eutl->certificates.size()) + 1);
    const ot::sign::TrustStore reloaded = ot::sign::TrustStore::load(dir.path());
    EXPECT_EQ(reloaded.anchors().size(), store.anchors().size());
}

// compat.trust.live: настоящий LOTL (OTTISK_LIVE_NET=1) — подписант из pinned-списка, указателей больше 25.
TEST(trust_live, DISABLED_lotl_pins_match_live_list) {
    const char* live = std::getenv("OTTISK_LIVE_NET");  // NOLINT(concurrency-mt-unsafe)
    if (live == nullptr || std::string(live) != "1") {
        GTEST_SKIP() << "OTTISK_LIVE_NET=1 is not set";
    }
    ot::test::qtApp();
    ot::QtHttpFetcher net(ot::NetworkAccess::Allowed);
    ot::HttpTrustListSource source(net);
    const ot::sign::Lotl lotl = source.lotl();
    const ot::sign::LotlPins pins = ot::lotlPins();
    bool pinned = false;
    for (const ot::sign::Bytes& der : lotl.signers) {
        pinned = pinned || pins.contains(der);
    }
    EXPECT_TRUE(pinned) << "LOTL " << lotl.sequence
                        << " is signed by a certificate outside assets/trust/lotl_pins.json"
                        << " (snapshot " << pins.snapshot << "): refresh the pins from the Official Journal";
    EXPECT_GT(lotl.pointers.size(), 25u);
}
