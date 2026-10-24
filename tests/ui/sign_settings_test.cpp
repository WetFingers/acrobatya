// ui.sign_settings — Установки → Подписи (план 4.6, п. 3; эталон 3.18 «Подписи», 3.8 «Доверие»; sign-05 (UI),
// sign-10, sign-11, sign-13, sign-14, sign-19, sign-30, sign-31 (UI), sign-32, sign-34): живая категория с
// пятью вкладками; удостоверения — добавить .pfx, экспорт сертификата .cer/.fdf, смена пароля, тайм-аут
// пароля, создать самоподписанное, по умолчанию, удалить; доверенные — импорт корня, экспорт, изменить
// доверие, корни Windows по назначениям, удалить; списки доверия — флажки и «Обновить сейчас» без сети
// (честное «не загрузились»); серверы времени — добавить, по умолчанию, «Проверить» (служба PKI в процессе),
// удалить, что делать при недоступной службе; проверка — время, отзыв, просроченные метки → VerifyRequest;
// создание — показывать причину и место, свои причины, без «Проверить документ», PKCS#7, свой внешний вид по
// умолчанию в диалоге подписи; сведения LTV после подписи «всегда» — подпись B-T становится B-LT.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/crypto_worker.h"
#include "services/identities.h"
#include "services/settings.h"
#include "services/sign_settings.h"
#include "services/trust_service.h"
#include "services/tsa_settings.h"
#include "support/sign_ui.h"
#include "support/ui_test.h"
#include "ui/dialogs/trust_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/identities_widget.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/shell/preferences/signatures_page.h"
#include "ui/shell/preferences/trusted_widget.h"
#include "ui/shell/preferences/tsa_widget.h"
#include "ui/tools/certificate/appearance_dialog.h"
#include "ui/tools/certificate/certificate_tool.h"
#include "ui/tools/certificate/new_identity_dialog.h"
#include "ui/tools/certificate/sign_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"

using ot::Settings;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::SignaturesPage;
using ot::ui::ToolRegistry;
namespace keys = ot::settings;

namespace {

SignaturesPage* signaturesPage(MainWindow& w) {
    ot::ui::PreferencesDialog* d = w.showPreferences(QStringLiteral("signatures"));
    return d != nullptr ? qobject_cast<SignaturesPage*>(d->page(QStringLiteral("signatures"))) : nullptr;
}

ot::ui::CertificateTool& certTool() {
    return *static_cast<ot::ui::CertificateTool*>(
        ToolRegistry::instance().find(QStringLiteral("certificate")));
}

QByteArray head(const QString& path, int n) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.read(n) : QByteArray();
}

}  // namespace

class SignSettingsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-sign_settings"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        QVERIFY(tmp_.isValid());
        network_ = std::make_unique<ot::test::PkiNetwork>();
    }

    void cleanupTestCase() {
        ot::CryptoWorker::shared().waitForDone();
        network_.reset();
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void pageIsLiveWithFiveTabs() {
        MainWindow w;
        SignaturesPage* page = signaturesPage(w);
        QVERIFY(page != nullptr);
        QCOMPARE(page->stage(), 0);
        QCOMPARE(page->title(), QStringLiteral("Подписи"));
        QCOMPARE(page->tabs()->count(), 5);
        for (const QString& id : SignaturesPage::tabIds()) {
            QVERIFY(page->showTab(id));
        }
        QVERIFY(!page->showTab(QStringLiteral("nope")));
        w.preferencesDialog()->reject();
    }

    // Удостоверения: добавить, экспорт, пароль, тайм-аут, создать, по умолчанию, удалить.
    void identitiesManage() {
        std::printf("OT_FEATURE:sign-10\n");
        std::printf("OT_FEATURE:sign-11\n");
        std::printf("OT_FEATURE:sign-05\n");
        MainWindow w;
        SignaturesPage* page = signaturesPage(w);
        QVERIFY(page != nullptr);
        ot::ui::IdentitiesWidget* ids = page->identities();
        QCOMPARE(ids->list()->topLevelItemCount(), 0);
        QVERIFY(!ids->windowsButton()->isEnabled());  // offscreen — хранилище Windows только на экране
        QVERIFY(!ids->addPfx(ot::test::pkiPfx(), QStringLiteral("неверный")));
        QVERIFY(ids->noteLabel()->isVisibleTo(ids));
        QVERIFY(ids->addPfx(ot::test::pkiPfx(), ot::test::pkiPassword()));
        QCOMPARE(ids->list()->topLevelItemCount(), 1);
        QVERIFY(ids->selected()->isDefault);  // первое — по умолчанию
        // Экспорт сертификата: .cer (DER) и .fdf (обмен Acrobat).
        const QString cer = QDir(tmp_.path()).filePath(QStringLiteral("мой.cer"));
        QVERIFY(ids->exportSelected(cer));
        QCOMPARE(head(cer, 1), QByteArray("\x30", 1));
        const QString fdf = QDir(tmp_.path()).filePath(QStringLiteral("мой.fdf"));
        QVERIFY(ids->exportSelected(fdf));
        QCOMPARE(head(fdf, 5), QByteArray("%FDF-"));
        // Пароль: неверный старый — отказ, верный — новый открывает.
        QVERIFY(!ids->changePassword(QStringLiteral("неверный"), QStringLiteral("новый")));
        QVERIFY(ids->changePassword(ot::test::pkiPassword(), QStringLiteral("новый-пароль")));
        const std::string id = ids->selectedId().toStdString();
        ot::Identities::shared().with([&](ot::sign::IdentityStore& s) {
            s.forgetCachedPasswords();
            (void)s.open(id, std::string("новый-пароль"));
        });
        // Тайм-аут пароля: 10 минут.
        QVERIFY(ids->setTimeoutSelected(600));
        QCOMPARE(ids->selected()->passwordTimeout, 600);
        QCOMPARE(ids->timeoutBox()->currentData().toInt(), 600);
        // Создать самоподписанное — второе в списке; по умолчанию — оно.
        ids->createButton()->click();
        ot::ui::NewIdentityDialog* create = nullptr;
        QTRY_VERIFY((create = ids->findChild<ot::ui::NewIdentityDialog*>()) != nullptr);
        create->nameField()->setText(QStringLiteral("Семья Петровых"));
        create->algorithmBox()->setCurrentIndex(1);
        create->passwordField()->setText(QStringLiteral("123456"));
        create->confirmField()->setText(QStringLiteral("123456"));
        create->submit();
        QTRY_COMPARE(ids->list()->topLevelItemCount(), 2);
        QCOMPARE(ids->selected()->name, std::string("Семья Петровых"));
        QVERIFY(!ids->selected()->isDefault);
        QVERIFY(ids->setDefaultSelected());
        QVERIFY(ids->selected()->isDefault);
        // Удалить — остаётся одно.
        QVERIFY(ids->removeSelected(true));
        QCOMPARE(ids->list()->topLevelItemCount(), 1);
        w.preferencesDialog()->reject();
    }

    // Доверенные сертификаты: импорт, экспорт, доверие, корни Windows, удаление.
    void trustedManage() {
        std::printf("OT_FEATURE:sign-30\n");
        std::printf("OT_FEATURE:sign-34\n");
        MainWindow w;
        SignaturesPage* page = signaturesPage(w);
        ot::ui::TrustedWidget* tw = page->trusted();
        QCOMPARE(tw->importFile(ot::test::fixturePath(QStringLiteral("pki/root.cer"))), 1);
        QCOMPARE(tw->importFile(ot::test::fixturePath(QStringLiteral("pki/root.cer"))), 0);  // уже есть
        QTRY_COMPARE(tw->list()->topLevelItemCount(), 1);
        QVERIFY(tw->list()->topLevelItem(0)->text(0).contains(QStringLiteral("корневой ЦС")));
        QCOMPARE(tw->list()->topLevelItem(0)->text(1), QStringLiteral("Добавлен вручную"));
        QCOMPARE(tw->list()->topLevelItem(0)->text(2), QStringLiteral("подписи"));
        const QString p7c = QDir(tmp_.path()).filePath(QStringLiteral("корень.p7c"));
        QVERIFY(tw->exportSelected(p7c));
        QVERIFY(QFile(p7c).size() > 100);
        const QString all = QDir(tmp_.path()).filePath(QStringLiteral("все.p7c"));
        QVERIFY(tw->exportAll(all));
        // Изменить доверие: и сертифицированные документы.
        tw->setTrustPrompt([](ot::ui::TrustDialog& d) -> std::optional<unsigned> {
            d.certificationBox()->setChecked(true);
            return d.flags();
        });
        QVERIFY(tw->editSelected());
        const std::string sha = tw->selectedThumbprint().toStdString();
        QCOMPARE(ot::TrustService::shared().snapshot()->find(sha)->flags,
                 ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
        // Корни Windows — для подписей: сразу в хранилище программы.
        tw->windowsSignaturesBox()->setChecked(true);
        QCOMPARE(ot::TrustService::shared().snapshot()->windowsRootsFlags(), ot::sign::kTrustSignatures);
        tw->windowsSignaturesBox()->setChecked(false);
        QCOMPARE(ot::TrustService::shared().snapshot()->windowsRootsFlags(), 0u);
        // Удалить.
        tw->setConfirm([](const QString&) { return true; });
        QVERIFY(tw->removeSelected());
        QVERIFY(ot::TrustService::shared().snapshot()->anchors().empty());
        w.preferencesDialog()->reject();
    }

    // Списки доверия: флажки — по «ОК»; «Обновить сейчас» без списков в сети — честное «не загрузились».
    void trustListsUpdateWithoutNetwork() {
        std::printf("OT_FEATURE:sign-31\n");
        MainWindow w;
        SignaturesPage* page = signaturesPage(w);
        ot::ui::TrustedWidget* tw = page->trusted();
        QVERIFY(tw->eutlBox()->isChecked());
        QVERIFY(tw->aatlBox()->isChecked());
        QVERIFY(tw->askBox()->isChecked());
        tw->aatlBox()->setChecked(false);
        w.preferencesDialog()->applyAll();
        QVERIFY(!Settings().get(keys::kTrustAatl));
        bool done = false;
        bool ok = true;
        QString message;
        connect(tw, &ot::ui::TrustedWidget::updateFinished, this, [&](bool success, const QString& text) {
            done = true;
            ok = success;
            message = text;
        });
        tw->updateNow();
        QVERIFY(tw->isUpdating());
        QTRY_VERIFY_WITH_TIMEOUT(done, 30000);
        QVERIFY(!ok);
        QVERIFY2(message.contains(QStringLiteral("не загрузились")), qPrintable(message));
        QVERIFY(!Settings().get(keys::kTrustListsCheckedAt).isEmpty());
        tw->aatlBox()->setChecked(true);
        w.preferencesDialog()->applyAll();
        w.preferencesDialog()->reject();
    }

    // Серверы времени: добавить, по умолчанию, проверить, удалить, «служба не ответила».
    void tsaServers() {
        std::printf("OT_FEATURE:sign-19\n");
        MainWindow w;
        SignaturesPage* page = signaturesPage(w);
        ot::ui::TsaWidget* tsa = page->tsa();
        QCOMPARE(tsa->list()->topLevelItemCount(), 3);
        QCOMPARE(tsa->servers().defaultName(), std::string("DigiCert"));
        QVERIFY(!tsa->addServer({"Плохой", "ftp://x", "", ""}));
        QVERIFY(tsa->addServer({"PKI", "http://tsa.acrobatya.test/tsr", "", ""}));
        QCOMPARE(tsa->list()->topLevelItemCount(), 4);
        QVERIFY(tsa->select(QStringLiteral("PKI")));
        QVERIFY(tsa->setDefaultSelected());
        QCOMPARE(tsa->servers().defaultName(), std::string("PKI"));
        bool checked = false;
        bool ok = false;
        connect(tsa, &ot::ui::TsaWidget::checkFinished, this, [&](bool success, const QString&) {
            checked = true;
            ok = success;
        });
        tsa->checkSelected();
        QTRY_VERIFY_WITH_TIMEOUT(checked, 20000);
        QVERIFY(ok);
        QVERIFY(tsa->checkLabel()->text().contains(QStringLiteral("ответил")));
        QVERIFY(tsa->select(QStringLiteral("Sectigo")));
        QVERIFY(tsa->removeSelected());
        QCOMPARE(tsa->list()->topLevelItemCount(), 3);
        tsa->fallbackBox()->setCurrentIndex(tsa->fallbackBox()->findData(QStringLiteral("withoutTimestamp")));
        w.preferencesDialog()->applyAll();
        const ot::sign::TsaServers saved = ot::loadTsaServers(Settings());
        QCOMPARE(saved.defaultName(), std::string("PKI"));
        QVERIFY(saved.find("Sectigo") == nullptr);
        QCOMPARE(Settings().get(keys::kTimestampFallback), QStringLiteral("withoutTimestamp"));
        w.preferencesDialog()->reject();
    }

    // Проверка: время, отзыв, просроченные метки — в VerifyRequest.
    void verificationSettings() {
        std::printf("OT_FEATURE:sign-32\n");
        MainWindow w;
        SignaturesPage* page = signaturesPage(w);
        QVERIFY(page->verifyOnOpenBox()->isChecked());
        page->verifyTimeBox()->setCurrentIndex(page->verifyTimeBox()->findData(QStringLiteral("current")));
        page->revocationBox()->setCurrentIndex(page->revocationBox()->findData(QStringLiteral("required")));
        page->expiredBox()->setChecked(false);
        w.preferencesDialog()->applyAll();
        const ot::VerifyRequest r = ot::VerifyRequest::fromSettings(Settings());
        QCOMPARE(r.time, ot::sign::VerificationTime::Current);
        QCOMPARE(r.revocation, ot::sign::RevocationPolicy::Required);
        QVERIFY(!r.acceptExpiredTimestamps);
        page->verifyTimeBox()->setCurrentIndex(page->verifyTimeBox()->findData(QStringLiteral("signing")));
        page->revocationBox()->setCurrentIndex(page->revocationBox()->findData(QStringLiteral("bestEffort")));
        page->expiredBox()->setChecked(true);
        w.preferencesDialog()->applyAll();
        QCOMPARE(ot::VerifyRequest::fromSettings(Settings()).time, ot::sign::VerificationTime::SigningTime);
        w.preferencesDialog()->reject();
    }

    // Создание: причина и место, свои причины, без «Проверить документ», свой вид по умолчанию, PKCS#7.
    void creationSettingsReachSignDialog() {
        std::printf("OT_FEATURE:sign-14\n");
        std::printf("OT_FEATURE:sign-13\n");
        Settings().set(keys::kAutoAddLtv, QStringLiteral("never"));  // уровень подписи — ровно выбранный
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        SignaturesPage* page = signaturesPage(w);
        page->showReasonBox()->setChecked(false);
        page->showLocationBox()->setChecked(true);
        page->reasonsEdit()->setPlainText(QStringLiteral("Причина А\nПричина Б"));
        page->reviewBox()->setCurrentIndex(page->reviewBox()->findData(QStringLiteral("never")));
        page->formatBox()->setCurrentIndex(1);  // PKCS#7
        ot::ui::AppearanceDialog* look = page->newAppearance();
        QVERIFY(look != nullptr);
        look->nameField()->setText(QStringLiteral("Рабочая"));
        look->submit();
        QTRY_COMPARE(static_cast<int>(page->appearances().size()), 1);
        QVERIFY(page->setDefaultAppearance());
        QCOMPARE(page->defaultAppearance(), QStringLiteral("Рабочая"));
        w.preferencesDialog()->accept();
        QCOMPARE(Settings().get(keys::kSignReasons),
                 (QStringList{QStringLiteral("Причина А"), QStringLiteral("Причина Б")}));
        QCOMPARE(Settings().get(keys::kSignAppearance), QStringLiteral("Рабочая"));

        const QString path = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                   QStringLiteral("pkcs7.pdf"));
        QVERIFY(w.openFile(path));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        DocumentView* doc = w.currentDocument();
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("certificate"), doc));
        certTool().setFileChooser([](QWidget*, const QString& suggested) { return suggested; });
        bool finished = false;
        bool ok = false;
        connect(&certTool(), &ot::ui::CertificateTool::finished, this, [&](bool success, const QString&) {
            finished = true;
            ok = success;
        });
        certTool().placeSignature(0, {72, 72, 300, 150});
        ot::ui::SignDialog* d = nullptr;
        QTRY_VERIFY((d = qobject_cast<ot::ui::SignDialog*>(certTool().dialog())) != nullptr);
        QVERIFY(!d->reasonBox()->isVisibleTo(d));
        QVERIFY(d->locationField()->isVisibleTo(d));
        QVERIFY(d->contactField()->isVisibleTo(d));
        QVERIFY(!d->reviewButton()->isVisibleTo(d));
        QCOMPARE(d->appearanceBox()->currentText(), QStringLiteral("Рабочая"));
        QCOMPARE(d->reasonBox()->count(), 3);  // пусто + две свои
        d->locationField()->setText(QStringLiteral("Тверь"));
        d->signButton()->click();  // пароль «новый-пароль» в кэше (прошлый тест) — либо запрос
        if (d->errorLabel()->isVisibleTo(d)) {
            d->passwordField()->setText(QStringLiteral("новый-пароль"));
            d->signButton()->click();
        }
        QTRY_VERIFY_WITH_TIMEOUT(finished, 30000);
        QVERIFY(ok);
        const ot::sign::DocumentVerification v = ot::test::verifyFile(path, nullptr);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        QCOMPARE(v.signatures[0].subFilter, std::string("adbe.pkcs7.detached"));
        QCOMPARE(QString::fromStdString(v.signatures[0].location), QStringLiteral("Тверь"));
        QVERIFY(v.signatures[0].reason.empty());
        // Назад к умолчаниям для следующего теста.
        Settings s;
        s.set(keys::kSignCompatibility, false);
        s.set(keys::kSignShowReason, true);
        s.set(keys::kSignShowLocation, false);
        s.set(keys::kSignReviewWarnings, QStringLiteral("always"));
    }

    // Сведения LTV после подписи «всегда»: B-T становится B-LT без вопроса.
    void autoAddLtvAlways() {
        std::printf("OT_FEATURE:sign-21\n");
        Settings().set(keys::kAutoAddLtv, QStringLiteral("always"));
        ot::test::trustPkiRoot();
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        const QString path =
            ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(), QStringLiteral("ltv.pdf"));
        QVERIFY(w.openFile(path));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("certificate"), w.currentDocument()));
        certTool().setFileChooser([](QWidget*, const QString& suggested) { return suggested; });
        bool added = false;
        bool ltvDone = false;
        connect(&certTool(), &ot::ui::CertificateTool::ltvFinished, this, [&](bool ok) {
            ltvDone = true;
            added = ok;
        });
        certTool().placeSignature(0, {72, 72, 300, 150});
        ot::ui::SignDialog* d = nullptr;
        QTRY_VERIFY((d = qobject_cast<ot::ui::SignDialog*>(certTool().dialog())) != nullptr);
        QCOMPARE(d->level(), ot::sign::SignLevel::T);
        d->tsaBox()->setCurrentIndex(d->tsaBox()->findData(QStringLiteral("PKI")));
        d->signButton()->click();
        if (d->errorLabel()->isVisibleTo(d)) {
            d->passwordField()->setText(QStringLiteral("новый-пароль"));
            d->signButton()->click();
        }
        QTRY_VERIFY_WITH_TIMEOUT(ltvDone, 30000);
        QVERIFY(added);
        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification v = ot::test::verifyFile(path, &trust);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        QCOMPARE(v.signatures[0].level, ot::sign::PadesLevel::LT);
        Settings().set(keys::kAutoAddLtv, QStringLiteral("ask"));
    }

private:
    QTemporaryDir tmp_;
    std::unique_ptr<ot::test::PkiNetwork> network_;
};

OT_UI_TEST(SignSettingsTest, "sign_settings");
#include "sign_settings_test.moc"
