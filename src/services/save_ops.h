// Сохранение для интерфейса (план 2.5; эталон 4.4; security-09): что сделает «Сохранить» (Saver::decide — без
// записи) задачей потока документа: окно спрашивает до записи — подписанный документ, который полная запись
// сломала бы (WouldBreakSignatures), новый без файла (NoPath), не PDF (NotPdf). Сама запись —
// DocumentSession::save/saveAs/saveCopy.
#pragma once

#include <QFuture>

#include "core/doc/saver.h"

namespace ot {

class DocumentSession;

namespace saving {

QFuture<SaveReport> decide(DocumentSession& session, const SaveOptions& options = {});

}  // namespace saving
}  // namespace ot
