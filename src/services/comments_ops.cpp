#include "services/comments_ops.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <set>
#include <stdexcept>

#include "core/annot/xfdf.h"
#include "services/task_context.h"
#include "services/xfdf_xml.h"

namespace ot::comments {

QFuture<Snapshot> snapshot(DocumentSession& session, TaskPriority priority) {
    return session.post(
        [](TaskContext& tc) {
            Snapshot s;
            s.tree = annot::buildComments(tc.document());
            s.documentKey = annot::commentsDocumentKey(tc.document());
            return s;
        },
        priority, QStringLiteral("Comment tree"));
}

QFuture<annot::AnnotRef> reply(DocumentSession& session, annot::AnnotRef parent, annot::ReplyProps props) {
    return session.edit(QString::fromLatin1(kReplyStep), [parent, props = std::move(props)](TaskContext& tc) {
        annot::AnnotRef ref = annot::addReply(tc.document(), parent, props, kReplyStep);
        tc.touchPage(parent.page);
        return ref;
    });
}

QFuture<annot::AnnotRef> setStatus(DocumentSession& session, annot::AnnotRef target, std::string model,
                                   std::string state, std::string author, std::int64_t when) {
    const QString step = QString::fromLatin1(model == annot::kMarkedModel ? kCheckStep : kStatusStep);
    return session.edit(step, [target, model = std::move(model), state = std::move(state),
                               author = std::move(author), when](TaskContext& tc) {
        annot::AnnotRef ref = annot::setStatus(tc.document(), target, model, state, author, when);
        tc.touchPage(target.page);
        return ref;
    });
}

QFuture<void> removeBranch(DocumentSession& session, annot::AnnotRef ref) {
    return session.edit(QString::fromLatin1(kDeleteCommentStep), [ref](TaskContext& tc) {
        const annot::CommentTree tree = annot::buildComments(tc.document());
        std::vector<annot::AnnotRef> refs = tree.branch(ref);
        if (refs.empty()) {
            refs.push_back(ref);
        }
        annot::remove(tc.document(), refs);
        std::set<int> pages;
        for (const annot::AnnotRef& r : refs) {
            if (pages.insert(r.page).second) {
                tc.touchPage(r.page);
            }
        }
    });
}

QFuture<void> editText(DocumentSession& session, annot::AnnotRef ref, std::string contents,
                       std::string rich) {
    return session.edit(QString::fromLatin1(kEditCommentStep),
                        [ref, contents = std::move(contents), rich = std::move(rich)](TaskContext& tc) {
                            annot::AnnotPatch patch;
                            patch.contents = contents;
                            patch.richContents = rich;
                            annot::update(tc.document(), ref, patch, kEditCommentStep);
                            tc.touchPage(ref.page);
                        });
}

namespace {

std::string documentFileName(TaskContext& tc) {
    return QFileInfo(tc.session().path()).fileName().toStdString();
}

void touchRefs(TaskContext& tc, const std::vector<annot::AnnotRef>& refs) {
    std::set<int> pages;
    for (const annot::AnnotRef& r : refs) {
        if (pages.insert(r.page).second) {
            tc.touchPage(r.page);
        }
    }
}

}  // namespace

QFuture<void> exportFile(DocumentSession& session, std::vector<annot::AnnotRef> refs, ExchangeFormat format,
                         QString path) {
    return session.post(
        [refs = std::move(refs), format, path](TaskContext& tc) {
            const std::string name = documentFileName(tc);
            QByteArray bytes;
            if (format == ExchangeFormat::Fdf) {
                bytes = QByteArray::fromStdString(annot::exportFdf(tc.document(), refs, name));
            } else {
                bytes = xfdf::write(annot::exportXfdf(tc.document(), refs, name));
            }
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                throw std::runtime_error("comments export: cannot write " + path.toStdString());
            }
        },
        TaskPriority::High, QStringLiteral("Export comments"));
}

ExchangeFile readExchange(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("comments import: cannot read " + path.toStdString());
    }
    ExchangeFile out;
    out.bytes = f.readAll();
    if (out.bytes.startsWith("%FDF-")) {
        out.format = ExchangeFormat::Fdf;
        out.info = annot::probeFdf(
            std::string_view(out.bytes.constData(), static_cast<std::size_t>(out.bytes.size())));
    } else {
        out.format = ExchangeFormat::Xfdf;
        out.info = annot::probeXfdf(xfdf::read(out.bytes));
    }
    return out;
}

QFuture<annot::ImportReport> importFile(DocumentSession& session, ExchangeFile file) {
    return session.edit(QString::fromLatin1(kImportStep), [file = std::move(file)](TaskContext& tc) {
        const std::string name = documentFileName(tc);
        annot::ImportReport report;
        if (file.format == ExchangeFormat::Fdf) {
            report = annot::importFdf(
                tc.document(),
                std::string_view(file.bytes.constData(), static_cast<std::size_t>(file.bytes.size())), name);
        } else {
            report = annot::importXfdf(tc.document(), xfdf::read(file.bytes), name);
        }
        touchRefs(tc, report.refs);
        return report;
    });
}

QFuture<annot::SummaryResult> summary(DocumentSession& session, annot::SummaryOptions options) {
    return session.post(
        [options = std::move(options)](TaskContext& tc) {
            const annot::CommentTree tree = annot::buildComments(tc.document());
            return annot::buildSummary(tc.document(), tree, options);
        },
        TaskPriority::High, QStringLiteral("Comment summary"));
}

}  // namespace ot::comments
