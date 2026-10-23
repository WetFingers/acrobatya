// unit.script_runner.* — движок --script (план 2.4b; эталон 3.18, 9.6): шаги по порядку, аргументы по схеме
// (type, enum, minimum, required, additionalProperties, items), неизвестный cmd и неверные аргументы
// находятся до выполнения (ни одно действие не вызвано) и называют номер и имя шага; исключение действия —
// стоп на нём;
// ${VAR} и пути от каталога скрипта; файл — массив или {"steps": [...]}.
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "services/script_runner.h"
#include "support/qt_app.h"

using ot::ScriptContext;
using ot::ScriptError;
using ot::ScriptResult;
using ot::ScriptRunner;

namespace {

QJsonDocument json(const char* text) { return QJsonDocument::fromJson(QByteArray(text)); }

struct Fixture {
    ScriptRunner runner;
    QStringList calls;
    Fixture() {
        runner.registerAction(QStringLiteral("doc.open"),
                              R"({"type": "object", "properties": {"path": {"type": "string", "minLength": 1},
                                  "password": {"type": "string"}}, "required": ["path"],
                                  "additionalProperties": false})",
                              [this](const QJsonObject& a, ScriptContext& c) {
                                  calls << QStringLiteral("open ") +
                                               c.path(a.value(QLatin1String("path")).toString());
                              });
        runner.registerAction(
            QStringLiteral("view.zoom"),
            R"({"type": "object", "properties": {"percent": {"type": "number", "minimum": 1,
                                  "maximum": 6400}}, "required": ["percent"], "additionalProperties": false})",
            [this](const QJsonObject& a, ScriptContext&) {
                calls << QStringLiteral("zoom %1").arg(a.value(QLatin1String("percent")).toDouble());
            });
        runner.registerAction(
            QStringLiteral("view.layout"),
            R"({"type": "object", "properties": {"mode": {"enum": ["single", "continuous"]},
                                  "pages": {"type": "array", "items": {"type": "integer"}, "minItems": 1}},
                                  "additionalProperties": false})",
            [this](const QJsonObject&, ScriptContext&) { calls << QStringLiteral("layout"); });
        runner.registerAction(
            QStringLiteral("doc.close"), QJsonObject(),
            [this](const QJsonObject&, ScriptContext&) { calls << QStringLiteral("close"); });
        runner.registerAction(QStringLiteral("fail"), QJsonObject(), [](const QJsonObject&, ScriptContext&) {
            throw ScriptError(QStringLiteral("nothing to undo"));
        });
    }
};

}  // namespace

TEST(script_runner, runs_steps_in_order) {
    ot::test::qtApp();
    Fixture f;
    ScriptContext ctx;
    ctx.scriptDir = QStringLiteral("C:/scripts");
    const ScriptResult r = f.runner.run(json(R"([
        {"cmd": "doc.open", "args": {"path": "report.pdf"}, "name": "open report"},
        {"cmd": "view.zoom", "args": {"percent": 150}},
        {"cmd": "view.layout", "args": {"mode": "single", "pages": [1, 2]}},
        {"cmd": "doc.close"}
    ])"),
                                        ctx);
    ASSERT_TRUE(r.ok) << r.describe().toStdString();
    EXPECT_EQ(r.steps, 4);
    EXPECT_EQ(r.done, 4);
    EXPECT_EQ(f.calls, (QStringList{QStringLiteral("open C:/scripts/report.pdf"), QStringLiteral("zoom 150"),
                                    QStringLiteral("layout"), QStringLiteral("close")}));
    EXPECT_EQ(r.describe(), QStringLiteral("ok: 4 steps"));
}

TEST(script_runner, unknown_command_fails_before_any_step_with_its_name) {
    ot::test::qtApp();
    Fixture f;
    ScriptContext ctx;
    const ScriptResult r = f.runner.run(json(R"({"steps": [
        {"cmd": "doc.open", "args": {"path": "a.pdf"}},
        {"cmd": "doc.bogus", "name": "the broken one"}
    ]})"),
                                        ctx);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.failedStep, 2);
    EXPECT_EQ(r.cmd, QStringLiteral("doc.bogus"));
    EXPECT_EQ(r.name, QStringLiteral("the broken one"));
    EXPECT_TRUE(f.calls.isEmpty()) << "nothing runs when a step is invalid";
    EXPECT_EQ(r.describe(), QStringLiteral("step 2 doc.bogus \"the broken one\": unknown command"));
}

TEST(script_runner, arguments_are_checked_by_schema) {
    ot::test::qtApp();
    Fixture f;
    auto failOf = [&](const char* step) {
        ScriptContext ctx;
        const QByteArray text = QByteArray("[") + step + "]";
        const ScriptResult r = f.runner.run(QJsonDocument::fromJson(text), ctx);
        EXPECT_FALSE(r.ok) << step;
        EXPECT_EQ(r.failedStep, 1) << step;
        return r.message;
    };
    EXPECT_EQ(failOf(R"({"cmd": "doc.open"})"), QStringLiteral("args.path: required"));
    EXPECT_EQ(failOf(R"({"cmd": "doc.open", "args": {"path": 5}})"),
              QStringLiteral("args.path: expected string, got integer"));
    EXPECT_EQ(failOf(R"({"cmd": "doc.open", "args": {"path": ""}})"),
              QStringLiteral("args.path: string is shorter than 1"));
    EXPECT_EQ(failOf(R"({"cmd": "doc.open", "args": {"path": "a", "page": 2}})"),
              QStringLiteral("args.page: unknown argument"));
    EXPECT_EQ(failOf(R"({"cmd": "view.zoom", "args": {"percent": 0}})"),
              QStringLiteral("args.percent: 0 is less than 1"));
    EXPECT_EQ(failOf(R"({"cmd": "view.layout", "args": {"mode": "spread"}})"),
              QStringLiteral("args.mode: \"spread\" is not one of \"single\"|\"continuous\""));
    EXPECT_EQ(failOf(R"({"cmd": "view.layout", "args": {"pages": [1, "2"]}})"),
              QStringLiteral("args.pages[1]: expected integer, got string"));
    EXPECT_EQ(failOf(R"({"cmd": "view.layout", "args": {"pages": []}})"),
              QStringLiteral("args.pages: fewer than 1 items"));
    EXPECT_EQ(failOf(R"({"cmd": "doc.close", "args": {"force": true}})"),
              QStringLiteral("args: doc.close takes no arguments"));
    EXPECT_EQ(failOf(R"({"cmd": "doc.close", "args": [1]})"),
              QStringLiteral("args: expected object, got array"));
    EXPECT_EQ(failOf(R"({"command": "doc.close"})"), QStringLiteral("unknown step key \"command\""));
    EXPECT_TRUE(f.calls.isEmpty());
}

TEST(script_runner, failing_action_stops_the_script) {
    ot::test::qtApp();
    Fixture f;
    ScriptContext ctx;
    const ScriptResult r =
        f.runner.run(json(R"([{"cmd": "doc.close"}, {"cmd": "fail"}, {"cmd": "doc.close"}])"), ctx);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.done, 1);
    EXPECT_EQ(r.failedStep, 2);
    EXPECT_EQ(f.calls, QStringList{QStringLiteral("close")});
    EXPECT_EQ(r.describe(), QStringLiteral("step 2 fail: nothing to undo"));
}

TEST(script_runner, file_variables_and_relative_paths) {
    ot::test::qtApp();
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    qputenv("OTTISK_SCRIPT_TEST_FILE", "nested/file.pdf");
    const QString file = QDir(dir.path()).filePath(QStringLiteral("open.json"));
    {
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly));
        out.write(R"([{"cmd": "doc.open", "args": {"path": "${OTTISK_SCRIPT_TEST_FILE}"}},
                      {"cmd": "doc.open", "args": {"path": "${SCRIPT_DIR}/../up.pdf"}}])");
    }
    Fixture f;
    ScriptContext ctx;
    const ScriptResult r = f.runner.runFile(file, ctx);
    ASSERT_TRUE(r.ok) << r.describe().toStdString();
    EXPECT_EQ(ctx.scriptDir, QDir(dir.path()).absolutePath());
    EXPECT_EQ(f.calls.value(0),
              QStringLiteral("open ") + QDir(dir.path()).filePath(QStringLiteral("nested/file.pdf")));
    EXPECT_EQ(f.calls.value(1), QStringLiteral("open ") +
                                    QDir::cleanPath(QDir(dir.path()).filePath(QStringLiteral("../up.pdf"))));
    // Неизвестная переменная — ошибка шага, испорченный JSON — ошибка файла (номер шага 0).
    EXPECT_THROW((void)ctx.expand(QStringLiteral("${OTTISK_NO_SUCH_VARIABLE_42}")), ScriptError);
    {
        QFile out(file);
        ASSERT_TRUE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
        out.write("[{\"cmd\": ");
    }
    ScriptContext ctx2;
    const ScriptResult broken = f.runner.runFile(file, ctx2);
    EXPECT_FALSE(broken.ok);
    EXPECT_EQ(broken.failedStep, 0);
    EXPECT_TRUE(broken.describe().startsWith(QStringLiteral("script: "))) << broken.describe().toStdString();
    const ScriptResult missing =
        f.runner.runFile(QDir(dir.path()).filePath(QStringLiteral("none.json")), ctx2);
    EXPECT_FALSE(missing.ok);
    EXPECT_TRUE(missing.message.contains(QStringLiteral("cannot read")));
}
