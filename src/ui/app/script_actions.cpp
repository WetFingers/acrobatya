#include "ui/app/script_actions.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QJsonArray>
#include <QPixmap>
#include <QThread>

#include <cstdio>
#include <type_traits>

#include "core/doc/saver.h"
#include "services/commands.h"
#include "services/document_session.h"
#include "services/props_ops.h"
#include "services/script_runner.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/certificate/sign_script.h"
#include "ui/tools/comment/comment_script.h"
#include "ui/tools/edit/edit_script.h"
#include "ui/tools/fillsign/fillsign_script.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

namespace ot::ui {

namespace {

using script::await;
using script::settle;

DocumentView& current(MainWindow& w) { return script::document(w); }

SaveOptions saveOptions(const QJsonObject& args) {
    SaveOptions o;
    const QString mode = args.value(QLatin1String("mode")).toString(QStringLiteral("auto"));
    if (mode == QLatin1String("incremental")) {
        o.mode = SaveMode::Incremental;
    } else if (mode == QLatin1String("full")) {
        o.mode = SaveMode::Full;
    } else if (mode == QLatin1String("optimized")) {
        o.mode = SaveMode::FullOptimized;
    }
    return o;
}

void checkSaved(const SaveReport& r, const char* what) {
    if (!r.ok()) {
        throw ScriptError(QStringLiteral("%1 failed: %2 (%3)")
                              .arg(QLatin1String(what), QLatin1String(Saver::errorName(r.error)),
                                   QString::fromStdString(r.message)));
    }
}

void undoRedo(MainWindow& w, const QJsonObject& args, bool undo) {
    DocumentSession& s = current(w).session();
    const int n = args.value(QLatin1String("n")).toInt(1);
    for (int i = 0; i < n; ++i) {
        const bool ok = await(undo ? s.undo() : s.redo(), undo ? "doc.undo" : "doc.redo");
        if (!ok) {
            throw ScriptError(QStringLiteral("only %1 of %2 steps %3")
                                  .arg(i)
                                  .arg(n)
                                  .arg(undo ? QStringLiteral("undone") : QStringLiteral("redone")));
        }
    }
}

const char kNoArgs[] = R"({"type": "object", "additionalProperties": false})";
const char kSaveSchema[] = R"({"type": "object", "properties": {
    "path": {"type": "string", "minLength": 1},
    "mode": {"enum": ["auto", "incremental", "full", "optimized"]}}, "additionalProperties": false})";
const char kCountSchema[] = R"({"type": "object", "properties": {"n": {"type": "integer", "minimum": 1}},
    "additionalProperties": false})";

}  // namespace

namespace script {

void settle() {
    for (int i = 0; i < 3; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }
}

DocumentView& document(MainWindow& window) {
    DocumentView* d = window.currentDocument();
    if (d == nullptr) {
        throw ScriptError(QStringLiteral("no open document (doc.open first)"));
    }
    return *d;
}

bool tilesReady(MainWindow& window) {
    DocumentView* d = window.currentDocument();
    if (d == nullptr) {
        settle();
        return true;
    }
    QElapsedTimer t;
    t.start();
    while (true) {
        d->view().viewport()->repaint();
        if (d->view().tilePainter().visibleComplete()) {
            return true;
        }
        if (t.elapsed() > 30000) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(5);
    }
}

}  // namespace script

void registerBaseActions(ScriptRunner& runner, MainWindow& w) {
    MainWindow* window = &w;

    // ---- документ
    runner.registerAction(
        QStringLiteral("doc.open"),
        R"({"type": "object", "properties": {"path": {"type": "string", "minLength": 1},
            "password": {"type": "string"}, "page": {"type": "integer", "minimum": 1}},
            "required": ["path"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& c) {
            const QString path = c.path(a.value(QLatin1String("path")).toString());
            if (!QFileInfo(path).isFile()) {
                throw ScriptError(QStringLiteral("no such file: ") + QDir::toNativeSeparators(path));
            }
            const int page = a.value(QLatin1String("page")).toInt(0) - 1;
            if (!window->openFile(path, page, a.value(QLatin1String("password")).toString())) {
                throw ScriptError(QStringLiteral("cannot open %1: %2")
                                      .arg(QDir::toNativeSeparators(path), window->errorString()));
            }
            settle();
        });
    runner.registerAction(
        QStringLiteral("doc.save"), kSaveSchema, [window](const QJsonObject& a, ScriptContext& c) {
            DocumentSession& s = current(*window).session();
            const SaveOptions o = saveOptions(a);
            if (a.contains(QLatin1String("path"))) {
                checkSaved(await(s.saveAs(c.path(a.value(QLatin1String("path")).toString()), o), "doc.save"),
                           "doc.save");
            } else {
                checkSaved(await(s.save(o), "doc.save"), "doc.save");
            }
        });
    runner.registerAction(
        QStringLiteral("doc.saveCopy"),
        R"({"type": "object", "properties": {"path": {"type": "string", "minLength": 1},
            "mode": {"enum": ["auto", "incremental", "full", "optimized"]}},
            "required": ["path"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& c) {
            DocumentSession& s = current(*window).session();
            checkSaved(await(s.saveCopy(c.path(a.value(QLatin1String("path")).toString()), saveOptions(a)),
                             "doc.saveCopy"),
                       "doc.saveCopy");
        });
    runner.registerAction(QStringLiteral("doc.close"), kNoArgs, [window](const QJsonObject&, ScriptContext&) {
        current(*window);
        if (!window->closeDocument(window->currentIndex())) {
            throw ScriptError(QStringLiteral("document did not close"));
        }
        settle();
    });
    runner.registerAction(QStringLiteral("doc.undo"), kCountSchema,
                          [window](const QJsonObject& a, ScriptContext&) { undoRedo(*window, a, true); });
    runner.registerAction(QStringLiteral("doc.redo"), kCountSchema,
                          [window](const QJsonObject& a, ScriptContext&) { undoRedo(*window, a, false); });
    {
        QJsonArray keys;
        for (const QString& k : props::editableKeys()) {
            keys.append(k);
        }
        const QJsonObject schema{
            {QStringLiteral("type"), QStringLiteral("object")},
            {QStringLiteral("properties"),
             QJsonObject{
                 {QStringLiteral("key"), QJsonObject{{QStringLiteral("enum"), keys}}},
                 {QStringLiteral("value"), QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
            {QStringLiteral("required"), QJsonArray{QStringLiteral("key"), QStringLiteral("value")}},
            {QStringLiteral("additionalProperties"), false},
        };
        runner.registerAction(
            QStringLiteral("doc.setProperty"), schema, [window](const QJsonObject& a, ScriptContext&) {
                await(props::setProperty(current(*window).session(), a.value(QLatin1String("key")).toString(),
                                         a.value(QLatin1String("value")).toString()),
                      "doc.setProperty");
            });
    }

    // ---- вид
    runner.registerAction(QStringLiteral("view.zoom"),
                          R"({"type": "object", "properties": {"percent": {"type": "number", "minimum": 1,
                              "maximum": 6400}}, "required": ["percent"], "additionalProperties": false})",
                          [window](const QJsonObject& a, ScriptContext&) {
                              current(*window).view().setZoomPercent(
                                  static_cast<float>(a.value(QLatin1String("percent")).toDouble()));
                              settle();
                          });
    runner.registerAction(QStringLiteral("view.zoomFit"),
                          R"({"type": "object", "properties": {"mode": {"enum": ["page", "width", "height",
                              "visible", "actual"]}}, "additionalProperties": false})",
                          [window](const QJsonObject& a, ScriptContext&) {
                              const QString m =
                                  a.value(QLatin1String("mode")).toString(QStringLiteral("page"));
                              FitMode fit = FitMode::Page;
                              if (m == QLatin1String("width")) {
                                  fit = FitMode::Width;
                              } else if (m == QLatin1String("height")) {
                                  fit = FitMode::Height;
                              } else if (m == QLatin1String("visible")) {
                                  fit = FitMode::Visible;
                              } else if (m == QLatin1String("actual")) {
                                  fit = FitMode::ActualSize;
                              }
                              current(*window).view().fit(fit);
                              settle();
                          });
    runner.registerAction(QStringLiteral("view.zoomIn"), kNoArgs,
                          [window](const QJsonObject&, ScriptContext&) {
                              current(*window).view().zoomIn();
                              settle();
                          });
    runner.registerAction(QStringLiteral("view.zoomOut"), kNoArgs,
                          [window](const QJsonObject&, ScriptContext&) {
                              current(*window).view().zoomOut();
                              settle();
                          });
    runner.registerAction(
        QStringLiteral("view.page"),
        R"({"type": "object", "properties": {"page": {"type": "integer", "minimum": 1}},
                              "required": ["page"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            PageView& v = current(*window).view();
            const int page = a.value(QLatin1String("page")).toInt();
            if (page > v.pageCount()) {
                throw ScriptError(QStringLiteral("page %1 of %2").arg(page).arg(v.pageCount()));
            }
            v.goToPage(page - 1);
            settle();
        });
    runner.registerAction(
        QStringLiteral("view.layout"),
        R"({"type": "object", "properties": {"mode": {"enum": ["single", "continuous", "facing",
                              "facingContinuous"]}, "cover": {"type": "boolean"}}, "required": ["mode"],
                              "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            PageView& v = current(*window).view();
            const QString m = a.value(QLatin1String("mode")).toString();
            PageMode mode = PageMode::Continuous;
            if (m == QLatin1String("single")) {
                mode = PageMode::SinglePage;
            } else if (m == QLatin1String("facing")) {
                mode = PageMode::Facing;
            } else if (m == QLatin1String("facingContinuous")) {
                mode = PageMode::FacingContinuous;
            }
            v.setPageMode(mode);
            if (a.contains(QLatin1String("cover"))) {
                v.setCoverPage(a.value(QLatin1String("cover")).toBool());
            }
            settle();
        });
    runner.registerAction(QStringLiteral("view.rotate"),
                          R"({"type": "object", "properties": {"degrees": {"enum": [90, 180, 270, -90]}},
                              "required": ["degrees"], "additionalProperties": false})",
                          [window](const QJsonObject& a, ScriptContext&) {
                              PageView& v = current(*window).view();
                              const int degrees = a.value(QLatin1String("degrees")).toInt();
                              for (int i = 0; i < (degrees < 0 ? 1 : degrees / 90); ++i) {
                                  v.rotateView(degrees < 0 ? -90 : 90);
                              }
                              settle();
                          });

    // ---- окно
    runner.registerAction(
        QStringLiteral("command.run"),
        R"({"type": "object", "properties": {"id": {"type": "string", "minLength": 1}},
                              "required": ["id"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            const QString id = a.value(QLatin1String("id")).toString();
            const Command* c = window->commands().find(id);
            if (c == nullptr) {
                throw ScriptError(QStringLiteral("unknown window command ") + id);
            }
            if (c->stage > 0) {
                throw ScriptError(QStringLiteral("%1 arrives in version 0.%2").arg(id).arg(c->stage));
            }
            if (!window->commands().trigger(id)) {
                throw ScriptError(QStringLiteral("command %1 is unavailable now").arg(id));
            }
            settle();
        });
    runner.registerAction(
        QStringLiteral("shot"),
        R"({"type": "object", "properties": {"name": {"type": "string", "minLength": 1}},
                              "required": ["name"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& c) {
            QString name = c.expand(a.value(QLatin1String("name")).toString());
            if (name.contains(QLatin1Char('/')) || name.contains(QLatin1Char('\\')) ||
                name.contains(QLatin1Char(':'))) {
                throw ScriptError(QStringLiteral("shot name is a file name, not a path: ") + name);
            }
            if (!name.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)) {
                name += QStringLiteral(".png");
            }
            if (!script::tilesReady(*window)) {
                throw ScriptError(QStringLiteral("visible pages not rendered in 30 s"));
            }
            const QString file = QDir(c.shotsDir()).filePath(name);
            const QPixmap pm = window->grab();
            if (pm.isNull() || !pm.save(file, "PNG")) {
                throw ScriptError(QStringLiteral("cannot write ") + QDir::toNativeSeparators(file));
            }
        });
    runner.registerAction(QStringLiteral("wait"),
                          R"({"type": "object", "properties": {"ms": {"type": "integer", "minimum": 0,
                              "maximum": 600000}}, "required": ["ms"], "additionalProperties": false})",
                          [](const QJsonObject& a, ScriptContext&) {
                              QElapsedTimer t;
                              t.start();
                              const int ms = a.value(QLatin1String("ms")).toInt();
                              while (t.elapsed() < ms) {
                                  QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                                  QThread::msleep(1);
                              }
                          });

    // ---- действия инструментов (сессии-владельцы)
    registerEditScriptActions(runner, w);  // 3.4: edit.insertText, edit.replaceAll
    registerSignScriptActions(runner,
                              w);  // 4.6: sign.sign, sign.certify, sign.timestamp, sign.verifyAll, sign.trust
    registerCommentScriptActions(runner, w);   // 5.1: annot.create
    registerFillSignScriptActions(runner, w);  // 5.3: fillsign.placeText/placeMark/placeSignature/flatCopy
}

int runScript(const QString& file, const QString& outDir) {
    MainWindow window;
    window.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
    window.resize(1440, 900);
    window.show();
    settle();
    ScriptRunner runner;
    registerBaseActions(runner, window);
    ScriptContext context;
    context.outDir = outDir;
    const ScriptResult result = runner.runFile(QFileInfo(file).absoluteFilePath(), context);
    const QByteArray line = result.describe().toUtf8();
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), result.ok ? stdout : stderr);
    std::fputc('\n', result.ok ? stdout : stderr);
    std::fflush(stdout);
    std::fflush(stderr);
    window.closeAllDocuments();
    window.close();
    settle();
    return result.ok ? 0 : 1;
}

}  // namespace ot::ui
