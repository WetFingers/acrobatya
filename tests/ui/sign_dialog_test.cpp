// ui.sign_dialog — инструмент «Использовать сертификат» и диалоги подписания (план 4.6, п. 1; эталон 3.8
// «Инструмент», «Диалог подписания», 2 — сценарий 4; sign-12, sign-13, sign-14, sign-16, sign-17, sign-20,
// sign-05): рамка мышью → «Подписать как» с .pfx тестовой PKI и меткой службы времени (служба PKI в процессе
// отвечает на адрес DigiCert по умолчанию) → «Сохранить как» → файл подписан B-T, причина в /Reason, вкладка
// на новом файле; щелчок по пустому полю формы подписывает его; «Сертифицировать» (невидимая) P=2 — DocMDP
// в файле; метка времени документа; служба времени недоступна — вопрос «подписать без метки?» из потока
// документа → B-B; «Создать самоподписанное» и конструктор внешнего вида; «Проверить документ» —
// предупреждения и «Просмотреть»; защищённый просмотр — действия серые с причиной; выбор сертификата Windows
// в offscreen — кнопка серая с подписью.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtTest/QtTest>

#include "services/app_info.h"
#include "services/document_session.h"
#include "services/identities.h"
#include "services/settings.h"
#include "services/sign_settings.h"
#include "support/sign_ui.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/tools/certificate/appearance_dialog.h"
#include "ui/tools/certificate/certificate_tool.h"
#include "ui/tools/certificate/certify_dialog.h"
#include "ui/tools/certificate/new_identity_dialog.h"
#include "ui/tools/certificate/sign_dialog.h"
#include "ui/tools/certificate/sign_placement.h"
#include "ui/tools/certificate/timestamp_dialog.h"
#include "ui/tools/certificate/warnings_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::ui::CertificateTool;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::SignDialog;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

CertificateTool& certTool() {
    return *static_cast<CertificateTool*>(ToolRegistry::instance().find(QStringLiteral("certificate")));
}

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
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("certificate"), doc));
    }();
    return doc;
}

// Диалог открыт инструментом — дождаться и привести тип.
template <class D>
D* waitDialog() {
    D* d = nullptr;
    [&] { QTRY_VERIFY_WITH_TIMEOUT((d = qobject_cast<D*>(certTool().dialog())) != nullptr, 10000); }();
    return d;
}

struct Finished {
    bool done = false;
    bool ok = false;
    QString message;
};

}  // namespace

class SignDialogTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-sign_dialog"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
        // Сведения LTV после подписи — отдельный тест (ui.sign_settings): здесь уровни ровно те, что выбраны.
        ot::Settings().set(ot::settings::kAutoAddLtv, QStringLiteral("never"));
        QVERIFY(tmp_.isValid());
        network_ = std::make_unique<ot::test::PkiNetwork>();
        connect(&certTool(), &CertificateTool::finished, this, [this](bool ok, const QString& message) {
            finished_.done = true;
            finished_.ok = ok;
            finished_.message = message;
        });
    }

    void cleanupTestCase() { network_.reset(); }

    void init() {
        finished_ = {};
        certTool().setTimestampPrompt({});
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void windowsPickerDisabledOffscreen() {
        std::printf("OT_FEATURE:sign-08\n");
        MainWindow w;
        DocumentView* doc = openDoc(w, ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                             QStringLiteral("win.pdf")));
        QVERIFY(doc != nullptr);
        QVERIFY(certTool().startCertify(false));
        auto* d = waitDialog<SignDialog>();
        QVERIFY(d != nullptr);
        QVERIFY(!d->windowsButton()->isEnabled());
        QVERIFY(d->windowsNote()->isVisibleTo(d));
        QVERIFY(d->windowsNote()->text().contains(QStringLiteral("на экране")));
        d->reject();
    }

    // Сценарий 4: рамка мышью → диалог → .pfx → B-T → «Сохранить как».
    void signsWithPfxAndTimestamp() {
        std::printf("OT_FEATURE:sign-12\n");
        std::printf("OT_FEATURE:sign-14\n");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        const QString source = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                     QStringLiteral("договор.pdf"));
        DocumentView* doc = openDoc(w, source);
        QVERIFY(doc != nullptr);
        const QString target = QDir(tmp_.path()).filePath(QStringLiteral("договор-подписан.pdf"));
        certTool().setFileChooser([&](QWidget*, const QString& suggested) {
            [&] { QCOMPARE(suggested, source); }();
            return target;
        });
        QVERIFY(certTool().startSign());
        QCOMPARE(doc->view().tools().tool(), ViewToolId::Sign);
        QVERIFY(certTool().placementBar() != nullptr);
        // Рамка мышью по середине страницы.
        const QRectF page = doc->view().pageViewportRect(0);
        const QPoint from = (page.center() - QPointF(120, 40)).toPoint();
        const QPoint to = (page.center() + QPointF(120, 40)).toPoint();
        QTest::mousePress(doc->view().viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(doc->view().viewport(), (from + to) / 2);
        QTest::mouseMove(doc->view().viewport(), to);
        QTest::mouseRelease(doc->view().viewport(), Qt::LeftButton, {}, to);
        auto* d = waitDialog<SignDialog>();
        QVERIFY(d != nullptr);
        QVERIFY(certTool().placementBar() == nullptr);
        QCOMPARE(d->mode(), SignDialog::Mode::Sign);
        QVERIFY(d->previewLabel()->isVisibleTo(d));
        QVERIFY(d->lockBox()->isVisibleTo(d));
        QVERIFY(!d->permissionBox()->isVisibleTo(d));
        // Удостоверений нет — подсказка и «Подписать» серая; .pfx с паролем — в список.
        QCOMPARE(d->identityBox()->count(), 0);
        QVERIFY(!d->signButton()->isEnabled());
        QVERIFY(!d->addPfx(ot::test::pkiPfx(), QStringLiteral("неверный")));
        QVERIFY(d->errorLabel()->isVisibleTo(d));
        QVERIFY(d->addPfx(ot::test::pkiPfx(), ot::test::pkiPassword()));
        QCOMPARE(d->identityBox()->count(), 1);
        QVERIFY(d->signButton()->isEnabled());
        QVERIFY(d->identityBox()->currentText().contains(QStringLiteral("АкроБатя")));
        QVERIFY(!d->previewLabel()->pixmap().isNull());
        // Уровень по умолчанию — B-T (служба по умолчанию — DigiCert, отвечает служба PKI).
        QCOMPARE(d->level(), ot::sign::SignLevel::T);
        d->reasonBox()->setCurrentText(QStringLiteral("Я утверждаю этот документ"));
        const int before = network_->tsaRequests();
        d->signButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(finished_.done, 30000);
        QVERIFY2(finished_.ok, qPrintable(finished_.message));
        QVERIFY(network_->tsaRequests() > before);
        // Вкладка — на подписанном файле; исходный не тронут.
        QTRY_COMPARE(QDir::cleanPath(doc->path()), QDir::cleanPath(target));
        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification v = ot::test::verifyFile(target, &trust);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        const ot::sign::SignatureInfo& s = v.signatures[0];
        QVERIFY(s.intact);
        QVERIFY(s.trusted);
        QVERIFY(s.timestamp.has_value());
        QCOMPARE(s.level, ot::sign::PadesLevel::T);
        QCOMPARE(QString::fromStdString(s.reason), QStringLiteral("Я утверждаю этот документ"));
        QVERIFY(s.visible);
        QCOMPARE(s.page, 0);
        QVERIFY(s.rect.width() > 100);
        QCOMPARE(v.status, ot::sign::DocumentStatus::Valid);
        QVERIFY(ot::test::verifyFile(source, &trust).signatures.empty());
    }

    // Щелчок по пустому полю «signature» формы подписывает это поле (forms-17).
    void clickOnEmptyFieldSignsIt() {
        std::printf("OT_FEATURE:forms-17\n");
        MainWindow w;
        const QString source =
            ot::test::copyFixture(QStringLiteral("demo/form.pdf"), tmp_.path(), QStringLiteral("анкета.pdf"));
        DocumentView* doc = openDoc(w, source);
        QVERIFY(doc != nullptr);
        certTool().setFileChooser([&](QWidget*, const QString& suggested) { return suggested; });
        QVERIFY(certTool().startSign());
        ot::ui::SignPlacementTool& tool = CertificateTool::placementOf(doc->view());
        QTRY_VERIFY(!tool.fields().empty());
        const auto empty =
            std::find_if(tool.fields().begin(), tool.fields().end(),
                         [](const ot::sign::SignatureField& f) { return !f.isSigned && f.visible; });
        QVERIFY(empty != tool.fields().end());
        const QString name = QString::fromStdString(empty->name);
        const QRectF onPage(QPointF(empty->rect.x0, empty->rect.y0), QPointF(empty->rect.x1, empty->rect.y1));
        doc->view().ensureVisible(empty->page, empty->rect, 20);
        QTest::qWait(50);
        const QPoint click = doc->view().pageTransform(empty->page).mapRect(onPage).center().toPoint();
        QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {}, click);
        auto* d = waitDialog<SignDialog>();
        QVERIFY(d != nullptr);
        QCOMPARE(d->identityBox()->count(), 1);  // удостоверение прошлого теста — в state.sqlite
        d->passwordField()->setText(ot::test::pkiPassword());
        d->signButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(finished_.done, 30000);
        QVERIFY2(finished_.ok, qPrintable(finished_.message));
        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification v = ot::test::verifyFile(source, &trust);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        QCOMPARE(QString::fromStdString(v.signatures[0].field), name);
    }

    // «Сертифицировать (невидимая подпись)» с P=2 (sign-16) и запомненным паролем.
    void certifyInvisibleFormFilling() {
        std::printf("OT_FEATURE:sign-16\n");
        MainWindow w;
        const QString source = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                     QStringLiteral("серт.pdf"));
        DocumentView* doc = openDoc(w, source);
        QVERIFY(doc != nullptr);
        certTool().setFileChooser([](QWidget*, const QString& suggested) { return suggested; });
        QVERIFY(certTool().startCertify(false));
        auto* d = waitDialog<ot::ui::CertifyDialog>();
        QVERIFY(d != nullptr);
        QCOMPARE(d->mode(), SignDialog::Mode::Certify);
        QVERIFY(!d->previewLabel()->isVisibleTo(d));  // невидимая — без внешнего вида
        QVERIFY(d->permissionBox()->isVisibleTo(d));
        QVERIFY(!d->lockBox()->isVisibleTo(d));
        QCOMPARE(d->permission(), ot::sign::MdpPermission::FormFilling);
        d->passwordField()->setText(ot::test::pkiPassword());
        d->rememberBox()->setChecked(true);
        d->signButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(finished_.done, 30000);
        QVERIFY2(finished_.ok, qPrintable(finished_.message));
        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification v = ot::test::verifyFile(source, &trust);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        QCOMPARE(v.signatures[0].kind, ot::sign::SignatureKind::Certification);
        QVERIFY(!v.signatures[0].visible);
        QCOMPARE(v.docMdp, ot::sign::MdpPermission::FormFilling);
        QCOMPARE(v.status, ot::sign::DocumentStatus::Certified);
        // Пароль запомнен: удостоверение открывается без него.
        const bool remembered = ot::Identities::shared().with([](ot::sign::IdentityStore& s) {
            return !s.list().empty() && s.list().front().rememberPassword;
        });
        QVERIFY(remembered);
        // Сертифицированный — второй раз сертифицировать нельзя (кнопка серая с причиной).
        QTRY_VERIFY(!certTool().button(CertificateTool::Action::CertifyVisible)->isEnabled());
        QVERIFY(certTool()
                    .blockReason(CertificateTool::Action::CertifyVisible)
                    .contains(QStringLiteral("сертифицирован")));
    }

    // Служба времени недоступна: вопрос из потока документа → «Да» → подпись без метки (B-B).
    void timestampDownAsksAndSignsWithout() {
        MainWindow w;
        const QString source = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                     QStringLiteral("без-метки.pdf"));
        DocumentView* doc = openDoc(w, source);
        QVERIFY(doc != nullptr);
        certTool().setFileChooser([](QWidget*, const QString& suggested) { return suggested; });
        int asked = 0;
        certTool().setTimestampPrompt([&](const QString& message) {
            ++asked;
            [&] { QVERIFY(!message.isEmpty()); }();
            return true;
        });
        network_->setTsaDown(true);
        certTool().placeSignature(0, {72, 72, 272, 142});
        auto* d = waitDialog<SignDialog>();
        QVERIFY(d != nullptr);
        d->signButton()->click();  // пароль запомнен прошлым тестом
        QTRY_VERIFY_WITH_TIMEOUT(finished_.done, 30000);
        network_->setTsaDown(false);
        QVERIFY2(finished_.ok, qPrintable(finished_.message));
        QCOMPARE(asked, 1);
        QVERIFY(finished_.message.contains(QStringLiteral("Метки времени нет")));
        const ot::sign::DocumentVerification v = ot::test::verifyFile(source, nullptr);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        QVERIFY(!v.signatures[0].timestamp.has_value());
        QCOMPARE(v.signatures[0].level, ot::sign::PadesLevel::B);
    }

    // «Метка времени документа» (sign-20).
    void documentTimestamp() {
        std::printf("OT_FEATURE:sign-20\n");
        MainWindow w;
        const QString source = ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                     QStringLiteral("метка.pdf"));
        DocumentView* doc = openDoc(w, source);
        QVERIFY(doc != nullptr);
        certTool().setFileChooser([](QWidget*, const QString& suggested) { return suggested; });
        QVERIFY(certTool().startTimestamp());
        auto* d = waitDialog<ot::ui::TimestampDialog>();
        QVERIFY(d != nullptr);
        QVERIFY(d->serverBox()->count() >= 3);  // DigiCert, Sectigo, GlobalSign
        QCOMPARE(d->serverBox()->currentText(), QStringLiteral("DigiCert"));
        QVERIFY(d->addButton()->isEnabled());
        d->addButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(finished_.done, 30000);
        QVERIFY2(finished_.ok, qPrintable(finished_.message));
        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification v = ot::test::verifyFile(source, &trust);
        QCOMPARE(static_cast<int>(v.signatures.size()), 1);
        QCOMPARE(v.signatures[0].kind, ot::sign::SignatureKind::DocTimestamp);
        QVERIFY(v.signatures[0].intact);
    }

    // «Создать самоподписанное» (sign-05) и конструктор внешнего вида (sign-13).
    void newIdentityAndAppearance() {
        std::printf("OT_FEATURE:sign-05\n");
        std::printf("OT_FEATURE:sign-13\n");
        MainWindow w;
        DocumentView* doc = openDoc(w, ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                             QStringLiteral("вид.pdf")));
        QVERIFY(doc != nullptr);
        certTool().placeSignature(0, {72, 72, 300, 160});
        auto* d = waitDialog<SignDialog>();
        QVERIFY(d != nullptr);
        const int before = d->identityBox()->count();
        d->createButton()->click();
        ot::ui::NewIdentityDialog* create = nullptr;
        QTRY_VERIFY((create = d->findChild<ot::ui::NewIdentityDialog*>()) != nullptr);
        create->nameField()->setText(QStringLiteral("Мария Петрова"));
        create->algorithmBox()->setCurrentIndex(1);  // ECDSA — быстрее
        create->passwordField()->setText(QStringLiteral("секрет-1"));
        create->confirmField()->setText(QStringLiteral("секрет-2"));
        create->submit();
        QVERIFY(create->errorLabel()->isVisibleTo(create));  // не совпали
        create->confirmField()->setText(QStringLiteral("секрет-1"));
        create->submit();
        QTRY_COMPARE(d->identityBox()->count(), before + 1);
        QVERIFY(d->identityBox()->currentText().contains(QStringLiteral("Мария Петрова")));
        QCOMPARE(d->passwordField()->text(), QStringLiteral("секрет-1"));

        d->newAppearanceButton()->click();
        ot::ui::AppearanceDialog* look = nullptr;
        QTRY_VERIFY((look = d->findChild<ot::ui::AppearanceDialog*>()) != nullptr);
        QVERIFY(!look->previewLabel()->pixmap().isNull());
        look->submit();
        QVERIFY(look->errorLabel()->isVisibleTo(look));  // без названия
        look->nameField()->setText(QStringLiteral("Без даты"));
        look->graphicButton(ot::sign::AppearanceGraphic::None)->click();
        look->textBox(QStringLiteral("date"))->setChecked(false);
        look->submit();
        QTRY_VERIFY(d->appearanceBox()->currentText() == QStringLiteral("Без даты"));
        const ot::sign::SignatureAppearance a = d->appearance();
        QCOMPARE(a.graphic, ot::sign::AppearanceGraphic::None);
        QVERIFY(!a.text.date);
        const std::vector<ot::NamedAppearance> saved = ot::loadAppearances(ot::Settings());
        QCOMPARE(static_cast<int>(saved.size()), 1);
        QCOMPARE(saved[0].name, QStringLiteral("Без даты"));
        QVERIFY(!d->previewLabel()->pixmap().isNull());
        d->reject();
    }

    // «Проверить документ» (sign-17): предупреждения и «Просмотреть» — переход к странице.
    void reviewWarnings() {
        std::printf("OT_FEATURE:sign-17\n");
        MainWindow w;
        DocumentView* doc = openDoc(w, ot::test::copyFixture(QStringLiteral("sig/warnings.pdf"), tmp_.path(),
                                                             QStringLiteral("предупреждения.pdf")));
        QVERIFY(doc != nullptr);
        QVERIFY(certTool().startCertify(false));
        auto* d = waitDialog<SignDialog>();
        QVERIFY(d != nullptr);
        QVERIFY(d->reviewButton()->isVisibleTo(d));
        QTRY_VERIFY_WITH_TIMEOUT(d->warnings().has_value(), 10000);
        QVERIFY(!d->warnings()->clean());
        QVERIFY(d->warningsLabel()->text().contains(QString::number(d->warnings()->warnings.size())));
        auto* list = qobject_cast<ot::ui::WarningsDialog*>(d->showWarnings());
        QVERIFY(list != nullptr);
        QCOMPARE(list->list()->topLevelItemCount(), static_cast<int>(d->warnings()->warnings.size()));
        int pageRow = -1;
        for (int i = 0; i < list->list()->topLevelItemCount(); ++i) {
            if (d->warnings()->warnings[static_cast<std::size_t>(i)].page >= 0) {
                pageRow = i;
            }
        }
        if (pageRow >= 0) {
            int went = -1;
            connect(d, &SignDialog::goToPage, this, [&went](int p) { went = p; });
            list->list()->setCurrentItem(list->list()->topLevelItem(pageRow));
            QVERIFY(list->reviewButton()->isEnabled());
            list->reviewButton()->click();
            QCOMPARE(went, d->warnings()->warnings[static_cast<std::size_t>(pageRow)].page);
        }
        list->reject();
        d->reject();
    }

    // Защищённый просмотр: подпись серая с причиной, поток не начинается.
    void protectedViewBlocksSigning() {
        MainWindow w;
        DocumentView* doc = openDoc(w, ot::test::copyFixture(QStringLiteral("sig/unsigned.pdf"), tmp_.path(),
                                                             QStringLiteral("чужой.pdf")));
        QVERIFY(doc != nullptr);
        w.applyProtectedView(doc, ot::protected_view::Origin::Internet);
        QVERIFY(!certTool().blockReason(CertificateTool::Action::Sign).isEmpty());
        QTRY_VERIFY(!certTool().button(CertificateTool::Action::Sign)->isEnabled());
        QVERIFY(certTool()
                    .button(CertificateTool::Action::Sign)
                    ->toolTip()
                    .contains(QStringLiteral("Защищённый")));
        QVERIFY(certTool().lockLabel()->isVisibleTo(certTool().panel()));
        QVERIFY(!certTool().startSign());
        QVERIFY(doc->view().tools().tool() != ViewToolId::Sign);
    }

private:
    QTemporaryDir tmp_;
    std::unique_ptr<ot::test::PkiNetwork> network_;
    Finished finished_;
};

OT_UI_TEST(SignDialogTest, "sign_dialog");
#include "sign_dialog_test.moc"
