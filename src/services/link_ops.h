// Ссылки через DocumentSession (план 2.2b): задача потока документа над core/links/links.h — для ui, у
// которого нет TaskContext и MuPDF. Вид держит LinkInfo-значения по страницам и сбрасывает их по pageChanged.
#pragma once

#include <QFuture>

#include <vector>

#include "core/links/links.h"
#include "services/document_session.h"

namespace ot::links {

QFuture<std::vector<LinkInfo>> list(DocumentSession& session, int page,
                                    TaskPriority priority = TaskPriority::Normal);

}  // namespace ot::links
