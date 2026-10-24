// Сцены подписей (план 4.6; эталон 3.8, 6.3 «Подписи», 6.10): инструмент «Использовать сертификат» слева и
// диалоги поверх документа — sign-dialog («Подписать как» над формой demo/form.pdf с удостоверением тестовой
// PKI: пароль запомнен, внешний вид и предпросмотр, итог «Проверить документ»), certify-dialog
// («Сертифицировать» над отчётом), sign-appearance-dialog (конструктор внешнего вида), sign-warnings
// (предупреждения документа sig/warnings.pdf). Удостоверение — tests/fixtures/pki/signer-rsa.pfx в каталоге
// данных снимков; диалоги встраиваются в окно (embedCentered).
#include <QTimer>

#include <exception>
#include <vector>

#include "services/identities.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/certificate/appearance_dialog.h"
#include "ui/tools/certificate/certificate_tool.h"
#include "ui/tools/certificate/sign_dialog.h"
#include "ui/tools/certificate/sign_placement.h"
#include "ui/tools/certificate/warnings_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace sign_scenes {

CertificateTool& tool() {
    return *static_cast<CertificateTool*>(ToolRegistry::instance().find(QStringLiteral("certificate")));
}

// Удостоверение тестовой PKI с запомненным паролем (один раз на каталог данных снимков).
void ensureIdentity() {
    const QString pfx = sceneFixture(QStringLiteral("pki/signer-rsa.pfx"));
    if (pfx.isEmpty()) {
        return;
    }
    try {
        Identities::shared().with([&](sign::IdentityStore& s) {
            if (s.list().empty()) {
                s.addPkcs12(std::filesystem::path(pfx.toStdWString()), "acrobatya", true);
            }
        });
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без удостоверения — пустой список
    }
}

// Открыть документ, включить инструмент и начать подпись (рамка — rect на странице 0).
void startSigning(MainWindow& w, const QString& fixture, bool certify) {
    ensureIdentity();
    w.openFile(sceneFixture(fixture));
    QTimer::singleShot(0, &w, [&w, certify] {
        DocumentView* doc = w.currentDocument();
        if (doc == nullptr) {
            return;
        }
        ToolRegistry::instance().activate(QStringLiteral("certificate"), doc);
        if (certify) {
            tool().startCertify(true);
            CertificateTool::placementOf(doc->view()).place(0, {300, 600, 534, 690});
        } else {
            tool().placeSignature(0, {300, 600, 534, 690});
        }
        if (QDialog* d = tool().dialog()) {
            embedCentered(w, d);
        }
    });
}

SignDialog* dialogOf(QWidget* w) { return w->findChild<SignDialog*>(); }

}  // namespace sign_scenes

std::vector<Scene> signScenes() {
    std::vector<Scene> scenes;

    Scene sign;
    sign.name = QStringLiteral("sign-dialog");
    sign.make = withMainWindow(
        [](MainWindow& w) { sign_scenes::startSigning(w, QStringLiteral("demo/form.pdf"), false); });
    sign.ready = [](QWidget* w) {
        SignDialog* d = sign_scenes::dialogOf(w);
        return sceneDocumentReady(w) && d != nullptr && d->warnings().has_value();
    };
    scenes.push_back(sign);

    Scene certify;
    certify.name = QStringLiteral("certify-dialog");
    certify.make = withMainWindow(
        [](MainWindow& w) { sign_scenes::startSigning(w, QStringLiteral("demo/report.pdf"), true); });
    certify.ready = [](QWidget* w) {
        SignDialog* d = sign_scenes::dialogOf(w);
        return sceneDocumentReady(w) && d != nullptr && d->warnings().has_value();
    };
    scenes.push_back(certify);

    Scene look;
    look.name = QStringLiteral("sign-appearance-dialog");
    look.make = withMainWindow([](MainWindow& w) {
        sign_scenes::startSigning(w, QStringLiteral("demo/form.pdf"), false);
        QTimer::singleShot(0, &w, [&w] {
            if (SignDialog* d = sign_scenes::dialogOf(&w)) {
                d->newAppearanceButton()->click();
                if (auto* a = d->findChild<AppearanceDialog*>()) {
                    a->nameField()->setText(AppearanceDialog::tr("Рабочая подпись"));
                    embedCentered(w, a);
                }
            }
        });
    });
    look.ready = [](QWidget* w) {
        return sceneDocumentReady(w) && w->findChild<AppearanceDialog*>() != nullptr;
    };
    scenes.push_back(look);

    Scene warnings;
    warnings.name = QStringLiteral("sign-warnings");
    warnings.make = withMainWindow(
        [](MainWindow& w) { sign_scenes::startSigning(w, QStringLiteral("sig/warnings.pdf"), false); });
    // Когда проверка документа пришла — список поверх диалога.
    warnings.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        SignDialog* d = sign_scenes::dialogOf(w);
        if (!sceneDocumentReady(w) || d == nullptr || !d->warnings().has_value()) {
            return false;
        }
        if (w->findChild<WarningsDialog*>() == nullptr) {
            if (QDialog* list = d->showWarnings()) {
                embedCentered(*window, list);
            }
            return false;
        }
        return true;
    };
    scenes.push_back(warnings);
    return scenes;
}

}  // namespace ot::ui
