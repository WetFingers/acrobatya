// ui.script_fillsign — сценарий 3 эталона через --script (план 5.3, п. 4; рецепт «Новое действие --script»):
// tests/scripts/fillsign.json — плоская анкета: текст на линиях, гребёнки, дата, галочка во флажке и
// нарисованная подпись (сохранена в <данные>/signatures) → инкрементальная копия (исходная анкета — её начало
// байт в байт; FreeText и /Stamp /FillSignSignature с PNG-альфой; qpdf --check = 0) и плоская копия без
// аннотаций (qpdf --check = 0); неверные аргументы (нет картинки подписи, страница вне документа, знак вне
// списка) — ошибка шага с его именем.
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>

#include "core/annot/annots.h"
#include "core/annot/stamps.h"
#include "core/doc/document.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "services/saved_signatures.h"
#include "services/script_runner.h"
#include "services/signature_image.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/main_window.h"

using ot::ScriptContext;
using ot::ScriptResult;
using ot::ScriptRunner;
using ot::ui::MainWindow;

namespace {

QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-script_fillsign"); }

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

class ScriptFillSignTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void scenarioThree() {
        OT_FEATURE("forms-07");
        OT_FEATURE("sign-01");
        OT_FEATURE("sign-02");
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        for (const char* id :
             {"fillsign.placeText", "fillsign.placeMark", "fillsign.placeSignature", "fillsign.flatCopy"}) {
            QVERIFY2(runner.hasAction(QLatin1String(id)), id);
        }
        QTemporaryDir out;
        QVERIFY(out.isValid());
        ScriptContext ctx;
        ctx.outDir = out.path();
        const ScriptResult r =
            runner.runFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts/fillsign.json"), ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));

        const QString filled = QDir(out.path()).filePath(QStringLiteral("fillsign.pdf"));
        const QString flat = QDir(out.path()).filePath(QStringLiteral("fillsign-flat.pdf"));
        const QByteArray original =
            bytes(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/forms/flat/anketa.pdf"));
        const QByteArray written = bytes(filled);
        QVERIFY(written.size() > original.size());
        QVERIFY(written.startsWith(original));  // инкремент: анкета байт в байт
        QVERIFY(ot::fillsign::SavedSignatures().has(ot::fillsign::SignatureKind::Signature));  // «сохранена»

        auto doc = ot::Document::openOrThrow(fsPath(filled));
        int texts = 0;
        int stamps = 0;
        for (const ot::annot::AnnotInfo& a : ot::annot::listAnnots(*doc, 0)) {
            QVERIFY2(a.hasAppearance, a.subtype.c_str());
            if (a.type == ot::annot::AnnotType::FreeText) {
                ++texts;
            } else if (a.type == ot::annot::AnnotType::Stamp) {
                ++stamps;
                const auto props = ot::annot::readProps(*doc, a.ref);
                QCOMPARE(QString::fromStdString(props->icon), QStringLiteral("FillSignSignature"));
                const std::string png = ot::annot::stampImage(*doc, a.ref);
                QImage image;
                QVERIFY(image.loadFromData(reinterpret_cast<const uchar*>(png.data()),
                                           static_cast<int>(png.size())));
                QVERIFY(image.hasAlphaChannel());
                QVERIFY(ot::fillsign::inkComponents(image) >= 3);
            }
        }
        // Фамилия, имя, 8 + 12 клеток, дата, галочка.
        QCOMPARE(texts, 2 + 8 + 12 + 1 + 1);
        QCOMPARE(stamps, 1);
        auto flatDoc = ot::Document::openOrThrow(fsPath(flat));
        QVERIFY(ot::annot::listAnnots(*flatDoc, 0).empty());
        const ot::QpdfTool qpdf;
        if (qpdf.available()) {
            for (const QString& file : {filled, flat}) {
                const ot::ToolResult check = qpdf.check(fsPath(file));
                QVERIFY2(check.ok(), check.output.c_str());
            }
        }
    }

    void badArgumentsFailWithName() {
        // Сохранённой подписи нет — нужна картинка или штрихи.
        ot::fillsign::SavedSignatures().remove(ot::fillsign::SignatureKind::Initials);
        MainWindow w;
        showWindow(w);
        ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        ScriptContext ctx;
        ctx.scriptDir = QStringLiteral(OTTISK_SOURCE_DIR "/tests/scripts");
        const char* open = R"({"cmd": "doc.open", "args": {"path": "../fixtures/forms/flat/anketa.pdf"}})";
        const struct {
            const char* step;
            const char* expect;
        } cases[] = {
            {R"({"cmd": "fillsign.placeSignature", "args": {"page": 1, "rect": [100, 100, 200, 140], "kind": "initials"},
                "name": "без картинки"})",
             "без картинки"},
            {R"({"cmd": "fillsign.placeText", "args": {"page": 5, "point": [10, 10], "text": "x"}, "name": "страница"})",
             "страница"},
            {R"({"cmd": "fillsign.placeMark", "args": {"page": 1, "point": [10, 10], "mark": "star"}, "name": "знак"})",
             "знак"},
            {R"({"cmd": "fillsign.placeSignature", "args": {"page": 1, "rect": [100, 100, 100, 100],
                "strokes": [[[1, 1], [20, 20]]]}, "name": "рамка"})",
             "рамка"},
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

OT_UI_TEST(ScriptFillSignTest, "script_fillsign");
#include "script_fillsign_test.moc"
