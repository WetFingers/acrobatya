// ui.signatures_pane — проверка подписей вкладки, полоса, панель «Подписи», свойства и сертификат (план 4.6,
// п. 2; эталон 3.8 «Валидатор», «Панель «Подписи»», 2 — сценарий 5; sign-25, sign-26, sign-27, sign-28,
// sign-22, security-09): подписанный B-T файл с доверенным корнем — синяя полоса «все подписи действительны»,
// в панели «действительна», значок на поле, «Подписано» в строке состояния; сертифицированный P=2 — синяя
// «Сертифицировано», разрешённые изменения; B-LTA — «LTV включено»; правка после подписи — красная полоса и
// «изменения после подписи не разрешены» (и EU DSS modified_after_signature.pdf, если есть корпус); просмотр
// сертификата — цепочка из 3; «Добавить в доверенные» меняет «подписант не доверен» на «действительна»;
// «Просмотреть подписанную версию» — вкладка «только чтение», временный файл уходит с ней; «Очистить
// подпись» — поле снова пустое; проверка при открытии выключена — полосы нет, панель проверяет сама;
// «Свойства подписи» — четыре вкладки.
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/crypto_worker.h"
#include "services/settings.h"
#include "services/trust_service.h"
#include "support/sign_ui.h"
#include "support/ui_test.h"
#include "ui/dialogs/certificate_viewer.h"
#include "ui/dialogs/signature_properties_dialog.h"
#include "ui/dialogs/trust_dialog.h"
#include "ui/panes/signatures_pane.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/signature_status.h"
#include "ui/shell/status_bar.h"
#include "ui/view/page_view.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/message_bar.h"

using ot::ui::CertificateViewer;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::MessageKind;
using ot::ui::SignaturesPane;

namespace {

DocumentView* openDoc(MainWindow& w, const QString& path) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(path));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
    }();
    return doc;
}

SignaturesPane* pane(MainWindow& w) {
    w.navPane().showPane(QStringLiteral("signatures"));
    return qobject_cast<SignaturesPane*>(w.navPane().page(QStringLiteral("signatures")));
}

bool verified(DocumentView& doc) { return doc.signatures().hasResult() && !doc.signatures().isVerifying(); }

QAction* action(QMenu* menu, const char* name) { return menu->findChild<QAction*>(QLatin1String(name)); }

}  // namespace

class SignaturesPaneTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-signatures_pane"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
        network_ = std::make_unique<ot::test::PkiNetwork>();
        ot::test::trustPkiRoot();
    }

    void cleanupTestCase() {
        ot::CryptoWorker::shared().waitForDone();
        network_.reset();
    }

    void init() { ot::test::trustPkiRoot(); }

    // Сценарий 5: подписанный файл с доверенным корнем — синяя полоса, «действительна», значок на поле.
    void validSignatureBlueBarAndPane() {
        std::printf("OT_FEATURE:sign-25\n");
        std::printf("OT_FEATURE:sign-26\n");
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("подписан.pdf"));
        ot::test::SignFileOptions o;
        o.level = ot::sign::SignLevel::T;
        o.network = network_.get();
        o.reason = "Утверждаю";
        ot::test::signFile(path, o);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::SignatureValid);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->tone(), ot::ui::Tone::Info);
        QVERIFY2(bar->text().contains(QStringLiteral("все подписи действительны")), qPrintable(bar->text()));
        QVERIFY(doc->messageStack().bar(MessageKind::SignatureInvalid) == nullptr);
        QCOMPARE(w.status().signedChip()->text(), QStringLiteral("Подписано"));
        SignaturesPane* p = pane(w);
        QVERIFY(p != nullptr);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        const QString text = p->itemText(QStringLiteral("Signature1"));
        QVERIFY2(text.contains(QStringLiteral("Подпись 1: АкроБатя тест (действительна)")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Причина: Утверждаю")));
        QVERIFY(text.contains(QStringLiteral("PAdES B-T")));
        QVERIFY(text.contains(QStringLiteral("встроенную метку времени")));
        QVERIFY(!p->isEmptyShown());
        // «Панель подписей» на полосе — открывает панель.
        w.navPane().closePanel();
        bar->findChild<QPushButton*>()->click();
        QTRY_VERIFY(w.navPane().isPanelOpen() && w.navPane().currentPane() == QLatin1String("signatures"));
        // «Перейти к полю» — страница поля.
        QMenu* menu = p->menuFor(QStringLiteral("Signature1"));
        QVERIFY(action(menu, "sigGoTo")->isEnabled());
        action(menu, "sigGoTo")->trigger();
        QCOMPARE(doc->view().currentPage(), 0);
        delete menu;
    }

    // Сертифицированный P=2 — синяя «Сертифицировано», разрешённые изменения.
    void certifiedShowsRibbonText() {
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("серт.pdf"));
        ot::test::SignFileOptions o;
        o.certify = ot::sign::MdpPermission::FormFilling;
        o.rect = {};
        ot::test::signFile(path, o);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        QCOMPARE(doc->signatures().verification().status, ot::sign::DocumentStatus::Certified);
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::SignatureValid);
        QVERIFY(bar != nullptr);
        QVERIFY2(bar->text().startsWith(QStringLiteral("Сертифицировано")), qPrintable(bar->text()));
        SignaturesPane* p = pane(w);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        const QString text = p->itemText(QStringLiteral("Signature1"));
        QVERIFY2(text.contains(QStringLiteral("Сертифицирующая подпись 1")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("Разрешённые изменения: Заполнение форм и подпись")));
        QVERIFY(text.contains(QStringLiteral("Невидимая подпись")));
    }

    // B-LTA: «LTV включено», уровень B-LTA, метка времени документа.
    void ltaShowsLtv() {
        const QString path =
            ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(), QStringLiteral("lta.pdf"));
        ot::test::SignFileOptions o;
        o.level = ot::sign::SignLevel::LTA;
        o.network = network_.get();
        ot::test::signFile(path, o);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        QVERIFY(doc->messageStack().bar(MessageKind::SignatureValid) != nullptr);
        SignaturesPane* p = pane(w);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        const QString text = p->itemText(QStringLiteral("Signature1"));
        QVERIFY2(text.contains(QStringLiteral("LTV включено")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("PAdES B-LTA")));
        QVERIFY(p->itemFor(QStringLiteral("Timestamp1")) != nullptr);
        QVERIFY(p->itemText(QStringLiteral("Timestamp1")).contains(QStringLiteral("Метка времени 2")));
    }

    // Правка после подписи — красная полоса и «изменения не разрешены».
    void modifiedAfterSigningIsInvalid() {
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("изменён.pdf"));
        ot::test::signFile(path);
        ot::test::tamperFile(path);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::SignatureInvalid);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->tone(), ot::ui::Tone::Danger);
        QCOMPARE(w.status().signedChip()->text(), QStringLiteral("Подпись недействительна"));
        SignaturesPane* p = pane(w);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        const QString text = p->itemText(QStringLiteral("Signature1"));
        QVERIFY2(text.contains(QStringLiteral("Изменения после подписи не разрешены")), qPrintable(text));
        QVERIFY(text.contains(QStringLiteral("недействительна")));
        // Корпус EU DSS (если скачан): тот же вердикт.
        const QString dss = QString::fromLocal8Bit(qgetenv("OTTISK_CORPUS")).isEmpty()
                                ? QStringLiteral("C:/Tools/ottisk/corpus")
                                : QString::fromLocal8Bit(qgetenv("OTTISK_CORPUS"));
        const QString file =
            dss + QStringLiteral("/dss/dss-pades/src/test/resources/validation/modified_after_signature.pdf");
        if (QFile::exists(file)) {
            const QString copy = QDir(tmp_.path()).filePath(QStringLiteral("modified_after_signature.pdf"));
            QFile::remove(copy);
            QVERIFY(QFile::copy(file, copy));
            QVERIFY(w.openFile(copy));
            DocumentView* d2 = w.currentDocument();
            QTRY_VERIFY_WITH_TIMEOUT(verified(*d2), 30000);
            QVERIFY(d2->messageStack().bar(MessageKind::SignatureInvalid) != nullptr);
            QTRY_VERIFY(!p->itemText(QStringLiteral("Signature1")).isEmpty());
            QVERIFY(p->itemText(QStringLiteral("Signature1")).contains(QStringLiteral("не разрешены")));
        }
    }

    // Просмотр сертификата: цепочка из трёх, вкладки, корень — доверенный якорь.
    void certificateViewerShowsChainOfThree() {
        std::printf("OT_FEATURE:sign-27\n");
        std::printf("OT_FEATURE:security-09\n");
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("цепочка.pdf"));
        ot::test::signFile(path);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        SignaturesPane* p = pane(w);
        CertificateViewer* v = p->showCertificate(QStringLiteral("Signature1"));
        QVERIFY(v != nullptr);
        QCOMPARE(v->chainLength(), 3);
        QCOMPARE(v->tabs()->count(), 5);
        QVERIFY(v->summaryLabel()->text().contains(QStringLiteral("АкроБатя тест")));
        QVERIFY(v->summaryLabel()->text().contains(QStringLiteral("цифровая подпись")));
        QVERIFY(v->detailsTree()->topLevelItemCount() > 8);
        QVERIFY(v->policiesLabel()->text().contains(QStringLiteral("не указаны")));
        v->select(2);
        QVERIFY2(v->trustLabel()->text().contains(QStringLiteral("доверенный якорь")),
                 qPrintable(v->trustLabel()->text()));
        QVERIFY(v->trustButton()->text().contains(QStringLiteral("Изменить")));
        v->select(0);
        QVERIFY(v->trustButton()->text().contains(QStringLiteral("Добавить")));
        v->close();
        // «Свойства подписи»: четыре вкладки, статус.
        ot::ui::SignaturePropertiesDialog* props = p->showProperties(QStringLiteral("Signature1"));
        QVERIFY(props != nullptr);
        QCOMPARE(props->tabs()->count(), 4);
        QVERIFY(props->verdictLabel()->text().contains(QStringLiteral("действительна")));
        QVERIFY(props->findingsTree()->topLevelItemCount() > 0);
        QVERIFY(props->signerLabel()->text().contains(
            QStringLiteral("Цепочка до доверенного сертификата построена")));
        props->close();
    }

    // «Добавить в доверенные» из просмотра сертификата: ValidUntrusted → Valid.
    void addToTrustedTurnsValid() {
        std::printf("OT_FEATURE:sign-34\n");
        QVERIFY(ot::test::untrustPkiRoot());
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("доверие.pdf"));
        ot::test::signFile(path);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        const ot::sign::SignatureInfo* s = doc->signatures().signature(QStringLiteral("Signature1"));
        QVERIFY(s != nullptr);
        QCOMPARE(s->verdict, ot::sign::Verdict::ValidUntrusted);
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::SignatureWarning);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->findChildren<QPushButton*>().size(), 2);  // «Панель подписей», «Доверять подписанту…»
        SignaturesPane* p = pane(w);
        QTRY_VERIFY(
            p->itemText(QStringLiteral("Signature1")).contains(QStringLiteral("подписант не доверен")));
        CertificateViewer* v = p->showCertificate(QStringLiteral("Signature1"));
        QVERIFY(v != nullptr);
        v->select(v->chainLength() - 1);  // корень
        v->setTrustPrompt([](ot::ui::TrustDialog& d) -> std::optional<unsigned> {
            [&] { QVERIFY(d.signaturesBox()->isChecked()); }();
            return d.flags();
        });
        v->editTrust();
        QVERIFY(v->trustLabel()->text().contains(QStringLiteral("доверенный якорь")));
        QTRY_VERIFY_WITH_TIMEOUT(doc->signatures().signature(QStringLiteral("Signature1")) != nullptr &&
                                     doc->signatures().signature(QStringLiteral("Signature1"))->verdict ==
                                         ot::sign::Verdict::Valid,
                                 20000);
        QTRY_VERIFY(doc->messageStack().bar(MessageKind::SignatureValid) != nullptr);
        QVERIFY(doc->messageStack().bar(MessageKind::SignatureWarning) == nullptr);
        v->close();
    }

    // «Просмотреть подписанную версию»: вкладка «только чтение», временный файл уходит с ней.
    void signedVersionOpensReadOnlyTab() {
        std::printf("OT_FEATURE:sign-28\n");
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("версия.pdf"));
        ot::test::signFile(path);
        ot::test::tamperFile(path);
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        SignaturesPane* p = pane(w);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        QMenu* menu = p->menuFor(QStringLiteral("Signature1"));
        action(menu, "sigSignedVersion")->trigger();
        delete menu;
        QTRY_COMPARE_WITH_TIMEOUT(w.documentCount(), 2, 10000);
        DocumentView* version = w.currentDocument();
        QVERIFY(version != doc);
        QVERIFY(version->isReadOnly());
        QVERIFY(!version->canEdit());
        bool hasBar = false;
        for (ot::ui::MessageBar* b : version->messages()) {
            hasBar = hasBar || b->objectName() == QLatin1String("signedVersionBar");
        }
        QVERIFY(hasBar);
        const QString temp = version->path();
        QVERIFY(QFile::exists(temp));
        QVERIFY(w.tabs().tabText(w.tabs().currentIndex()).contains(QStringLiteral("подписанная версия")));
        // Подписанная ревизия — без правки после подписи: подпись покрывает весь файл.
        QTRY_VERIFY_WITH_TIMEOUT(verified(*version), 20000);
        QCOMPARE(version->signatures().verification().status, ot::sign::DocumentStatus::Valid);
        QVERIFY(w.closeDocument(w.currentIndex()));
        QVERIFY(!QFile::exists(temp));
    }

    // «Очистить подпись»: последняя незаблокированная — поле снова пустое, полосы нет.
    void clearSignatureEmptiesField() {
        std::printf("OT_FEATURE:sign-22\n");
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("очистить.pdf"));
        ot::test::signFile(path);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        SignaturesPane* p = pane(w);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        QVERIFY(p->canClear(QStringLiteral("Signature1")));
        p->setConfirm([](const QString&) { return true; });
        bool done = false;
        bool ok = false;
        connect(p, &SignaturesPane::actionFinished, this, [&](bool success, const QString&) {
            done = true;
            ok = success;
        });
        p->clearSignature(QStringLiteral("Signature1"));
        QTRY_VERIFY_WITH_TIMEOUT(done, 20000);
        QVERIFY(ok);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc) && doc->signatures().verification().signatures.empty(),
                                 20000);
        QVERIFY(doc->messageStack().bar(MessageKind::SignatureValid) == nullptr);
        QTRY_VERIFY(p->itemFor(QStringLiteral("Signature1")) != nullptr);
        QVERIFY(p->itemText(QStringLiteral("Signature1")).contains(QStringLiteral("Пустое поле подписи")));
        QVERIFY(ot::test::verifyFile(path, nullptr).signatures.empty());
    }

    // Проверка при открытии выключена — полосы нет; панель проверяет сама.
    void verifyOnOpenOffWaitsForPane() {
        ot::Settings().set(ot::settings::kVerifyOnOpen, false);
        ot::Settings().set(ot::settings::kNavPaneOpen, false);  // открытая панель «Подписи» проверила бы сама
        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("потом.pdf"));
        ot::test::signFile(path);
        MainWindow w;
        DocumentView* doc = openDoc(w, path);
        QVERIFY(doc != nullptr);
        QTest::qWait(300);
        QVERIFY(!doc->signatures().hasResult());
        QVERIFY(doc->signatures().bar() == nullptr);
        pane(w);
        QTRY_VERIFY_WITH_TIMEOUT(verified(*doc), 20000);
        QVERIFY(doc->signatures().bar() != nullptr);
        ot::Settings().set(ot::settings::kVerifyOnOpen, true);
    }

private:
    QTemporaryDir tmp_;
    std::unique_ptr<ot::test::PkiNetwork> network_;
};

OT_UI_TEST(SignaturesPaneTest, "signatures_pane");
#include "signatures_pane_test.moc"
