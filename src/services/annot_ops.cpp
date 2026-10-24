#include "services/annot_ops.h"

#include <set>

#include "services/task_context.h"

namespace ot::annot {

namespace {

void touchPages(TaskContext& tc, const std::vector<AnnotRef>& refs) {
    std::set<int> pages;
    for (const AnnotRef& ref : refs) {
        if (pages.insert(ref.page).second) {
            tc.touchPage(ref.page);
        }
    }
}

}  // namespace

QFuture<std::vector<AnnotInfo>> list(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return listAnnots(tc.document(), page); }, priority,
                        QStringLiteral("List annotations"));
}

QFuture<std::optional<AnnotInfo>> at(DocumentSession& session, int page, PointF point, float tolerance,
                                     TaskPriority priority) {
    return session.post(
        [page, point, tolerance](TaskContext& tc) { return annotAt(tc.document(), page, point, tolerance); },
        priority, QStringLiteral("Annotation at point"));
}

QFuture<std::optional<AnnotProps>> props(DocumentSession& session, AnnotRef ref, TaskPriority priority) {
    return session.post([ref](TaskContext& tc) { return readProps(tc.document(), ref); }, priority,
                        QStringLiteral("Annotation properties"));
}

QFuture<void> setRect(DocumentSession& session, AnnotRef ref, RectF rect) {
    return session.edit(QString::fromLatin1(kSetRectStep), [ref, rect](TaskContext& tc) {
        ot::annot::setRect(tc.document(), ref, rect);
        tc.touchPage(ref.page);
    });
}

QFuture<void> setRects(DocumentSession& session, std::vector<RectEdit> edits) {
    return session.edit(QString::fromLatin1(kSetRectStep), [edits = std::move(edits)](TaskContext& tc) {
        std::set<int> pages;
        for (const RectEdit& e : edits) {
            ot::annot::setRect(tc.document(), e.ref, e.rect);
            if (pages.insert(e.ref.page).second) {
                tc.touchPage(e.ref.page);
            }
        }
    });
}

QFuture<void> remove(DocumentSession& session, std::vector<AnnotRef> refs) {
    return session.edit(QString::fromLatin1(kDeleteStep), [refs = std::move(refs)](TaskContext& tc) {
        ot::annot::remove(tc.document(), refs);
        touchPages(tc, refs);
    });
}

QFuture<AnnotRef> create(DocumentSession& session, int page, AnnotProps props, QString step) {
    const QString name = step.isEmpty() ? QString::fromStdString(createStepName(props.type)) : step;
    return session.edit(name, [page, props = std::move(props), name](TaskContext& tc) {
        AnnotRef ref = ot::annot::create(tc.document(), page, props, name.toStdString());
        tc.touchPage(page);
        return ref;
    });
}

QFuture<std::vector<AnnotRef>> createGroup(DocumentSession& session, int page, std::vector<AnnotProps> items,
                                           QString step) {
    return session.edit(step, [page, items = std::move(items), step](TaskContext& tc) {
        std::vector<AnnotRef> refs = ot::annot::createGroup(tc.document(), page, items, step.toStdString());
        tc.touchPage(page);
        return refs;
    });
}

QFuture<void> update(DocumentSession& session, AnnotRef ref, AnnotPatch patch, QString step) {
    return session.edit(step, [ref, patch = std::move(patch), step](TaskContext& tc) {
        ot::annot::update(tc.document(), ref, patch, step.toStdString());
        tc.touchPage(ref.page);
    });
}

QFuture<void> setFlags(DocumentSession& session, std::vector<AnnotRef> refs, int set, int clear) {
    return session.edit(QString::fromLatin1(kFlagsStep),
                        [refs = std::move(refs), set, clear](TaskContext& tc) {
                            ot::annot::setFlags(tc.document(), refs, set, clear);
                            touchPages(tc, refs);
                        });
}

QFuture<void> translate(DocumentSession& session, std::vector<AnnotRef> refs, float dx, float dy) {
    return session.edit(QString::fromLatin1(kMoveStep), [refs = std::move(refs), dx, dy](TaskContext& tc) {
        ot::annot::translate(tc.document(), refs, dx, dy);
        touchPages(tc, refs);
    });
}

QFuture<bool> eraseInk(DocumentSession& session, AnnotRef ref, std::vector<PointF> path, float radius) {
    return session.edit(QString::fromLatin1(kEraseStep),
                        [ref, path = std::move(path), radius](TaskContext& tc) {
                            const bool erased = ot::annot::eraseInk(tc.document(), ref, path, radius);
                            if (erased) {
                                tc.touchPage(ref.page);
                            }
                            return erased;
                        });
}

QFuture<AnnotClip> copy(DocumentSession& session, std::vector<AnnotRef> refs) {
    return session.post(
        [refs = std::move(refs)](TaskContext& tc) { return ot::annot::copy(tc.document(), refs); },
        TaskPriority::High, QStringLiteral("Copy annotations"));
}

QFuture<std::vector<AnnotRef>> paste(DocumentSession& session, int page, AnnotClip clip, PointF offset) {
    return session.edit(QString::fromLatin1(kPasteStep),
                        [page, clip = std::move(clip), offset](TaskContext& tc) {
                            std::vector<AnnotRef> refs = ot::annot::paste(tc.document(), page, clip, offset);
                            tc.touchPage(page);
                            return refs;
                        });
}

QFuture<AnnotRef> createStamp(DocumentSession& session, int page, StampSource source, StampValues values,
                              AnnotProps props) {
    return session.edit(QString::fromStdString(createStepName(AnnotType::Stamp)),
                        [page, source = std::move(source), values = std::move(values),
                         props = std::move(props)](TaskContext& tc) {
                            AnnotRef ref = ot::annot::createStamp(tc.document(), page, source, values, props);
                            tc.touchPage(page);
                            return ref;
                        });
}

QFuture<AnnotRef> createImageStamp(DocumentSession& session, int page, std::string image, AnnotProps props,
                                   std::string name) {
    return session.edit(QString::fromStdString(createStepName(AnnotType::Stamp)),
                        [page, image = std::move(image), props = std::move(props),
                         name = std::move(name)](TaskContext& tc) {
                            AnnotRef ref = ot::annot::createImageStamp(tc.document(), page, image, props, name);
                            tc.touchPage(page);
                            return ref;
                        });
}

QFuture<void> setStampRotation(DocumentSession& session, AnnotRef ref, float degrees) {
    return session.edit(QString::fromLatin1(kRotateStampStep), [ref, degrees](TaskContext& tc) {
        ot::annot::setStampRotation(tc.document(), ref, degrees);
        tc.touchPage(ref.page);
    });
}

QFuture<float> stampRotation(DocumentSession& session, AnnotRef ref) {
    return session.post([ref](TaskContext& tc) { return ot::annot::stampRotation(tc.document(), ref); },
                        TaskPriority::High, QStringLiteral("Stamp rotation"));
}

}  // namespace ot::annot
