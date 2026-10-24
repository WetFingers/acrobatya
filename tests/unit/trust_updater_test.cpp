// unit.trust_updater — автообновление списков доверия (план 4.5, п. 4; эталон 3.8 «Доверие»; sign-31): с
// подменой часов — не чаще раза в 14 дней (13-й день — без сети, 15-й — снова), «спрашивать перед установкой»
// — ожидающее до согласия, без вопроса — установка сразу, LOTL не из pins — согласие всегда; ключи trust/eutl
// и trust/aatl; неудача загрузки — Failed, попытка засчитана.
#include <gtest/gtest.h>

#include <QDateTime>
#include <QDir>
#include <QObject>
#include <QTimeZone>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/sign/trust_store.h"
#include "core/util/features.h"
#include "core/util/http_fetcher.h"
#include "core/util/paths.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/trust_lists.h"
#include "services/trust_updater.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::TrustUpdater;
using Outcome = ot::TrustUpdater::Outcome;

namespace {

std::string fixture(const wchar_t* name) {
    return ot::paths::readFile(ot::test::sourceDir() / L"tests" / L"fixtures" / L"trust" / name);
}

std::vector<std::uint8_t> bytes(const std::string& s) { return {s.begin(), s.end()}; }

std::string tlLocation() {
    return nlohmann::json::parse(fixture(L"expected.json"))["tl"]["location"].get<std::string>();
}

std::shared_ptr<ot::net::FakeHttpFetcher> listsNet(const std::string& lotl) {
    auto fake = std::make_shared<ot::net::FakeHttpFetcher>();
    fake->addBody(ot::kLotlUrl, bytes(lotl), "application/xml");
    fake->addBody(tlLocation(), bytes(fixture(L"TL-DE.xml")), "application/xml");
    fake->addBody(ot::kAatlUrl, bytes(fixture(L"not-a-list.acrobatsecuritysettings")), "application/pdf");
    return fake;
}

// Сигналы TrustUpdater (QtTest в unit_tests нет — свои счётчики).
struct Signals {
    struct Available {
        int eutl, aatl;
        bool consent;
    };
    std::vector<Available> available;
    std::vector<int> installed;
    explicit Signals(TrustUpdater& u) {
        QObject::connect(&u, &TrustUpdater::updateAvailable,
                         [this](int e, int a, bool c) { available.push_back({e, a, c}); });
        QObject::connect(&u, &TrustUpdater::installed, [this](int n) { installed.push_back(n); });
    }
};

struct Env {
    ot::test::TempDir dir;
    QString ini = QString::fromStdWString((dir.path() / L"settings.ini").wstring());
    QString trust = QString::fromStdWString((dir.path() / L"trust").wstring());
    ot::Settings settings{ini};
    QDateTime clock = QDateTime(QDate(2026, 10, 24), QTime(10, 0), QTimeZone::UTC);
};

}  // namespace

// Раз в 14 дней: первая проверка ходит в сеть, на 13-й день — нет, на 15-й — снова.
TEST(trust_updater, not_more_often_than_every_14_days) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    Env env;
    env.settings.set(ot::settings::kTrustAatl, false);
    const auto net = listsNet(fixture(L"eu-lotl.xml"));
    TrustUpdater updater(env.settings, *net, env.trust);
    updater.setClock([&] { return env.clock; });
    EXPECT_TRUE(updater.due());
    EXPECT_EQ(updater.check(), Outcome::Pending);
    EXPECT_EQ(net->requestCount(), 2u);
    EXPECT_EQ(updater.lastCheck(), env.clock);

    env.clock = env.clock.addDays(13);
    EXPECT_FALSE(updater.due());
    EXPECT_EQ(updater.check(), Outcome::NotDue);
    EXPECT_EQ(net->requestCount(), 2u);

    env.clock = env.clock.addDays(2);
    EXPECT_TRUE(updater.due());
    EXPECT_EQ(updater.check(), Outcome::Pending);
    EXPECT_EQ(net->requestCount(), 4u);
    // force — сразу, даже если рано.
    EXPECT_EQ(updater.check(true), Outcome::Pending);
    EXPECT_EQ(net->requestCount(), 6u);
}

// «Спрашивать перед установкой»: ожидающее и сигнал; установка — по согласию в каталог доверия и кеш.
TEST(trust_updater, ask_before_install) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    Env env;
    const auto net = listsNet(fixture(L"eu-lotl.xml"));
    TrustUpdater updater(env.settings, *net, env.trust);
    updater.setClock([&] { return env.clock; });
    const Signals sig(updater);
    EXPECT_EQ(updater.check(), Outcome::Pending);
    ASSERT_EQ(sig.available.size(), 1u);
    EXPECT_EQ(sig.available[0].eutl, 6);     // EUTL
    EXPECT_EQ(sig.available[0].aatl, 0);     // AATL — искусственный файл отклонён
    EXPECT_FALSE(sig.available[0].consent);  // согласия сверх обычного не нужно
    ASSERT_TRUE(updater.hasPending());
    ASSERT_TRUE(updater.pending().aatl.has_value());
    EXPECT_EQ(updater.pending().aatl->status, ot::sign::AatlStatus::NotATrustList);
    EXPECT_FALSE(fs::exists(fs::path(env.trust.toStdWString()) / L"anchors.json"));
    EXPECT_EQ(updater.install(), 6);
    EXPECT_FALSE(updater.hasPending());
    EXPECT_EQ(sig.installed.size(), 1u);
    const ot::sign::TrustStore store = ot::sign::TrustStore::load(fs::path(env.trust.toStdWString()));
    EXPECT_EQ(store.anchors().size(), 6u);
    EXPECT_TRUE(fs::exists(fs::path(env.trust.toStdWString()) / L"eutl.json"));
    EXPECT_FALSE(fs::exists(fs::path(env.trust.toStdWString()) / L"aatl.json"));
}

// Без вопроса — установка сразу; LOTL не из pins — всё равно ждёт согласия.
TEST(trust_updater, install_without_asking_unless_unpinned) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    Env env;
    env.settings.set(ot::settings::kTrustListsAsk, false);
    {
        const auto net = listsNet(fixture(L"eu-lotl.xml"));
        TrustUpdater updater(env.settings, *net, env.trust);
        updater.setClock([&] { return env.clock; });
        EXPECT_EQ(updater.check(), Outcome::Installed);
        EXPECT_EQ(ot::sign::TrustStore::load(fs::path(env.trust.toStdWString())).anchors().size(), 6u);
    }
    // Подпись LOTL — чужим сертификатом (сертификат подписи заменён сертификатом PKI).
    std::string lotl = fixture(L"eu-lotl.xml");
    const std::size_t sig = lotl.find("<ds:Signature");
    const std::size_t a = lotl.find("X509Certificate>", sig) + 16;
    const std::size_t b = lotl.find('<', a);
    const std::string pem = ot::test::readPki("inter.pem");
    std::string b64;
    for (std::size_t i = pem.find('\n') + 1; i < pem.size() && pem.compare(i, 5, "-----") != 0; ++i) {
        if (pem[i] != '\n' && pem[i] != '\r') {
            b64 += pem[i];
        }
    }
    lotl.replace(a, b - a, b64);
    const auto net = listsNet(lotl);
    TrustUpdater updater(env.settings, *net, env.trust);
    updater.setClock([&] { return env.clock.addDays(20); });
    const Signals spy(updater);
    EXPECT_EQ(updater.check(), Outcome::Pending);
    ASSERT_EQ(spy.available.size(), 1u);
    EXPECT_TRUE(spy.available[0].consent);
    EXPECT_TRUE(updater.pending().needsConsent());
    updater.discard();
    EXPECT_FALSE(updater.hasPending());
}

// Выключено — без сети; сеть недоступна — Failed, попытка засчитана (следующая — через 14 дней).
TEST(trust_updater, disabled_and_failed) {
    OT_FEATURE("sign-31");
    ot::test::qtApp();
    Env env;
    auto offline = std::make_shared<ot::net::FakeHttpFetcher>();
    TrustUpdater updater(env.settings, *offline, env.trust);
    updater.setClock([&] { return env.clock; });
    env.settings.set(ot::settings::kTrustEutl, false);
    env.settings.set(ot::settings::kTrustAatl, false);
    EXPECT_EQ(updater.check(), Outcome::Disabled);
    EXPECT_EQ(offline->requestCount(), 0u);
    env.settings.set(ot::settings::kTrustEutl, true);
    EXPECT_EQ(updater.check(), Outcome::Failed);
    EXPECT_EQ(offline->requestCount(), 1u);
    EXPECT_FALSE(updater.due());
    EXPECT_EQ(updater.check(), Outcome::NotDue);
}
