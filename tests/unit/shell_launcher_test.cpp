// unit.shell_launcher.* — оболочка Windows только через ShellLauncher (план 2.4; эталон 7.3, 7.4): фейк
// записывает вызовы и ничего не открывает, подмена на время области видимости возвращает прежний; настоящий
// без живого рабочего стола (QCoreApplication, offscreen) ничего не открывает и отвечает false — тест не
// откроет браузер и Проводник на машине человека.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QUrl>

#include "core/util/features.h"
#include "services/shell_launcher.h"
#include "support/qt_app.h"

using ot::FakeShellLauncher;
using ot::ScopedShellLauncher;
using ot::ShellLauncher;

TEST(shell_launcher, fake_records_calls) {
    OT_FEATURE("security-43");
    ot::test::qtApp();
    FakeShellLauncher fake;
    {
        const ScopedShellLauncher scoped(fake);
        EXPECT_EQ(&ShellLauncher::instance(), &fake);
        EXPECT_TRUE(ShellLauncher::instance().openUrl(QUrl(QStringLiteral("https://example.org/a"))));
        EXPECT_TRUE(ShellLauncher::instance().openFile(QStringLiteral("C:/tmp/report.docx")));
        EXPECT_TRUE(ShellLauncher::instance().openSettings(QStringLiteral("defaultapps")));
        fake.result = false;
        EXPECT_FALSE(ShellLauncher::instance().showInExplorer(QStringLiteral("C:/tmp/a.pdf")));
    }
    EXPECT_NE(&ShellLauncher::instance(), &fake);
    ASSERT_EQ(fake.calls.size(), 4);
    EXPECT_EQ(fake.calls[0].kind, QStringLiteral("openUrl"));
    EXPECT_EQ(fake.calls[0].target, QStringLiteral("https://example.org/a"));
    EXPECT_EQ(fake.calls[1].kind, QStringLiteral("openFile"));
    EXPECT_EQ(fake.calls[1].target, QStringLiteral("C:/tmp/report.docx"));
    EXPECT_EQ(fake.calls[2].kind, QStringLiteral("openSettings"));
    EXPECT_EQ(fake.calls[3].kind, QStringLiteral("showInExplorer"));
}

TEST(shell_launcher, real_refuses_without_live_desktop) {
    ot::test::qtApp();  // QCoreApplication — рабочего стола нет
    EXPECT_FALSE(ShellLauncher::liveDesktop());
    ShellLauncher& real = ShellLauncher::instance();
    EXPECT_FALSE(real.openUrl(QUrl(QStringLiteral("https://example.org/"))));
    EXPECT_FALSE(real.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/view/nav.json")));
    EXPECT_FALSE(real.openSettings(QStringLiteral("defaultapps")));
    EXPECT_FALSE(real.showInExplorer(QStringLiteral(OTTISK_SOURCE_DIR "/CLAUDE.md")));
}

TEST(shell_launcher, nested_scopes_restore_in_order) {
    FakeShellLauncher a;
    FakeShellLauncher b;
    {
        const ScopedShellLauncher sa(a);
        {
            const ScopedShellLauncher sb(b);
            EXPECT_EQ(&ShellLauncher::instance(), &b);
        }
        EXPECT_EQ(&ShellLauncher::instance(), &a);
    }
    ShellLauncher::instance().openFile(QStringLiteral("x"));  // настоящий: без рабочего стола — отказ
    EXPECT_TRUE(a.calls.isEmpty());
    EXPECT_TRUE(b.calls.isEmpty());
}
