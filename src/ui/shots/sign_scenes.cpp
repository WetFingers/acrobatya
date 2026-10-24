// Сцены подписей (план 4.6; эталон 3.8, 6.3 «Подписи», 6.10): инструмент «Использовать сертификат» слева и
// диалоги поверх документа — sign-dialog («Подписать как» над формой demo/form.pdf с удостоверением тестовой
// PKI: пароль запомнен, внешний вид и предпросмотр, итог «Проверить документ»), certify-dialog
// («Сертифицировать» над отчётом), sign-appearance-dialog (конструктор внешнего вида), sign-warnings
// (предупреждения документа sig/warnings.pdf); проверка подписей — sig-bar-valid (отчёт, подписанный при
// снимке тестовой PKI через DocumentSession::sign, корень PKI — доверенный в каталоге данных снимков: синяя
// полоса и значок на поле), sig-bar-problems (sig/signed-bb.pdf — самоподписанный подписант не доверен:
// жёлтая полоса с «Доверять подписанту…»), pane-signatures (панель «Подписи» с раскрытой подписью),
// certificate-viewer (цепочка из трёх сертификатов); Установки → Подписи по вкладкам —
// preferences-identities, preferences-trusted, preferences-tsa, preferences-verification,
// preferences-sign-creation. Удостоверение — tests/fixtures/pki/signer-rsa.pfx в каталоге данных снимков;
// диалоги встраиваются в окно (embedCentered).
#include <QDir>
#include <QFile>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>

#include <algorithm>
#include <exception>
#include <memory>
#include <vector>

#include "core/sign/pkcs12.h"
#include "core/sign/signer.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/identities.h"
#include "services/trust_service.h"
#include "ui/dialogs/certificate_viewer.h"
#include "ui/panes/signatures_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/shell/preferences/signatures_page.h"
#include "ui/shell/signature_status.h"
#include "ui/shots/scenes.h"
#include "ui/tools/certificate/appearance_dialog.h"
#include "ui/tools/certificate/certificate_tool.h"
#include "ui/tools/certificate/sign_dialog.h"
#include "ui/tools/certificate/sign_placement.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/tools/certificate/warnings_dialog.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"

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

QByteArray readAll(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Корень тестовой PKI — доверенный в каталоге данных снимков.
void trustRoot() {
    const QByteArray root = readAll(sceneFixture(QStringLiteral("pki/root.cer")));
    if (root.isEmpty()) {
        return;
    }
    try {
        TrustService::shared().edit([&](sign::TrustStore& s) {
            s.add(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(root.constData()),
                                                static_cast<std::size_t>(root.size())),
                  sign::kTrustSignatures | sign::kTrustCertification);
        });
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): без якоря — полоса жёлтая
    }
}

// Копия фикстуры в каталоге данных снимков (ASCII-имя).
QString sceneCopy(const QString& fixture, const QString& name) {
    const QString dir = QDir(app::dataDir()).filePath(QStringLiteral("scene-docs"));
    QDir().mkpath(dir);
    const QString to = QDir(dir).filePath(name);
    QFile::remove(to);
    QFile::copy(sceneFixture(fixture), to);
    QFile(to).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return to;
}

// Открыть отчёт и подписать его удостоверением PKI (видимая подпись вверху первой страницы) — по-настоящему,
// через DocumentSession::sign; проверку и полосу делает вкладка.
void openSigned(MainWindow& w, const QString& name) {
    trustRoot();
    w.openFile(sceneCopy(QStringLiteral("demo/report.pdf"), name));
    DocumentView* doc = w.currentDocument();
    const QByteArray pfx = readAll(sceneFixture(QStringLiteral("pki/signer-rsa.pfx")));
    if (doc == nullptr || pfx.isEmpty()) {
        return;
    }
    sign::SignRequest r;
    try {
        r.identity = std::make_shared<const sign::SigningKey>(sign::loadPkcs12(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(pfx.constData()),
                                          static_cast<std::size_t>(pfx.size())),
            "acrobatya"));
    } catch (const std::exception&) {
        return;
    }
    r.level = sign::SignLevel::B;
    r.reason = MainWindow::tr("Я утверждаю этот документ").toStdString();
    r.location = MainWindow::tr("Москва").toStdString();
    r.placement.page = 0;
    r.placement.rect = {340, 36, 556, 96};
    r.appearance.labels = SignTexts::appearanceLabels();
    (void)doc->session().sign(std::move(r));
}

bool signedReady(QWidget* w) {
    auto* window = static_cast<MainWindow*>(w);
    DocumentView* doc = window->currentDocument();
    if (!sceneDocumentReady(w) || doc == nullptr || !doc->signatures().hasResult() ||
        doc->signatures().isVerifying() || doc->signatures().bar() == nullptr) {
        return false;
    }
    // Кнопки полосы показываются отложенно (QLayout) — кадр после них.
    const QList<QPushButton*> buttons = doc->signatures().bar()->findChildren<QPushButton*>();
    return std::any_of(buttons.begin(), buttons.end(), [](const QPushButton* b) { return b->isVisible(); });
}

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

    Scene valid;
    valid.name = QStringLiteral("sig-bar-valid");
    valid.make = withMainWindow([](MainWindow& w) {
        w.navPane().closePanel();
        sign_scenes::openSigned(w, QStringLiteral("signed-valid.pdf"));
    });
    valid.ready = sign_scenes::signedReady;
    scenes.push_back(valid);

    Scene problems;
    problems.name = QStringLiteral("sig-bar-problems");
    problems.make = withMainWindow([](MainWindow& w) {
        w.navPane().closePanel();
        w.openFile(
            sign_scenes::sceneCopy(QStringLiteral("sig/signed-bb.pdf"), QStringLiteral("signed-bb.pdf")));
    });
    problems.ready = sign_scenes::signedReady;
    scenes.push_back(problems);

    Scene pane;
    pane.name = QStringLiteral("pane-signatures");
    pane.make = withMainWindow([](MainWindow& w) {
        sign_scenes::openSigned(w, QStringLiteral("signed-pane.pdf"));
        w.navPane().showPane(QStringLiteral("signatures"));
    });
    pane.ready = [](QWidget* w) {
        if (!sign_scenes::signedReady(w)) {
            return false;
        }
        auto* window = static_cast<MainWindow*>(w);
        auto* p = qobject_cast<SignaturesPane*>(window->navPane().page(QStringLiteral("signatures")));
        QTreeWidgetItem* item = p != nullptr ? p->itemFor(QStringLiteral("Signature1")) : nullptr;
        if (item == nullptr) {
            return false;
        }
        item->setExpanded(true);
        return true;
    };
    scenes.push_back(pane);

    Scene viewer;
    viewer.name = QStringLiteral("certificate-viewer");
    viewer.make = withMainWindow(
        [](MainWindow& w) { sign_scenes::openSigned(w, QStringLiteral("signed-viewer.pdf")); });
    // Когда подпись проверена — просмотр сертификата поверх окна.
    viewer.ready = [](QWidget* w) {
        if (!sign_scenes::signedReady(w)) {
            return false;
        }
        if (w->findChild<CertificateViewer*>() != nullptr) {
            return true;
        }
        auto* window = static_cast<MainWindow*>(w);
        window->navPane().showPane(QStringLiteral("signatures"));
        auto* p = qobject_cast<SignaturesPane*>(window->navPane().page(QStringLiteral("signatures")));
        if (p != nullptr) {
            if (CertificateViewer* v = p->showCertificate(QStringLiteral("Signature1"))) {
                embedCentered(*window, v);
            }
        }
        return false;
    };
    scenes.push_back(viewer);

    // Установки → Подписи по вкладкам (4.6, п. 3): удостоверение PKI и корень PKI — в каталоге данных
    // снимков.
    const std::pair<const char*, const char*> prefs[] = {
        {"preferences-identities", "identities"},
        {"preferences-trusted", "trusted"},
        {"preferences-tsa", "tsa"},
        {"preferences-verification", "verification"},
        {"preferences-sign-creation", "creation"},
    };
    for (const auto& [name, tab] : prefs) {
        Scene s;
        s.name = QLatin1String(name);
        const QString id = QLatin1String(tab);
        s.make = withMainWindow([id](MainWindow& w) {
            sign_scenes::ensureIdentity();
            sign_scenes::trustRoot();
            w.openFile(sceneFixture(QStringLiteral("demo/report.pdf")));
            QTimer::singleShot(0, &w, [&w, id] {
                PreferencesDialog* d = w.showPreferences(QStringLiteral("signatures"));
                if (auto* page = qobject_cast<SignaturesPage*>(d->page(QStringLiteral("signatures")))) {
                    page->showTab(id);
                }
                embedCentered(w, d);
            });
        });
        s.ready = sceneDocumentReady;
        scenes.push_back(s);
    }

    // Главные экраны этапа 4 — и в тёмной теме (эталон 6.10; план 4.6, п. 4).
    const std::vector<Scene> light = scenes;
    for (const char* name : {"sign-dialog", "sig-bar-problems", "pane-signatures", "certificate-viewer",
                             "preferences-trusted"}) {
        for (const Scene& s : light) {
            if (s.name == QLatin1String(name)) {
                Scene dark = s;
                dark.name = s.name + QStringLiteral("-dark");
                dark.theme = ThemeChoice::Dark;
                scenes.push_back(dark);
            }
        }
    }
    return scenes;
}

}  // namespace ot::ui
