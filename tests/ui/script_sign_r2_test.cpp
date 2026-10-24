// ui.script_sign_r2 — помощник рубежа Р2 (tools/verify_r2.py, проверка sign): сценарий --script из
// OTTISK_R2_SIGN_SCRIPT (действия sign.sign/sign.certify 4.6) со службой PKI в процессе — уровни B-LT и B-LTA
// тянут OCSP/CRL тестовой PKI по адресам http://pki.acrobatya.test/… (домен .test в сети не живёт), поэтому
// их нельзя сделать запуском AcroBatya.exe --script, как B-T с локальной tsa_serve.py. Без переменной —
// пропуск (обычный прогон ctest ничего не делает, как DISABLED-помощники unit_tests Р1). ${OUT_DIR} —
// OTTISK_R2_OUT.
#include <QDir>
#include <QtTest/QtTest>

#include "services/crypto_worker.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "support/sign_ui.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/main_window.h"

class ScriptSignR2Test : public QObject {
    Q_OBJECT
private slots:
    void runFromEnvironment() {
        const QString script = qEnvironmentVariable("OTTISK_R2_SIGN_SCRIPT");
        if (script.isEmpty()) {
            QSKIP("OTTISK_R2_SIGN_SCRIPT is not set (helper of tools/verify_r2.py)");
        }
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_sign_r2"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        ot::Settings().set(ot::settings::kAutoAddLtv, QStringLiteral("never"));
        {
            ot::test::PkiNetwork network;
            ot::ui::MainWindow w;
            w.setCloseConfirm([](ot::ui::DocumentView&) { return ot::ui::MainWindow::CloseAnswer::Discard; });
            w.resize(1440, 900);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            ot::ScriptRunner runner;
            ot::ui::registerBaseActions(runner, w);
            ot::ScriptContext ctx;
            ctx.outDir = qEnvironmentVariable("OTTISK_R2_OUT", QDir::tempPath());
            const ot::ScriptResult r = runner.runFile(script, ctx);
            std::printf("r2 sign script: %s, tsa requests %d\n", r.ok ? "ok" : "failed",
                        network.tsaRequests());
            QVERIFY2(r.ok, qPrintable(r.describe()));
            ot::CryptoWorker::shared().waitForDone();
        }
    }
};

OT_UI_TEST(ScriptSignR2Test, "script_sign_r2");
#include "script_sign_r2_test.moc"
