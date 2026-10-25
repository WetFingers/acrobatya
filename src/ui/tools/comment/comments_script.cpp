#include "ui/tools/comment/comments_script.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>

#include "core/annot/comments.h"
#include "core/annot/fdf.h"
#include "core/annot/summary.h"
#include "services/comment_settings.h"
#include "services/comments_ops.h"
#include "services/document_session.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "ui/app/script_actions.h"
#include "ui/panes/comment_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"

namespace ot::ui {

namespace {

// Запись дерева по /NM; нет — ошибка шага.
annot::AnnotRef byName(DocumentView& doc, const QString& nm, const char* what) {
    const comments::Snapshot snap =
        script::await(comments::snapshot(doc.session(), TaskPriority::High), what);
    const std::string name = nm.toStdString();
    annot::AnnotRef found;
    snap.tree.forEach([&](const annot::Comment& c) {
        if (c.ref.nm == name) {
            found = c.ref;
        }
    });
    if (!found.valid()) {
        throw ScriptError(QStringLiteral("no comment with name %1").arg(nm));
    }
    return found;
}

std::string authorOf(const QJsonObject& a) {
    return a.contains(QLatin1String("author")) ? a.value(QLatin1String("author")).toString().toStdString()
                                               : comments::author(Settings());
}

}  // namespace

void registerCommentsScriptActions(ScriptRunner& runner, MainWindow& w) {
    MainWindow* window = &w;
    runner.registerAction(QStringLiteral("comment.reply"),
                          R"({"type": "object", "properties": {
            "nm": {"type": "string", "minLength": 1}, "text": {"type": "string", "minLength": 1},
            "author": {"type": "string"}},
            "required": ["nm", "text"], "additionalProperties": false})",
                          [window](const QJsonObject& a, ScriptContext&) {
                              DocumentView& doc = script::document(*window);
                              const annot::AnnotRef parent =
                                  byName(doc, a.value(QLatin1String("nm")).toString(), "comment.reply");
                              annot::ReplyProps reply;
                              reply.author = authorOf(a);
                              reply.contents = a.value(QLatin1String("text")).toString().toStdString();
                              script::await(comments::reply(doc.session(), parent, std::move(reply)),
                                            "comment.reply");
                          });
    runner.registerAction(
        QStringLiteral("comment.setStatus"),
        R"({"type": "object", "properties": {
            "nm": {"type": "string", "minLength": 1},
            "state": {"type": "string", "enum": ["None", "Accepted", "Rejected", "Cancelled", "Completed",
                "Marked", "Unmarked"]},
            "author": {"type": "string"}},
            "required": ["nm", "state"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            const annot::AnnotRef target =
                byName(doc, a.value(QLatin1String("nm")).toString(), "comment.setStatus");
            const std::string state = a.value(QLatin1String("state")).toString().toStdString();
            const bool marked = state == "Marked" || state == "Unmarked";
            script::await(comments::setStatus(doc.session(), target,
                                              std::string(marked ? annot::kMarkedModel : annot::kReviewModel),
                                              state, authorOf(a)),
                          "comment.setStatus");
        });
    runner.registerAction(
        QStringLiteral("comments.export"),
        R"({"type": "object", "properties": {
            "path": {"type": "string", "minLength": 1}, "format": {"type": "string", "enum": ["fdf", "xfdf"]},
            "nm": {"type": "string", "minLength": 1}},
            "required": ["path", "format"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& ctx) {
            DocumentView& doc = script::document(*window);
            std::vector<annot::AnnotRef> refs;
            if (a.contains(QLatin1String("nm"))) {
                refs.push_back(byName(doc, a.value(QLatin1String("nm")).toString(), "comments.export"));
            } else {
                const comments::Snapshot snap =
                    script::await(comments::snapshot(doc.session(), TaskPriority::High), "comments.export");
                for (const annot::Comment& c : snap.tree.roots) {
                    refs.push_back(c.ref);
                }
            }
            const comments::ExchangeFormat format =
                a.value(QLatin1String("format")).toString() == QLatin1String("fdf")
                    ? comments::ExchangeFormat::Fdf
                    : comments::ExchangeFormat::Xfdf;
            script::await(comments::exportFile(doc.session(), std::move(refs), format,
                                               ctx.path(a.value(QLatin1String("path")).toString())),
                          "comments.export");
        });
    runner.registerAction(
        QStringLiteral("comments.import"),
        R"({"type": "object", "properties": {
            "path": {"type": "string", "minLength": 1},
            "expect": {"type": "object", "properties": {
                "created": {"type": "integer", "minimum": 0}, "updated": {"type": "integer", "minimum": 0},
                "skipped": {"type": "integer", "minimum": 0}}, "additionalProperties": false}},
            "required": ["path"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& ctx) {
            DocumentView& doc = script::document(*window);
            comments::ExchangeFile file;
            try {
                file = comments::readExchange(ctx.path(a.value(QLatin1String("path")).toString()));
            } catch (const std::exception& e) {
                throw ScriptError(QStringLiteral("comments.import: %1").arg(QString::fromUtf8(e.what())));
            }
            if (!annot::sameDocumentName(file.info.file, QFileInfo(doc.path()).fileName().toStdString())) {
                throw ScriptError(QStringLiteral("comments.import: file belongs to another document (%1)")
                                      .arg(QString::fromStdString(file.info.file)));
            }
            const annot::ImportReport report =
                script::await(comments::importFile(doc.session(), std::move(file)), "comments.import");
            const QJsonObject expect = a.value(QLatin1String("expect")).toObject();
            const std::pair<const char*, int> checks[] = {
                {"created", report.created}, {"updated", report.updated}, {"skipped", report.skipped}};
            for (const auto& [key, value] : checks) {
                if (expect.contains(QLatin1String(key)) &&
                    expect.value(QLatin1String(key)).toInt() != value) {
                    throw ScriptError(QStringLiteral("comments.import: %1 = %2, expected %3")
                                          .arg(QLatin1String(key))
                                          .arg(value)
                                          .arg(expect.value(QLatin1String(key)).toInt()));
                }
            }
        });
    runner.registerAction(
        QStringLiteral("comments.summary"),
        R"({"type": "object", "properties": {
            "path": {"type": "string", "minLength": 1},
            "layout": {"type": "string", "enum": ["callouts", "comments", "separate"]},
            "fontSize": {"type": "number", "minimum": 6, "maximum": 16}},
            "required": ["path"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& ctx) {
            DocumentView& doc = script::document(*window);
            annot::SummaryOptions o;
            const QString layout = a.value(QLatin1String("layout")).toString();
            o.layout = layout == QLatin1String("comments")   ? annot::SummaryLayout::CommentsOnly
                       : layout == QLatin1String("separate") ? annot::SummaryLayout::SeparatePages
                                                             : annot::SummaryLayout::DocumentAndComments;
            o.fontSize = static_cast<float>(a.value(QLatin1String("fontSize")).toDouble(9));
            o.texts.title = QStringLiteral("Comment summary: %1").arg(doc.title()).toStdString();
            o.texts.pageTitle = [](int page) { return "Page " + std::to_string(page); };
            o.texts.typeName = [](const annot::Comment& c) {
                return comment_actions::typeTitle(c).toStdString();
            };
            o.texts.stateName = [](annot::ReviewState s) {
                return comment_actions::stateTitle(s).toStdString();
            };
            o.texts.noComments = "No comments";
            const annot::SummaryResult r =
                script::await(comments::summary(doc.session(), o), "comments.summary");
            QSaveFile out(ctx.path(a.value(QLatin1String("path")).toString()));
            const QByteArray bytes = QByteArray::fromStdString(r.pdf);
            if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size() || !out.commit()) {
                throw ScriptError(QStringLiteral("comments.summary: cannot write %1").arg(out.fileName()));
            }
        });
}

}  // namespace ot::ui
