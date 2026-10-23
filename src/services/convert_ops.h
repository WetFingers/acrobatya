// Конвертация через DocumentSession (план 2.3): задачи потока документа над core/create — для ui, у которого
// нет TaskContext и MuPDF. «Сохранить как PDF» документа «только просмотр» (не-PDF, view-01) — новый файл,
// открытый документ не меняется; cancelAll() прерывает между страницами.
#pragma once

#include <QFuture>
#include <QString>

#include "services/document_session.h"

namespace ot::convert {

QFuture<void> saveAsPdf(DocumentSession& session, const QString& path);

}  // namespace ot::convert
