// Аннотации через DocumentSession (план 2.2): задачи потока документа над core/annot/annots.h — для ui, у
// которого нет TaskContext и MuPDF. Правки — DocumentSession::edit (шаг истории с английским кодом — подпись
// «Отменить: …» выбирает ui), страница правки сбрасывает display list и приходит pageChanged.
#pragma once

#include <QFuture>

#include <optional>
#include <vector>

#include "core/annot/annots.h"
#include "core/util/geometry.h"
#include "services/document_session.h"

namespace ot::annot {

// Коды шагов истории.
inline constexpr const char* kSetRectStep = "Set annotation rectangle";
inline constexpr const char* kDeleteStep = "Delete annotation";

QFuture<std::vector<AnnotInfo>> list(DocumentSession& session, int page,
                                     TaskPriority priority = TaskPriority::Normal);
QFuture<std::optional<AnnotInfo>> at(DocumentSession& session, int page, PointF point, float tolerance = 0,
                                     TaskPriority priority = TaskPriority::High);
QFuture<void> setRect(DocumentSession& session, AnnotRef ref, RectF rect);
QFuture<void> remove(DocumentSession& session, std::vector<AnnotRef> refs);

}  // namespace ot::annot
