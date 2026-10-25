// ui.script_comments — сценарий 2 эталона через --script (план 5.4, п. 4; рецепт «Новое действие --script»):
// tests/scripts/comments.json — выделение и записка, ответ, «Принято» и галочка → копия, XFDF (пространство
// имён Adobe, 5 записей, ответ с inreplyto), FDF («%FDF-1.2»), сводка «с выносками» (текст записки и ответа)
// → тот же документ без комментариев: импорт XFDF создаёт 5, повторный FDF — обновляет 5 → копия с деревом
// (записка, ответ, статус; qpdf --check = 0). Чужой файл комментариев и неизвестная запись — ошибка шага.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <algorithm>
#include <filesystem>

#include "core/annot/comments.h"
#include "core/doc/document.h"
#include "core/optimize/qpdf_tool.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "services/script_runner.h"
#include "services/xfdf_xml.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"

using ot::ScriptContext;
using ot::ScriptResult;
using ot::ScriptRunner;
using ot::ui::MainWindow;

namespace {

QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_comments"); }

void showWindow(MainWindow& w) {
    w.setCloseConfirm([](ot::ui::DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    w.resize(1440, 900);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
}

std::filesystem::path fsPath(const QString& path) { return {path.toStdWString()}; }

QByteArray bytes(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}  // namespace

class ScriptCommentsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void scenarioTwo() {
        OT_FEATURE("comment-20");
        OT_FEATURE("comment-21");
        OT_FEATURE("comment-25");
        OT_FEATURE("comment-27");
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        for (const char* id : {"comment.reply", "comment.setStatus", "comments.export", "comments.import",
                               "comments.summary"}) {
            QVERIFY2(runner.hasAction(QLatin1String(id)), id);
        }
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r =
            runner.runFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts/comments.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        const QDir dir(out.path());

        const QByteArray xml = bytes(dir.filePath(QStringLiteral("review.xfdf")));
        QVERIFY(xml.contains("xmlns=\"http://ns.adobe.com/xfdf/\""));
        QVERIFY(xml.contains("<f href=\"word-paragraphs.pdf\""));
        const ot::annot::XfdfDocument parsed = ot::xfdf::read(xml);
        QCOMPARE(static_cast<int>(parsed.annots.size()), 5);
        QVERIFY(xml.contains("inreplyto=\"scenario2-note\""));
        QVERIFY(bytes(dir.filePath(QStringLiteral("review.fdf"))).startsWith("%FDF-1.2"));

        auto summary = ot::Document::openOrThrow(fsPath(dir.filePath(QStringLiteral("summary.pdf"))));
        QVERIFY(summary->pageCount() >= 1);
        const std::string text = ot::TextPage::build(*summary, 0)->text();
        QVERIFY2(text.find("Нужна ссылка на приложение 2") != std::string::npos, text.c_str());
        QVERIFY(text.find("Добавлю в следующей версии") != std::string::npos);

        const QString imported = dir.filePath(QStringLiteral("imported.pdf"));
        auto doc = ot::Document::openOrThrow(fsPath(imported));
        const ot::annot::CommentTree tree = ot::annot::buildComments(*doc);
        QCOMPARE(static_cast<int>(tree.roots.size()), 2);
        const auto note = std::find_if(tree.roots.begin(), tree.roots.end(), [](const ot::annot::Comment& c) {
            return c.ref.nm == "scenario2-note";
        });
        QVERIFY(note != tree.roots.end());
        QCOMPARE(note->replyCount(), 1);
        QCOMPARE(note->review(), ot::annot::ReviewState::Accepted);
        const auto hl = std::find_if(tree.roots.begin(), tree.roots.end(), [](const ot::annot::Comment& c) {
            return c.ref.nm == "scenario2-highlight";
        });
        QVERIFY(hl != tree.roots.end());
        QVERIFY(hl->marked());
        const ot::QpdfTool qpdf;
        if (qpdf.available()) {
            for (const QString& file : {imported, dir.filePath(QStringLiteral("review.pdf")),
                                        dir.filePath(QStringLiteral("summary.pdf"))}) {
                const ot::ToolResult check = qpdf.check(fsPath(file));
                QVERIFY2(check.ok(), check.output.c_str());
            }
        }
    }

    void badStepsFail() {
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        QTemporaryDir out;
        QVERIFY(out.isValid());
        // Чужой файл комментариев: XFDF другого документа.
        QFile foreign(QDir(out.path()).filePath(QStringLiteral("foreign.xfdf")));
        QVERIFY(foreign.open(QIODevice::WriteOnly));
        foreign.write(
            "<?xml version=\"1.0\"?><xfdf xmlns=\"http://ns.adobe.com/xfdf/\"><annots>"
            "<square page=\"0\" rect=\"10,10,50,50\" name=\"x\"/></annots><f href=\"other.pdf\"/></xfdf>");
        foreign.close();
        const QByteArray script =
            QByteArrayLiteral("[{\"cmd\": \"doc.open\", \"args\": {\"path\": \"" OTTISK_SOURCE_DIR
                              "/tests/fixtures/edit/word-paragraphs.pdf\"}},"
                              " {\"cmd\": \"comments.import\", \"args\": {\"path\": "
                              "\"${OUT_DIR}/foreign.xfdf\"}, \"name\": \"чужой\"}]");
        ScriptContext ctx;
        ctx.outDir = out.path();
        ScriptResult r = runner.run(QJsonDocument::fromJson(script), ctx);
        QVERIFY(!r.ok);
        QVERIFY2(r.describe().contains(QStringLiteral("another document")), qPrintable(r.describe()));
        const QByteArray reply =
            QByteArrayLiteral("[{\"cmd\": \"doc.open\", \"args\": {\"path\": \"" OTTISK_SOURCE_DIR
                              "/tests/fixtures/edit/word-paragraphs.pdf\"}},"
                              " {\"cmd\": \"comment.reply\", \"args\": {\"nm\": \"nope\", \"text\": \"x\"}, "
                              "\"name\": \"нет записи\"}]");
        r = runner.run(QJsonDocument::fromJson(reply), ctx);
        QVERIFY(!r.ok);
        QVERIFY2(r.describe().contains(QStringLiteral("no comment")), qPrintable(r.describe()));
    }
};

OT_UI_TEST(ScriptCommentsTest, "script_comments");
#include "script_comments_test.moc"
