// Свойства документа через DocumentSession (план 2.4b — doc.setProperty; диалог «Свойства» — 2.5): задачи
// потока документа над Document::properties/setProperties (1.3: /Info и XMP вместе). Правка — шаг журнала
// «Set document property» (undo возвращает прежние /Info и XMP). ui держит только значения.
#pragma once

#include <QFuture>
#include <QString>
#include <QStringList>

#include "core/doc/document.h"
#include "services/document_session.h"

namespace ot::props {

// Ключи setProperty: title, author, subject, keywords, creator, producer.
QStringList editableKeys();

QFuture<DocProperties> read(DocumentSession& session);
// Записать одно поле /Info (и XMP); неизвестный ключ — исключение std::invalid_argument в QFuture.
QFuture<void> setProperty(DocumentSession& session, const QString& key, const QString& value);

}  // namespace ot::props
