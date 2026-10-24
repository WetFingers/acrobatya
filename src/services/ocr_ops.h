// OCR через DocumentSession (план 3.4, п. 5 — признак скана; дальше — распознавание 8.1): задачи потока
// документа над core/ocr для ui без MuPDF.
#pragma once

#include <QFuture>

#include "core/ocr/is_scan.h"
#include "services/document_session.h"

namespace ot::ocr {

// «Похоже на скан» (эталон 3.15): страница — одна картинка не меньше 90 % площади и нет текста.
QFuture<ScanCheck> scanCheck(DocumentSession& session, int page, TaskPriority priority = TaskPriority::Low);

}  // namespace ot::ocr
