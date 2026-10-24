// ui.script_sign — действия --script подписей (план 4.6, п. 4; эталон 2 — сценарии 4 и 5; рубеж Р2):
// сценарий tests/scripts/sign.json со службой PKI в процессе — подпись B-T в ${OUT_DIR}/scenario4.pdf
// действительна с меткой и причиной, чужой signed-bb.pdf — «проблемы», сертификация P=2 и метка времени
// документа — сертифицирован, две подписи (подпись и метка); неверный пароль и чужой ожидаемый итог — ошибка
// шага с его именем.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "services/crypto_worker.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "support/sign_ui.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/main_window.h"

using ot::ScriptContext;
using ot::ScriptResult;
using ot::ScriptRunner;
using ot::ui::MainWindow;

namespace {

QString scriptPath(const char* name) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts/") + QLatin1String(name);
}

void showWindow(MainWindow& w) {
    w.setCloseConfirm([](ot::ui::DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    w.resize(1440, 900);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

}  // namespace

class ScriptSignTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_sign"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        ot::Settings().set(ot::settings::kAutoAddLtv, QStringLiteral("never"));
        network_ = std::make_unique<ot::test::PkiNetwork>();
    }

    void cleanupTestCase() {
        ot::CryptoWorker::shared().waitForDone();
        network_.reset();
    }

    void scenarios4And5() {
        std::printf("OT_FEATURE:sign-12\n");
        std::printf("OT_FEATURE:sign-25\n");
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        for (const char* id :
             {"sign.sign", "sign.certify", "sign.timestamp", "sign.verifyAll", "sign.trust"}) {
            QVERIFY2(runner.hasAction(QLatin1String(id)), id);
        }
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r = runner.runFile(scriptPath("sign.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        QVERIFY(network_->tsaRequests() >= 2);  // метка подписи и метка документа

        const ot::sign::TrustStore trust = ot::test::pkiTrust();
        const ot::sign::DocumentVerification signedDoc =
            ot::test::verifyFile(QDir(out.path()).filePath(QStringLiteral("scenario4.pdf")), &trust);
        QCOMPARE(signedDoc.status, ot::sign::DocumentStatus::Valid);
        QCOMPARE(static_cast<int>(signedDoc.signatures.size()), 1);
        QCOMPARE(signedDoc.signatures[0].level, ot::sign::PadesLevel::T);
        QCOMPARE(QString::fromStdString(signedDoc.signatures[0].reason),
                 QStringLiteral("Я утверждаю этот документ"));
        QVERIFY(signedDoc.signatures[0].visible);
        QVERIFY(QFile::exists(QDir(out.path()).filePath(QStringLiteral("scenario4.png"))));

        const ot::sign::DocumentVerification certified =
            ot::test::verifyFile(QDir(out.path()).filePath(QStringLiteral("certified.pdf")), &trust);
        QCOMPARE(certified.status, ot::sign::DocumentStatus::Certified);
        QCOMPARE(static_cast<int>(certified.signatures.size()), 2);
        QCOMPARE(certified.signatures[1].kind, ot::sign::SignatureKind::DocTimestamp);
        QCOMPARE(certified.docMdp, ot::sign::MdpPermission::FormFilling);
    }

    void badStepsFailWithName() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        QTemporaryDir out;
        ScriptContext ctx;
        ctx.outDir = out.path();
        ctx.scriptDir = QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts");
        const QByteArray wrongPassword = R"({"steps": [
            {"cmd": "doc.open", "args": {"path": "../fixtures/sig/unsigned.pdf"}},
            {"cmd": "sign.sign", "args": {"pfx": "../fixtures/pki/signer-rsa.pfx", "password": "nope",
             "level": "B", "target": "${OUT_DIR}/x.pdf"}, "name": "неверный пароль"}]})";
        ScriptResult r = runner.run(QJsonDocument::fromJson(wrongPassword), ctx);
        QVERIFY(!r.ok);
        QCOMPARE(r.failedStep, 2);
        QCOMPARE(r.name, QStringLiteral("неверный пароль"));
        const QByteArray wrongExpect = R"({"steps": [
            {"cmd": "doc.open", "args": {"path": "../fixtures/sig/signed-bb.pdf"}},
            {"cmd": "sign.verifyAll", "args": {"expect": "valid"}, "name": "ждали действительную"}]})";
        r = runner.run(QJsonDocument::fromJson(wrongExpect), ctx);
        QVERIFY(!r.ok);
        QCOMPARE(r.name, QStringLiteral("ждали действительную"));
        QVERIFY2(r.message.contains(QStringLiteral("problems")), qPrintable(r.message));
        const QByteArray badArgs = R"({"steps": [{"cmd": "sign.certify", "args": {"pfx": "a.pfx",
            "password": "x", "perms": 4}, "name": "права вне 1–3"}]})";
        r = runner.run(QJsonDocument::fromJson(badArgs), ctx);
        QVERIFY(!r.ok);
        QCOMPARE(r.name, QStringLiteral("права вне 1–3"));
    }

private:
    std::unique_ptr<ot::test::PkiNetwork> network_;
};

OT_UI_TEST(ScriptSignTest, "script_sign");
#include "script_sign_test.moc"
