// ui.protected_view — защищённый просмотр и права /P в окне (план 2.5b; эталон 7.1, 7.5; security-08, 35, 37,
// 39): файл с Zone.Identifier ZoneId=3 открывается только для чтения — «Сохранить», «Сохранить как», отмена и
// «Выбор объектов» не правят, печать правами не запрещена; жёлтая полоса «Включить все функции» снимает всё и
// запоминает файл (state.sqlite): повторное открытие — без полосы, Zone.Identifier на месте. ZoneId=2 и файл
// без потока — обычные; временная папка и PDF-вложение — тоже защищённый просмотр; вложения в нём — только
// «Сохранить…», ссылки наружу не открываются. Права: файл без печати и копирования — «Печать», «Копировать» и
// «Снимок» серые с подсказкой «Документ защищён: …», Ctrl+C ничего не кладёт в буфер.
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "services/app_info.h"
#include "services/commands.h"
#include "services/link_policy.h"
#include "services/protected_view.h"
#include "services/settings.h"
#include "services/shell_launcher.h"
#include "support/ui_test.h"
#include "ui/panes/attachments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/link_flow.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/view/tools/view_tool.h"
#include "ui/widgets/message_bar.h"

namespace pv = ot::protected_view;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::MessageKind;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

void showActive(QWidget& w) {
    w.resize(1440, 900);
    w.show();
    w.activateWindow();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

QPushButton* barButton(ot::ui::MessageBar* bar, const QString& text) {
    for (QPushButton* b : bar->findChildren<QPushButton*>()) {
        if (b->text().remove(QLatin1Char('&')) == text) {
            return b;
        }
    }
    return nullptr;
}

}  // namespace

class ProtectedViewTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-protected_view"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
    }

    void internetFileIsReadOnlyUntilEnableAllFeatures() {
        std::printf("OT_FEATURE:security-35\nOT_FEATURE:security-39\n");
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        const QString file = copy("fixtures/demo/report.pdf", QStringLiteral("скачанный отчёт.pdf"));
        QVERIFY(pv::writeZoneIdentifier(file, 3, QStringLiteral("https://example.org/report.pdf")));
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        QVERIFY(w.openFile(file));
        DocumentView* view = w.currentDocument();
        QVERIFY(view->isProtectedView());
        QCOMPARE(view->protectedOrigin(), pv::Origin::Internet);
        QVERIFY(!view->canEdit());
        QVERIFY(!view->canSave());
        QVERIFY(!view->view().tools().editable());
        ot::CommandRegistry& c = w.commands();
        for (const char* id : {"doc.save", "doc.saveAs"}) {
            QVERIFY2(!c.isEnabled(QLatin1String(id)), id);
            QVERIFY2(c.blockedReason(QLatin1String(id)).startsWith(QStringLiteral("Защищённый просмотр")),
                     id);
        }
        QVERIFY(c.blockedReason(QStringLiteral("doc.print"))
                    .isEmpty());  // печать защищённый просмотр не запрещает
        QVERIFY(c.isEnabled(QStringLiteral("doc.properties")));
        ot::ui::MessageBar* bar = view->messageStack().bar(MessageKind::ProtectedView);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->objectName(), QStringLiteral("protectedViewBar"));
        QVERIFY(view->messageStack().bars().first() == bar);  // жёлтая — первой

        // Ссылки наружу не открываются: ни диалога, ни браузера.
        int prompts = 0;
        w.linkFlow().setConfirm([&prompts](ot::ui::LinkConfirmDialog&) {
            ++prompts;
            return ot::ui::LinkConfirmDialog::Choice::Open;
        });
        ot::links::LinkAction uri;
        uri.kind = ot::links::LinkKind::Uri;
        uri.uri = "https://example.org/akrobatya";
        QCOMPARE(w.linkFlow().follow(*view, uri), ot::LinkDecision::ProtectedView);
        QCOMPARE(prompts, 0);
        QVERIFY(shell.calls.isEmpty());

        // «Включить все функции».
        QPushButton* enable = barButton(bar, QStringLiteral("Включить все функции"));
        QVERIFY(enable != nullptr);
        enable->click();
        QVERIFY(!view->isProtectedView());
        QVERIFY(view->canEdit());
        QVERIFY(view->view().tools().editable());
        QVERIFY(view->messageStack().bar(MessageKind::ProtectedView) == nullptr);
        QVERIFY(c.blockedReason(QStringLiteral("doc.save")).isEmpty());
        QVERIFY(c.isEnabled(QStringLiteral("doc.save")));
        QCOMPARE(pv::zoneId(file), 3);  // Zone.Identifier не трогаем
        QVERIFY(pv::isTrusted(file));

        // Повторное открытие — свой файл.
        QVERIFY(w.closeDocument(0));
        QVERIFY(w.openFile(file));
        QVERIFY(!w.currentDocument()->isProtectedView());
        QVERIFY(w.currentDocument()->messageStack().bar(MessageKind::ProtectedView) == nullptr);
    }

    void intranetPlainAndModes() {
        const QString intranet = copy("fixtures/demo/report.pdf", QStringLiteral("intranet.pdf"), "intranet");
        QVERIFY(pv::writeZoneIdentifier(intranet, 2));
        const QString plain = copy("fixtures/demo/report.pdf", QStringLiteral("plain.pdf"), "plain");
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(intranet));
        QVERIFY(!w.currentDocument()->isProtectedView());
        QVERIFY(w.openFile(plain));
        QVERIFY(!w.currentDocument()->isProtectedView());
        QVERIFY(w.closeAllDocuments());

        ot::Settings().set(ot::settings::kProtectedView, QStringLiteral("all"));
        QVERIFY(w.openFile(plain));
        QCOMPARE(w.currentDocument()->protectedOrigin(), pv::Origin::AllFiles);
        QVERIFY(w.closeAllDocuments());
        ot::Settings().set(ot::settings::kProtectedView, QStringLiteral("unsafe"));

        // Временная папка (у тестов правило выключено в ui_main — подставляем свою).
        pv::setTempFoldersForTesting(QStringList{tmp_.path()});
        QVERIFY(w.openFile(plain));
        QCOMPARE(w.currentDocument()->protectedOrigin(), pv::Origin::TempFolder);
        QVERIFY(w.closeAllDocuments());
        pv::setTempFoldersForTesting(QStringList{});

        // Привилегированная папка (security-37).
        std::printf("OT_FEATURE:security-37\n");
        QDir(tmp_.path()).mkpath(QStringLiteral("Доверенная"));
        const QString trusted =
            copy("fixtures/demo/report.pdf", QStringLiteral("Доверенная/web.pdf"), "trusted folder");
        QVERIFY(pv::writeZoneIdentifier(trusted, 3));
        ot::Settings().set(ot::settings::kTrustedFolders,
                           QStringList{tmp_.filePath(QStringLiteral("Доверенная"))});
        QVERIFY(w.openFile(trusted));
        QVERIFY(!w.currentDocument()->isProtectedView());
        ot::Settings().set(ot::settings::kTrustedFolders, QStringList{});
    }

    void attachmentsOnlySaveAndPdfAttachmentIsProtected() {
        ot::FakeShellLauncher shell;
        const ot::ScopedShellLauncher scoped(shell);
        const QString file = copy("fixtures/view/nav.pdf", QStringLiteral("nav-web.pdf"));
        QVERIFY(pv::writeZoneIdentifier(file, 3));
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        showActive(w);
        QVERIFY(w.openFile(file));
        auto* pane = static_cast<ot::ui::AttachmentsPane*>(w.navPane().page(QStringLiteral("attachments")));
        QStringList asked;
        QStringList chosen;
        pane->setConfirm([&asked](const QString& name) {
            asked << name;
            return true;
        });
        pane->setSaveChooser([&chosen](const QString& suggested) {
            chosen << suggested;
            return QString();  // отказ от сохранения — нам важно, что предложено сохранить
        });
        w.navPane().showPane(QStringLiteral("attachments"));
        QTRY_COMPARE(pane->files().size(), std::size_t{5});
        for (int i : {0, 2}) {  // inner.pdf и report.docx — только «Сохранить…»
            pane->select(i);
            QVERIFY(!pane->openButton()->isEnabled());
            QVERIFY(pane->openButton()->toolTip().startsWith(QStringLiteral("Защищённый просмотр")));
            pane->openCurrent();
        }
        QVERIFY(asked.isEmpty());
        QVERIFY(shell.calls.isEmpty());
        QCOMPARE(chosen.size(), 2);
        QCOMPARE(w.documentCount(), 1);

        // После «Включить все функции» PDF-вложение открывается — вкладкой в защищённом просмотре.
        w.enableAllFeatures(w.currentDocument());
        pane->select(0);
        QVERIFY(pane->openButton()->isEnabled());
        pane->openCurrent();
        QTRY_COMPARE(w.documentCount(), 2);
        DocumentView* inner = w.currentDocument();
        QVERIFY(inner->path().endsWith(QStringLiteral("inner.pdf")));
        QCOMPARE(inner->protectedOrigin(), pv::Origin::Attachment);
        ot::ui::MessageBar* bar = inner->messageStack().bar(MessageKind::ProtectedView);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->objectName(), QStringLiteral("attachmentBar"));
        QVERIFY(barButton(bar, QStringLiteral("Включить все функции")) != nullptr);
    }

    void permissionsGrayCommandsWithReason() {
        std::printf("OT_FEATURE:security-08\n");
        const QString file = tmp_.filePath(QStringLiteral("без печати и копирования.pdf"));
        {
            auto blank = ot::Document::createBlank(1);
            ot::SaveOptions options;
            options.mode = ot::SaveMode::Full;
            // Биты 3 (печать) и 5 (копирование) сняты; пароль пользователя пустой.
            options.encryption = ot::SaveEncryption{ot::SaveEncryption::Method::Aes256, "", "owner",
                                                    -4 & ~(1 << 2) & ~(1 << 4)};
            QVERIFY(ot::Saver::save(*blank, file.toStdWString(), options).ok());
        }
        MainWindow w;
        showActive(w);
        QVERIFY(w.openFile(file));
        DocumentView* view = w.currentDocument();
        QVERIFY(!view->isProtectedView());
        QVERIFY(!view->permissions().print);
        QVERIFY(!view->permissions().copy);
        QVERIFY(view->permissions().edit);
        QVERIFY(view->permissions().screenReader());  // бит доступности оставлен
        ot::CommandRegistry& c = w.commands();
        QCOMPARE(c.blockedReason(QStringLiteral("doc.print")),
                 QStringLiteral("Документ защищён: печать запрещена"));
        QVERIFY(!c.isEnabled(QStringLiteral("doc.print")));
        for (const char* id : {"edit.copy", "tool.snapshot"}) {
            QCOMPARE(c.blockedReason(QLatin1String(id)),
                     QStringLiteral("Документ защищён: копирование запрещено"));
            QVERIFY2(!c.isEnabled(QLatin1String(id)), id);
        }
        QVERIFY(c.blockedReason(QStringLiteral("doc.save")).isEmpty());
        QVERIFY(!view->view().tools().copyAllowed());
        QApplication::clipboard()->setText(QStringLiteral("до"));
        view->view().tools().setTool(ot::ui::ViewToolId::Select);
        view->view().tools().select().selectAll();
        QVERIFY(!view->view().tools().select().copySelection());
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("до"));

        // Обычный файл — ничего не запрещено.
        QVERIFY(w.openFile(source("fixtures/enc/plain.pdf")));
        QVERIFY(c.blockedReason(QStringLiteral("doc.print")).isEmpty());
        QVERIFY(c.blockedReason(QStringLiteral("edit.copy")).isEmpty());
        QVERIFY(w.currentDocument()->view().tools().copyAllowed());
    }

private:
    // Копия файла репозитория в папку теста; salt — другое содержимое (свой отпечаток для «доверенных»).
    QString copy(const char* rel, const QString& name, const QByteArray& salt = {}) {
        const QString target = tmp_.filePath(name);
        QFile::remove(target);
        QFile in(source(rel));
        if (!in.open(QIODevice::ReadOnly)) {
            return {};
        }
        QFile out(target);
        if (!out.open(QIODevice::WriteOnly)) {
            return {};
        }
        out.write(in.readAll());
        if (!salt.isEmpty()) {
            out.write("\n% " + salt + "\n");  // комментарий после %%EOF: файл тот же, отпечаток другой
        }
        return QFileInfo(target).absoluteFilePath();
    }

    QTemporaryDir tmp_;
};

OT_UI_TEST(ProtectedViewTest, "protected_view");

#include "protected_view_test.moc"
