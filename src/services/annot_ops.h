// Аннотации через DocumentSession (планы 2.2, 5.1): задачи потока документа над core/annot/annots.h — для ui,
// у которого нет TaskContext и MuPDF. Правки — DocumentSession::edit (шаг истории с английским кодом —
// подпись «Отменить: …» выбирает ui), страница правки сбрасывает display list и приходит pageChanged.
#pragma once

#include <QFuture>
#include <QString>

#include <optional>
#include <vector>

#include "core/annot/annots.h"
#include "core/util/geometry.h"
#include "services/document_session.h"

namespace ot::annot {

// Коды шагов истории.
inline constexpr const char* kSetRectStep = "Set annotation rectangle";
inline constexpr const char* kDeleteStep = "Delete annotation";
inline constexpr const char* kUpdateStep = "Set annotation properties";
inline constexpr const char* kFlagsStep = "Set annotation flags";
inline constexpr const char* kMoveStep = "Move annotation";
inline constexpr const char* kEraseStep = "Erase ink";
inline constexpr const char* kPasteStep = "Paste annotations";
inline constexpr const char* kReplaceTextStep = "Replace text";
inline constexpr const char* kEditNoteStep = "Edit note text";

QFuture<std::vector<AnnotInfo>> list(DocumentSession& session, int page,
                                     TaskPriority priority = TaskPriority::Normal);
QFuture<std::optional<AnnotInfo>> at(DocumentSession& session, int page, PointF point, float tolerance = 0,
                                     TaskPriority priority = TaskPriority::High);
QFuture<std::optional<AnnotProps>> props(DocumentSession& session, AnnotRef ref,
                                         TaskPriority priority = TaskPriority::High);
QFuture<void> setRect(DocumentSession& session, AnnotRef ref, RectF rect);
// Несколько рамок одним шагом истории (выбранные аннотации тянут вместе — 2.2b).
struct RectEdit {
    AnnotRef ref;
    RectF rect;
};
QFuture<void> setRects(DocumentSession& session, std::vector<RectEdit> edits);
QFuture<void> remove(DocumentSession& session, std::vector<AnnotRef> refs);

// 5.1: создание (шаг — createStepName типа или step), группа «Заменить текст», правка свойств (шаг step —
// одинаковые шаги подряд за 1 с сливаются: набор текста записки), флаги, сдвиг, ластик, буфер.
QFuture<AnnotRef> create(DocumentSession& session, int page, AnnotProps props, QString step = {});
QFuture<std::vector<AnnotRef>> createGroup(DocumentSession& session, int page, std::vector<AnnotProps> items,
                                           QString step);
QFuture<void> update(DocumentSession& session, AnnotRef ref, AnnotPatch patch,
                     QString step = QString::fromLatin1(kUpdateStep));
QFuture<void> setFlags(DocumentSession& session, std::vector<AnnotRef> refs, int set, int clear);
QFuture<void> translate(DocumentSession& session, std::vector<AnnotRef> refs, float dx, float dy);
QFuture<bool> eraseInk(DocumentSession& session, AnnotRef ref, std::vector<PointF> path, float radius);
QFuture<AnnotClip> copy(DocumentSession& session, std::vector<AnnotRef> refs);
QFuture<std::vector<AnnotRef>> paste(DocumentSession& session, int page, AnnotClip clip, PointF offset);

}  // namespace ot::annot
