// ui.script_edit — действия --script редактора (план 3.4, п. 4; рецепт «Новое действие --script»; Р2):
// сценарий tests/scripts/edit_insert_replace.json — edit.insertText вставляет «Hello, » в начало первого
// абзаца latin.pdf, edit.replaceAll меняет «paragraph» на «passage»; копия с правками несёт оба изменения,
// копия после doc.undo {n: 2} — исходный текст; блок вне страницы и «ничего не найдено» — ошибка шага с его
// именем.
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "core/doc/document.h"
#include "core/text/stext.h"
#include "services/script_runner.h"
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

std::string fileText(const QString& path) {
    auto doc = ot::Document::openOrThrow(std::filesystem::path(path.toStdWString()));
    std::string out;
    for (int p = 0; p < doc->pageCount(); ++p) {
        out += ot::TextPage::build(*doc, p, ot::TextOptions{})->text();
    }
    return out;
}

void showWindow(MainWindow& w) {
    w.setCloseConfirm([](ot::ui::DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    w.resize(1440, 900);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

}  // namespace

class ScriptEditTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_edit"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void insertAndReplaceScenario() {
        std::printf("OT_FEATURE:edit-05\n");
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        QVERIFY(runner.hasAction(QStringLiteral("edit.insertText")));
        QVERIFY(runner.hasAction(QStringLiteral("edit.replaceAll")));
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r = runner.runFile(scriptPath("edit_insert_replace.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        const std::string edited = fileText(QDir(out.path()).filePath(QStringLiteral("edited.pdf")));
        QVERIFY2(edited.find("Hello, The editor") != std::string::npos, edited.c_str());
        QVERIFY(edited.find("passage") != std::string::npos);
        QVERIFY(edited.find("paragraph") == std::string::npos);
        const std::string undone = fileText(QDir(out.path()).filePath(QStringLiteral("undone.pdf")));
        QVERIFY(undone.find("Hello, ") == std::string::npos);
        QVERIFY(undone.find("paragraph") != std::string::npos);
    }

    void badStepsFailWithName() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        ScriptContext ctx;
        ctx.scriptDir = QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts");
        const QByteArray outOfRange = R"([
            {"cmd": "doc.open", "args": {"path": "../fixtures/edit/latin.pdf"}},
            {"cmd": "edit.insertText", "args": {"page": 1, "block": 99, "text": "x"}, "name": "далеко"}])";
        ScriptResult r = runner.run(QJsonDocument::fromJson(outOfRange), ctx);
        QVERIFY(!r.ok);
        QVERIFY2(r.describe().contains(QStringLiteral("далеко")), qPrintable(r.describe()));
        QVERIFY(r.describe().contains(QStringLiteral("out of range")));
        const QByteArray nothing = R"([
            {"cmd": "edit.replaceAll", "args": {"find": "no-such-word-xyz", "replace": "x"}, "name": "пусто"}])";
        r = runner.run(QJsonDocument::fromJson(nothing), ctx);
        QVERIFY(!r.ok);
        QVERIFY(r.describe().contains(QStringLiteral("nothing replaced")));
        // Аргумент не по схеме — до выполнения.
        r = runner.run(QJsonDocument::fromJson(
                           R"([{"cmd": "edit.insertText", "args": {"page": 0, "block": 1, "text": "x"}}])"),
                       ctx);
        QVERIFY(!r.ok);
    }
};

OT_UI_TEST(ScriptEditTest, "script_edit");
#include "script_edit_test.moc"
