// Рамки областей страницы через DocumentSession (план 2.5b): задача потока документа над core/pages/boxes.h —
// для слоя рамок вида (ui/view/boxes_overlay.h; Установки → Просмотр страницы → «Показывать рамки обрезки,
// подрезки и выпуска»).
#pragma once

#include <QFuture>

#include "core/pages/boxes.h"
#include "services/document_session.h"

namespace ot::boxes {

QFuture<pages::PageBoxes> read(DocumentSession& session, int page, TaskPriority priority = TaskPriority::Low);

}  // namespace ot::boxes
