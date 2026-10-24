// ui.script_annots — комментарии через --script (план 5.1, пп. 1 и 5; рецепт «Новое действие --script»):
// tests/scripts/annots_forms.json создаёт annot.create 15 типов на пустой странице, пишет инкрементальную
// копию (все типы на месте, у каждого есть /AP, автор и /NM; qpdf --check = 0), отмена всех шагов даёт копию
// без комментариев; неверные аргументы (тип вне списка, нет рамки и точек, плохой цвет, страница вне
// документа) — ошибка шага с его именем.
#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <set>

#include "core/annot/annots.h"
#include "core/doc/document.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "services/script_runner.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/main_window.h"

using ot::ScriptContext;
using ot::ScriptResult;
using ot::ScriptRunner;
using ot::ui::MainWindow;

namespace {

void showWindow(MainWindow& w) {
    w.setCloseConfirm([](ot::ui::DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    w.resize(1440, 900);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

std::vector<ot::annot::AnnotInfo> fileComments(const QString& path) {
    auto doc = ot::Document::openOrThrow(std::filesystem::path(path.toStdWString()));
    std::vector<ot::annot::AnnotInfo> out;
    for (ot::annot::AnnotInfo& a : ot::annot::listAnnots(*doc, 0)) {
        if (ot::annot::isComment(a.type)) {
            out.push_back(std::move(a));
        }
    }
    return out;
}

}  // namespace

class ScriptAnnotsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_annots"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void createsAllTypesAndUndoes() {
        OT_FEATURE("comment-08");
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        QVERIFY(runner.hasAction(QStringLiteral("annot.create")));
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r =
            runner.runFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts/annots_forms.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        const QString saved = QDir(out.path()).filePath(QStringLiteral("annots.pdf"));
        const auto list = fileComments(saved);
        std::set<std::string> types;
        for (const ot::annot::AnnotInfo& a : list) {
            types.insert(a.subtype);
            QVERIFY2(a.hasAppearance, a.subtype.c_str());
            QVERIFY2(!a.author.empty(), a.subtype.c_str());
            QCOMPARE(static_cast<int>(a.ref.nm.size()), 36);
        }
        QCOMPARE(static_cast<int>(types.size()), 15);
        QCOMPARE(static_cast<int>(list.size()), 15);
        const ot::QpdfTool qpdf;
        if (qpdf.available()) {
            const ot::ToolResult check = qpdf.check(std::filesystem::path(saved.toStdWString()));
            QVERIFY2(check.exitCode == 0, check.output.c_str());
        }
        QVERIFY(fileComments(QDir(out.path()).filePath(QStringLiteral("undone.pdf"))).empty());
    }

    void badArgumentsFailWithName() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        ScriptContext ctx;
        ctx.scriptDir = QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts");
        const char* open = R"({"cmd": "doc.open", "args": {"path": "../data/mini/blank-a4.pdf"}})";
        const struct {
            const char* step;
            const char* expect;
        } cases[] = {
            {R"({"cmd": "annot.create", "args": {"type": "Widget", "page": 1, "rect": [1, 1, 9, 9]}, "name": "тип"})",
             "тип"},
            {R"({"cmd": "annot.create", "args": {"type": "Square", "page": 1}, "name": "нет рамки"})",
             "нет рамки"},
            {R"({"cmd": "annot.create", "args": {"type": "Square", "page": 1, "rect": [1, 1, 9, 9], "color": "nope"},
                "name": "цвет"})",
             "цвет"},
            {R"({"cmd": "annot.create", "args": {"type": "Text", "page": 7, "rect": [1, 1, 9, 9]}, "name": "страница"})",
             "страница"},
        };
        for (const auto& c : cases) {
            const QByteArray script = QByteArray("[") + open + "," + c.step + "]";
            const ScriptResult r = runner.run(QJsonDocument::fromJson(script), ctx);
            QVERIFY2(!r.ok, c.step);
            QVERIFY2(r.describe().contains(QString::fromUtf8(c.expect)), qPrintable(r.describe()));
            runner.run(QJsonDocument::fromJson(R"([{"cmd": "doc.close"}])"), ctx);
        }
    }
};

OT_UI_TEST(ScriptAnnotsTest, "script_annots");
#include "script_annots_test.moc"
