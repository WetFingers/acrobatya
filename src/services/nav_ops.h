// Панели навигации через DocumentSession (план 2.4): задачи потока документа над core/links — закладки
// уровнями, назначения, вложения (список, запись в файл), слои (список, переключение, сброс). ui держит
// только значения; смена слоёв — setLayersState (display lists сброшены, сигнал layersChanged, новый
// layersHash в ключе тайлов).
#pragma once

#include <QFuture>
#include <QString>

#include <vector>

#include "core/links/attachments.h"
#include "core/links/destinations.h"
#include "core/links/layers.h"
#include "core/links/outlines.h"
#include "services/document_session.h"

namespace ot::nav {

// Дочерние закладки узла path (пусто — верхний уровень).
QFuture<std::vector<outline::Item>> outline(DocumentSession& session, std::vector<int> path = {},
                                            TaskPriority priority = TaskPriority::High);

QFuture<std::vector<dests::Destination>> destinations(DocumentSession& session);

QFuture<std::vector<attachments::Attachment>> attachments(DocumentSession& session);
// Записать содержимое вложения в файл path (целиком; прежний перезаписывается). Ошибка — исключение в
// QFuture.
QFuture<void> saveAttachment(DocumentSession& session, attachments::Attachment attachment, QString path);

QFuture<std::vector<layers::LayerUi>> layers(DocumentSession& session);
// Переключить строку (заблокированная и подпись группы не меняются) / сбросить к /D — ответ: строки после.
QFuture<std::vector<layers::LayerUi>> toggleLayer(DocumentSession& session, int index);
QFuture<std::vector<layers::LayerUi>> resetLayers(DocumentSession& session);

}  // namespace ot::nav
