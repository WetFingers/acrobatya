// ui.script_runner — сценарии --script над окном (план 2.4b; эталон 3.18, 9.6): базовые действия doc.*,
// view.*, shot, command.run на настоящем MainWindow; tests/scripts/open_zoom_shot.json снимает PNG 1440 ×
// 900; неизвестный cmd — ошибка с номером и именем шага до выполнения; doc.setProperty → doc.undo → копия
// равна исходнику по qpdf --json (а копия с правкой несёт новый Title); тот же сценарий через AcroBatya.exe
// --script (код 0, PNG в --out; неизвестный cmd — код ≠ 0 и имя шага в stderr).
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>

#include "core/optimize/qpdf_tool.h"
#include "services/script_runner.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/view/page_view.h"

using ot::ScriptContext;
using ot::ScriptResult;
using ot::ScriptRunner;
using ot::ui::DocumentView;
using ot::ui::MainWindow;

namespace {

QString scriptPath(const char* name) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts/") + QLatin1String(name);
}

void showWindow(MainWindow& w) {
    w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    w.resize(1440, 900);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

// Объекты PDF по qpdf --json (без /ID трейлера — время записи).
QJsonObject qpdfObjects(const QString& file) {
    const ot::QpdfTool qpdf;
    const ot::ToolResult r = qpdf.json(std::filesystem::path(file.toStdWString()), "qpdf");
    if (r.exitCode != 0) {
        qWarning("qpdf --json %s: %s", qPrintable(file), r.output.c_str());
        return {};
    }
    QJsonObject objects = QJsonDocument::fromJson(QByteArray::fromStdString(r.output))
                              .object()
                              .value(QLatin1String("qpdf"))
                              .toArray()
                              .at(1)
                              .toObject();
    QJsonObject trailer = objects.value(QLatin1String("trailer")).toObject();
    QJsonObject value = trailer.value(QLatin1String("value")).toObject();
    value.remove(QStringLiteral("/ID"));
    trailer.insert(QStringLiteral("value"), value);
    objects.insert(QStringLiteral("trailer"), trailer);
    return objects;
}

QString exePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AcroBatya.exe"));
}

}  // namespace

class ScriptRunnerTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_runner"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void openZoomShotScript() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        for (const char* id : {"doc.open", "doc.save", "doc.saveCopy", "doc.close", "doc.undo", "doc.redo",
                               "doc.setProperty", "view.zoom", "view.zoomFit", "view.zoomIn", "view.zoomOut",
                               "view.page", "view.layout", "view.rotate", "command.run", "shot", "wait"}) {
            QVERIFY2(runner.hasAction(QLatin1String(id)), id);
        }
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r = runner.runFile(scriptPath("open_zoom_shot.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        QCOMPARE(r.done, 7);
        for (const char* name : {"open-zoom-page.png", "open-zoom-width-p3.png"}) {
            const QImage png(QDir(out.path()).filePath(QLatin1String(name)));
            QVERIFY2(!png.isNull(), name);
            QCOMPARE(png.size(), QSize(1440, 900));
        }
        QCOMPARE(w.documentCount(), 0);  // doc.close
    }

    void viewActionsChangeTheView() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        ScriptContext ctx;
        ctx.scriptDir = QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts");
        const ScriptResult r = runner.run(QJsonDocument::fromJson(R"([
            {"cmd": "doc.open", "args": {"path": "../fixtures/demo/report.pdf", "page": 2}},
            {"cmd": "view.zoom", "args": {"percent": 150}},
            {"cmd": "view.layout", "args": {"mode": "facing"}},
            {"cmd": "view.rotate", "args": {"degrees": 90}},
            {"cmd": "command.run", "args": {"id": "view.readMode"}}
        ])"),
                                          ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        ot::ui::PageView& v = w.currentDocument()->view();
        QCOMPARE(qRound(v.zoomPercent()), 150);
        QCOMPARE(v.pageMode(), ot::ui::PageMode::Facing);
        QCOMPARE(v.viewRotation(), 90);
        QVERIFY(w.isReadMode());
        w.setReadMode(false);
        // Страница за пределами документа, стадия-заглушка, документа нет — ошибки шага.
        ScriptResult bad =
            runner.run(QJsonDocument::fromJson(R"([{"cmd": "view.page", "args": {"page": 999}}])"), ctx);
        QVERIFY(!bad.ok);
        QVERIFY2(bad.message.startsWith(QStringLiteral("page 999 of ")), qPrintable(bad.message));
        bad = runner.run(QJsonDocument::fromJson(R"([{"cmd": "command.run", "args": {"id": "doc.print"}}])"),
                         ctx);
        QVERIFY(!bad.ok);
        QVERIFY2(bad.message.contains(QStringLiteral("arrives in version 0.2")), qPrintable(bad.message));
        w.closeAllDocuments();
        bad = runner.run(QJsonDocument::fromJson(R"([{"cmd": "view.zoomIn"}])"), ctx);
        QVERIFY(!bad.ok);
        QVERIFY(bad.message.startsWith(QStringLiteral("no open document")));
    }

    void unknownCommandNamesTheStep() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        ScriptContext ctx;
        const ScriptResult r = runner.runFile(scriptPath("unknown_command.json"), ctx);
        QVERIFY(!r.ok);
        QCOMPARE(r.failedStep, 2);
        QCOMPARE(r.cmd, QStringLiteral("view.bogus"));
        QCOMPARE(r.describe(), QStringLiteral("step 2 view.bogus \"нет такого действия\": unknown command"));
        QCOMPARE(w.documentCount(), 0);  // doc.open (шаг 1) не выполнен
    }

    void setPropertyUndoEqualsSourceByQpdfJson() {
        if (!ot::QpdfTool().available()) {
            QSKIP("qpdf.exe not found");
        }
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r = runner.runFile(scriptPath("property_undo.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        const QDir dir(out.path());
        const QJsonObject before = qpdfObjects(dir.filePath(QStringLiteral("before.pdf")));
        const QJsonObject edited = qpdfObjects(dir.filePath(QStringLiteral("edited.pdf")));
        const QJsonObject undone = qpdfObjects(dir.filePath(QStringLiteral("undone.pdf")));
        QVERIFY(!before.isEmpty() && !edited.isEmpty() && !undone.isEmpty());
        QVERIFY2(before == undone, "doc.setProperty → doc.undo → copy must equal the copy before the edit");
        QVERIFY(before != edited);
        const QByteArray editedJson = QJsonDocument(edited).toJson(QJsonDocument::Compact);
        // Title пишется в /Info (строка UTF-16BE с BOM — qpdf показывает её как u:…) и в XMP.
        QVERIFY2(
            editedJson.contains("u:Заголовок из сценария") || editedJson.contains("Заголовок из сценария"),
            "edited copy carries the new Title");
    }

    void exeRunsScriptsWithExitCodes() {
        const QString exe = exePath();
        if (!QFileInfo::exists(exe)) {
            QSKIP("AcroBatya.exe is not built next to ui_tests");
        }
        QTemporaryDir out;
        QVERIFY(out.isValid());
        QProcess ok;
        ok.start(exe, {QStringLiteral("--script"), scriptPath("open_zoom_shot.json"), QStringLiteral("--out"),
                       out.path()});
        QVERIFY(ok.waitForFinished(120000));
        QCOMPARE(ok.exitCode(), 0);
        QVERIFY2(QString::fromUtf8(ok.readAllStandardOutput()).contains(QStringLiteral("ok: 7 steps")),
                 qPrintable(QString::fromUtf8(ok.readAllStandardError())));
        QVERIFY(QFileInfo::exists(QDir(out.path()).filePath(QStringLiteral("open-zoom-page.png"))));
        QProcess bad;
        bad.start(exe, {QStringLiteral("--script"), scriptPath("unknown_command.json")});
        QVERIFY(bad.waitForFinished(120000));
        QVERIFY(bad.exitCode() != 0);
        const QString err = QString::fromUtf8(bad.readAllStandardError());
        QVERIFY2(err.contains(QStringLiteral("step 2 view.bogus \"нет такого действия\": unknown command")),
                 qPrintable(err));
    }
};

OT_UI_TEST(ScriptRunnerTest, "script_runner");
#include "script_runner_test.moc"
