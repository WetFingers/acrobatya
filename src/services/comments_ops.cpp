#include "services/comments_ops.h"

#include <set>

#include "services/task_context.h"

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
                                   std::string state, std::string author) {
    const QString step = QString::fromLatin1(model == annot::kMarkedModel ? kCheckStep : kStatusStep);
    return session.edit(step, [target, model = std::move(model), state = std::move(state),
                               author = std::move(author)](TaskContext& tc) {
        annot::AnnotRef ref = annot::setStatus(tc.document(), target, model, state, author);
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

}  // namespace ot::comments
