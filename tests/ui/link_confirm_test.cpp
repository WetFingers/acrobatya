// ui.link_confirm — ссылки наружу в окне (план 2.5b; эталон 7.4, 7.6; security-36, security-42): /URI —
// «Предупреждение системы безопасности» с адресом; «Открыть» — один раз (в следующий раз снова вопрос),
// «Разрешить для сайта» — запоминается, дальше без вопроса, вызов уходит в ShellLauncher (FakeShellLauncher);
// «Запретить» — запоминается, браузер не открывается; /Launch не выполняется никогда и без вопроса; /GoToR —
// «Документ пытается открыть другой файл» и вкладка на странице назначения. Ссылка в виде (фокус Tab,
// activateLink — как Enter и щелчок) идёт тем же путём.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/link_policy.h"
#include "services/shell_launcher.h"
#include "support/ui_test.h"
#include "ui/dialogs/link_confirm_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/link_flow.h"
#include "ui/shell/main_window.h"
#include "ui/view/link_layer.h"
#include "ui/view/page_view.h"

using ot::LinkDecision;
using ot::ui::DocumentView;
using ot::ui::LinkConfirmDialog;
using ot::ui::MainWindow;
using Choice = ot::ui::LinkConfirmDialog::Choice;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

ot::links::LinkAction uri(const char* address) {
    ot::links::LinkAction a;
    a.kind = ot::links::LinkKind::Uri;
    a.uri = address;
    return a;
}

struct Prompts {
    QList<LinkConfirmDialog::Kind> kinds;
    QStringList targets;
    QStringList hosts;
    Choice answer = Choice::Open;
    auto confirm() {
        return [this](LinkConfirmDialog& d) {
            kinds << d.kind();
            targets << d.targetLabel()->toolTip();
            hosts << (d.allowButton() != nullptr ? QStringLiteral("allow") : QString());
            d.choose(answer);
            return d.choice();
        };
    }
};

}  // namespace

class LinkConfirmTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-link_confirm"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
        QVERIFY(QFile::copy(source("fixtures/view/links.pdf"), tmp_.filePath(QStringLiteral("ссылки.pdf"))));
        QVERIFY(QFile::copy(source("fixtures/view/layout-mixed.pdf"),
                            tmp_.filePath(QStringLiteral("other.pdf"))));
        // Набор может идти после других в одном процессе — сайты теста начинают «не из списка».
        ot::LinkTrust trust;
        for (const char* host : {"example.org", "bad.example"}) {
            trust.setRule(QLatin1String(host), ot::HostRule::Ask);
        }
    }

    void dialogButtons() {
        LinkConfirmDialog web(LinkConfirmDialog::Kind::Url, QStringLiteral("https://example.org/a"),
                              QStringLiteral("example.org"));
        QVERIFY(web.allowButton() != nullptr);
        QCOMPARE(web.openButton()->text().remove(QLatin1Char('&')), QStringLiteral("Открыть"));
        QCOMPARE(web.allowButton()->text().remove(QLatin1Char('&')), QStringLiteral("Разрешить для сайта"));
        QCOMPARE(web.blockButton()->text().remove(QLatin1Char('&')), QStringLiteral("Запретить"));
        QVERIFY(web.openButton()->isDefault());
        QCOMPARE(web.targetLabel()->toolTip(), QStringLiteral("https://example.org/a"));
        web.allowButton()->click();
        QCOMPARE(web.choice(), Choice::AllowSite);
        QCOMPARE(web.result(), static_cast<int>(QDialog::Accepted));

        LinkConfirmDialog mail(LinkConfirmDialog::Kind::Url, QStringLiteral("mailto:a@example.org"));
        QVERIFY(mail.allowButton() == nullptr);  // у адреса без хоста «для сайта» нет
        mail.reject();
        QCOMPARE(mail.choice(), Choice::Cancel);

        LinkConfirmDialog file(LinkConfirmDialog::Kind::File, QStringLiteral("C:\\Документы\\other.pdf"));
        QVERIFY(file.allowButton() == nullptr);
        QCOMPARE(file.blockButton()->text(), QStringLiteral("Отмена"));
        file.blockButton()->click();
        QCOMPARE(file.choice(), Choice::Cancel);
    }

    void uriOpenAllowAndBlock() {
        std::printf("OT_FEATURE:security-42\n");
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(tmp_.filePath(QStringLiteral("ссылки.pdf"))));
        DocumentView& view = *w.currentDocument();
        Prompts p;
        w.linkFlow().setConfirm(p.confirm());
        const auto site = uri("https://example.org/akrobatya");

        // «Открыть» — один раз.
        QCOMPARE(w.linkFlow().follow(view, site), LinkDecision::AskUrl);
        QCOMPARE(p.targets, QStringList{QStringLiteral("https://example.org/akrobatya")});
        QCOMPARE(p.hosts, QStringList{QStringLiteral("allow")});
        QCOMPARE(shell.calls.size(), 1);
        QCOMPARE(shell.calls[0].kind, QStringLiteral("openUrl"));
        QCOMPARE(shell.calls[0].target, QStringLiteral("https://example.org/akrobatya"));
        w.linkFlow().follow(view, site);
        QCOMPARE(p.kinds.size(), 2);  // не запомнено — снова вопрос
        QCOMPARE(shell.calls.size(), 2);

        // «Разрешить для сайта» — запомнено (state.sqlite), дальше без вопроса.
        p.answer = Choice::AllowSite;
        w.linkFlow().follow(view, site);
        QCOMPARE(p.kinds.size(), 3);
        QCOMPARE(shell.calls.size(), 3);
        QCOMPARE(ot::LinkTrust().rule(QStringLiteral("example.org")), ot::HostRule::Allow);
        QCOMPARE(w.linkFlow().follow(view, uri("https://EXAMPLE.org/other?x=1")), LinkDecision::OpenUrl);
        QCOMPARE(p.kinds.size(), 3);
        QCOMPARE(shell.calls.size(), 4);

        // «Запретить» — запомнено, браузер не открывается и потом.
        p.answer = Choice::Block;
        const auto bad = uri("https://bad.example/x");
        w.linkFlow().follow(view, bad);
        QCOMPARE(p.kinds.size(), 4);
        QCOMPARE(shell.calls.size(), 4);
        QCOMPARE(w.linkFlow().follow(view, bad), LinkDecision::BlockedUrl);
        QCOMPARE(p.kinds.size(), 4);
        QCOMPARE(shell.calls.size(), 4);
        QVERIFY(w.linkFlow().lastMessage().contains(QStringLiteral("bad.example")));

        // Esc — ничего не запоминается.
        p.answer = Choice::Cancel;
        w.linkFlow().follow(view, uri("https://third.example/"));
        QCOMPARE(shell.calls.size(), 4);
        QCOMPARE(ot::LinkTrust().listedRule(QStringLiteral("third.example")), ot::HostRule::Ask);
    }

    void launchAndUnsupportedNeverRun() {
        std::printf("OT_FEATURE:security-36\n");
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(tmp_.filePath(QStringLiteral("ссылки.pdf"))));
        Prompts p;
        w.linkFlow().setConfirm(p.confirm());
        ot::links::LinkAction launch;
        launch.kind = ot::links::LinkKind::Launch;
        launch.file = "calc.exe";
        launch.uri = "file:calc.exe";
        QCOMPARE(w.linkFlow().follow(*w.currentDocument(), launch), LinkDecision::Forbidden);
        QVERIFY(w.linkFlow().lastMessage().contains(QStringLiteral("calc.exe")));
        QCOMPARE(w.linkFlow().follow(*w.currentDocument(), uri("javascript:app.alert(1)")),
                 LinkDecision::Unsupported);
        QVERIFY(p.kinds.isEmpty());
        QVERIFY(shell.calls.isEmpty());
        QCOMPARE(w.documentCount(), 1);
    }

    void goToRAsksAndOpensFile() {
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(tmp_.filePath(QStringLiteral("ссылки.pdf"))));
        Prompts p;
        w.linkFlow().setConfirm(p.confirm());
        ot::links::LinkAction remote;
        remote.kind = ot::links::LinkKind::GoToR;
        remote.file = "other.pdf";
        remote.uri = "file:other.pdf#page=2";
        remote.page = 1;
        p.answer = Choice::Cancel;
        QCOMPARE(w.linkFlow().follow(*w.currentDocument(), remote), LinkDecision::AskFile);
        QCOMPARE(p.kinds, QList<LinkConfirmDialog::Kind>{LinkConfirmDialog::Kind::File});
        QVERIFY(p.targets[0].endsWith(QStringLiteral("other.pdf")));
        QCOMPARE(w.documentCount(), 1);
        p.answer = Choice::Open;
        w.linkFlow().follow(*w.currentDocument(), remote);
        QCOMPARE(w.documentCount(), 2);
        QVERIFY(w.currentDocument()->path().endsWith(QStringLiteral("other.pdf")));
        QTRY_COMPARE(w.currentDocument()->view().currentPage(), 1);

        remote.file = "нет такого.pdf";
        w.linkFlow().follow(*w.document(0), remote);
        QCOMPARE(w.documentCount(), 2);
        QVERIFY(w.linkFlow().lastMessage().contains(QStringLiteral("не найден")));
    }

    void linkInViewGoesThroughFlow() {
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(tmp_.filePath(QStringLiteral("ссылки.pdf")), 0));
        ot::ui::PageView& v = w.currentDocument()->view();
        QTRY_VERIFY_WITH_TIMEOUT(v.linkLayer().hasLinks(0), 10000);
        Prompts p;
        p.answer = Choice::Cancel;
        w.linkFlow().setConfirm(p.confirm());
        w.linkFlow().trust().setRule(QStringLiteral("example.org"), ot::HostRule::Ask);  // разрешён выше
        v.linkLayer().clearFocus();
        for (int i = 0; i < 4; ++i) {  // четвёртая ссылка страницы 1 — /URI example.org (порядок Tab)
            QVERIFY(v.linkLayer().focusNext(true));
        }
        QVERIFY(v.linkLayer().focused() != nullptr);
        v.activateLink(*v.linkLayer().focused());  // как Enter и щелчок: сигнал linkActivated → окно
        QCOMPARE(p.targets, QStringList{QStringLiteral("https://example.org/akrobatya")});
        QVERIFY(shell.calls.isEmpty());
    }

private:
    QTemporaryDir tmp_;
};

OT_UI_TEST(LinkConfirmTest, "link_confirm");

#include "link_confirm_test.moc"
