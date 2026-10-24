// unit.protected_view.* и unit.link_policy.* — защищённый просмотр и доверие ссылкам без окна (план 2.5b;
// эталон 7.4, 7.5; security-35, 36, 37, 42): поток Zone.Identifier (Mark-of-the-Web) читается — ZoneId ≥ 3
// делает файл «чужим», 2 и ниже — нет; временные папки и PDF-вложения — тоже чужие; «Все файлы» — каждый,
// «Выкл.» — никакой; привилегированные папки и файлы, для которых нажали «Включить все функции» (state.sqlite
// по отпечатку содержимого), — свои. Ссылки: сайт не из списка — спросить, «Разрешить для сайта» и
// «Запретить» запоминаются по хосту, /Launch запрещён всегда, в защищённом просмотре внешнее не открывается.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

#include <fstream>

#include "core/links/links.h"
#include "core/util/features.h"
#include "services/link_policy.h"
#include "services/protected_view.h"
#include "services/settings.h"
#include "services/state_db.h"
#include "support/qt_app.h"

namespace pv = ot::protected_view;
using ot::HostRule;
using ot::LinkDecision;
using ot::LinkTrust;
using ot::Settings;
using ot::StateDb;

namespace {

QString writeFile(const QDir& dir, const QString& name, const QByteArray& bytes) {
    const QString path = dir.filePath(name);
    QFile f(path);
    EXPECT_TRUE(f.open(QIODevice::WriteOnly));
    f.write(bytes);
    return path;
}

// Поток Zone.Identifier в UTF-16 с BOM (так его пишут некоторые почтовые программы).
void writeUtf16Zone(const QString& path, int zone) {
    std::ofstream out(path.toStdWString() + L":Zone.Identifier", std::ios::binary);
    const QString text = QStringLiteral("[ZoneTransfer]\r\nZoneId=%1\r\n").arg(zone);
    out.write("\xFF\xFE", 2);
    out.write(reinterpret_cast<const char*>(text.utf16()), text.size() * 2);
}

struct Env {
    QTemporaryDir root;
    QDir dir{root.path()};
    Settings settings{root.filePath(QStringLiteral("settings.ini"))};
    StateDb db{root.filePath(QStringLiteral("state.sqlite"))};
    Env() {
        ot::test::qtApp();
        pv::setTempFoldersForTesting(QStringList{});  // папка теста сама лежит в %TEMP%
    }
    ~Env() { pv::setTempFoldersForTesting(std::nullopt); }
    pv::Origin check(const QString& path, bool attachment = false) {
        return pv::check(path, attachment, settings, db);
    }
};

}  // namespace

TEST(protected_view, zone_identifier_marks_internet_files) {
    OT_FEATURE("security-35");
    Env env;
    ASSERT_TRUE(env.db.isOpen());
    const QString plain = writeFile(env.dir, QStringLiteral("plain.pdf"), "%PDF-1.7 plain");
    EXPECT_EQ(pv::zoneId(plain), -1);
    EXPECT_EQ(env.check(plain), pv::Origin::None);

    const QString web = writeFile(env.dir, QStringLiteral("скачан.pdf"), "%PDF-1.7 web");
    ASSERT_TRUE(pv::writeZoneIdentifier(web, 3, QStringLiteral("https://example.org/a.pdf")));
    EXPECT_EQ(pv::zoneId(web), 3);
    EXPECT_EQ(env.check(web), pv::Origin::Internet);

    const QString untrusted = writeFile(env.dir, QStringLiteral("zone4.pdf"), "%PDF-1.7 z4");
    writeUtf16Zone(untrusted, 4);
    EXPECT_EQ(pv::zoneId(untrusted), 4);
    EXPECT_EQ(env.check(untrusted), pv::Origin::Internet);

    const QString intranet = writeFile(env.dir, QStringLiteral("intranet.pdf"), "%PDF-1.7 z2");
    ASSERT_TRUE(pv::writeZoneIdentifier(intranet, 2));
    EXPECT_EQ(pv::zoneId(intranet), 2);
    EXPECT_EQ(env.check(intranet), pv::Origin::None);

    EXPECT_EQ(pv::zoneId(env.dir.filePath(QStringLiteral("нет файла.pdf"))), -1);
}

TEST(protected_view, modes_attachments_and_temp_folders) {
    Env env;
    const QString plain = writeFile(env.dir, QStringLiteral("plain.pdf"), "%PDF-1.7 plain");
    const QString web = writeFile(env.dir, QStringLiteral("web.pdf"), "%PDF-1.7 web");
    pv::writeZoneIdentifier(web, 3);

    EXPECT_EQ(env.check(plain, true), pv::Origin::Attachment);
    pv::setTempFoldersForTesting(QStringList{env.root.path()});
    EXPECT_EQ(env.check(plain), pv::Origin::TempFolder);
    pv::setTempFoldersForTesting(QStringList{});

    env.settings.set(ot::settings::kProtectedView, QStringLiteral("all"));
    EXPECT_EQ(env.check(plain), pv::Origin::AllFiles);
    env.settings.set(ot::settings::kProtectedView, QStringLiteral("off"));
    EXPECT_EQ(env.check(web), pv::Origin::None);
    EXPECT_EQ(env.check(plain, true), pv::Origin::None);
    env.settings.set(ot::settings::kProtectedView, QStringLiteral("unsafe"));
    EXPECT_EQ(env.check(web), pv::Origin::Internet);
}

TEST(protected_view, trusted_folders_and_enable_all_features) {
    OT_FEATURE("security-37");
    Env env;
    QDir().mkpath(env.dir.filePath(QStringLiteral("Доверенная/вложенная")));
    const QString inside =
        writeFile(env.dir, QStringLiteral("Доверенная/вложенная/web.pdf"), "%PDF-1.7 trusted folder");
    pv::writeZoneIdentifier(inside, 3);
    EXPECT_EQ(env.check(inside), pv::Origin::Internet);
    env.settings.set(ot::settings::kTrustedFolders,
                     QStringList{env.dir.filePath(QStringLiteral("Доверенная"))});
    EXPECT_EQ(env.check(inside), pv::Origin::None);
    // Похожее имя папки — не она.
    QDir().mkpath(env.dir.filePath(QStringLiteral("Доверенная2")));
    const QString beside = writeFile(env.dir, QStringLiteral("Доверенная2/web.pdf"), "%PDF-1.7 beside");
    pv::writeZoneIdentifier(beside, 3);
    EXPECT_EQ(env.check(beside), pv::Origin::Internet);

    // «Включить все функции»: файл свой по отпечатку содержимого; Zone.Identifier не трогается.
    const QString web = writeFile(env.dir, QStringLiteral("web.pdf"), "%PDF-1.7 enable all features");
    pv::writeZoneIdentifier(web, 3);
    EXPECT_FALSE(pv::isTrusted(web, env.db));
    ASSERT_TRUE(pv::trust(web, env.db));
    EXPECT_TRUE(pv::isTrusted(web, env.db));
    EXPECT_EQ(env.check(web), pv::Origin::None);
    EXPECT_EQ(pv::zoneId(web), 3);
    // Тот же файл в другом месте — тот же отпечаток; другое содержимое — другой.
    const QString copy = writeFile(env.dir, QStringLiteral("копия.pdf"), "%PDF-1.7 enable all features");
    pv::writeZoneIdentifier(copy, 3);
    EXPECT_EQ(pv::fileKey(copy), pv::fileKey(web));
    EXPECT_EQ(env.check(copy), pv::Origin::None);
    const QString other = writeFile(env.dir, QStringLiteral("другой.pdf"), "%PDF-1.7 something else");
    pv::writeZoneIdentifier(other, 3);
    EXPECT_NE(pv::fileKey(other), pv::fileKey(web));
    EXPECT_EQ(env.check(other), pv::Origin::Internet);
    EXPECT_TRUE(pv::fileKey(env.dir.filePath(QStringLiteral("нет.pdf"))).isEmpty());
}

TEST(link_policy, hosts_are_remembered_and_defaults_follow_settings) {
    OT_FEATURE("security-42");
    Env env;
    LinkTrust trust(env.db, env.settings);
    EXPECT_EQ(LinkTrust::hostKey(QUrl(QStringLiteral("https://Example.ORG/a?b=1"))),
              QStringLiteral("example.org"));
    EXPECT_EQ(trust.rule(QStringLiteral("example.org")), HostRule::Ask);
    trust.setRule(QStringLiteral("example.org"), HostRule::Allow);
    trust.setRule(QStringLiteral("bad.example"), HostRule::Block);
    EXPECT_EQ(trust.rule(QStringLiteral("example.org")), HostRule::Allow);
    EXPECT_EQ(trust.rule(QStringLiteral("bad.example")), HostRule::Block);
    EXPECT_EQ(trust.hosts().size(), 2);
    {
        LinkTrust again(env.db, env.settings);  // память — в state.sqlite
        EXPECT_EQ(again.rule(QStringLiteral("example.org")), HostRule::Allow);
    }
    trust.setRule(QStringLiteral("bad.example"), HostRule::Ask);  // убрать из списка
    EXPECT_EQ(trust.hosts().size(), 1);
    env.settings.set(ot::settings::kInternetAccess, QStringLiteral("blockAll"));
    EXPECT_EQ(trust.rule(QStringLiteral("new.example")), HostRule::Block);
    EXPECT_EQ(trust.rule(QStringLiteral("example.org")), HostRule::Allow);  // список важнее
    env.settings.set(ot::settings::kInternetAccess, QStringLiteral("allowAll"));
    EXPECT_EQ(trust.rule(QStringLiteral("new.example")), HostRule::Allow);
}

TEST(link_policy, decisions_by_kind) {
    OT_FEATURE("security-36");
    Env env;
    LinkTrust trust(env.db, env.settings);
    auto action = [](ot::links::LinkKind kind, const char* uri) {
        ot::links::LinkAction a;
        a.kind = kind;
        a.uri = uri;
        return a;
    };
    using ot::links::LinkKind;
    const auto web = action(LinkKind::Uri, "https://example.org/akrobatya");
    EXPECT_EQ(ot::decideLink(web, false, trust), LinkDecision::AskUrl);
    trust.setRule(QStringLiteral("example.org"), HostRule::Allow);
    EXPECT_EQ(ot::decideLink(web, false, trust), LinkDecision::OpenUrl);
    trust.setRule(QStringLiteral("example.org"), HostRule::Block);
    EXPECT_EQ(ot::decideLink(web, false, trust), LinkDecision::BlockedUrl);
    EXPECT_EQ(ot::decideLink(web, true, trust), LinkDecision::ProtectedView);
    EXPECT_EQ(ot::decideLink(action(LinkKind::Uri, "mailto:a@example.org"), false, trust),
              LinkDecision::AskUrl);
    EXPECT_EQ(ot::decideLink(action(LinkKind::Uri, "javascript:alert(1)"), false, trust),
              LinkDecision::Unsupported);
    EXPECT_EQ(ot::decideLink(action(LinkKind::Uri, "file:///C:/Windows/notepad.exe"), false, trust),
              LinkDecision::Unsupported);
    auto remote = action(LinkKind::GoToR, "file:other.pdf#page=2");
    remote.file = "other.pdf";
    EXPECT_EQ(ot::decideLink(remote, false, trust), LinkDecision::AskFile);
    EXPECT_EQ(ot::decideLink(remote, true, trust), LinkDecision::ProtectedView);
    auto launch = action(LinkKind::Launch, "file:calc.exe");
    launch.file = "calc.exe";
    EXPECT_EQ(ot::decideLink(launch, false, trust), LinkDecision::Forbidden);
    EXPECT_EQ(ot::decideLink(action(LinkKind::GoTo, "#page=2"), true, trust), LinkDecision::Navigate);
    EXPECT_EQ(ot::decideLink(action(LinkKind::Named, ""), false, trust), LinkDecision::Navigate);
    EXPECT_EQ(ot::decideLink(action(LinkKind::Unsupported, ""), false, trust), LinkDecision::Unsupported);
}
