// Подписи через DocumentSession для интерфейса без MuPDF (план 4.6; эталон 3.8 «Инструмент», «Диалог
// подписания», «Панель «Подписи»»): задачи потока документа над core/sign — поля подписи (щелчок по пустому
// полю, панель), «Проверить документ» перед подписью (sign-17), можно ли сертифицировать (sign-16) и права
// DocMDP документа (инструменты правки серые у сертифицированного); панель «Подписи» — «Очистить подпись»
// (sign-22) и «Просмотреть подписанную версию» (sign-28). Подпись, метка времени и сведения LTV —
// DocumentSession::sign/timestampDocument/addValidationInfo, проверка — services/crypto_worker.h.
#pragma once

#include <QFuture>

#include <vector>

#include "core/sign/docmdp.h"
#include "core/sign/sig_fields.h"
#include "core/sign/warnings_scan.h"
#include "services/document_session.h"

namespace ot::signing {

QFuture<std::vector<sign::SignatureField>> fields(DocumentSession& session,
                                                  TaskPriority priority = TaskPriority::High);
QFuture<sign::WarningScan> warnings(DocumentSession& session);
QFuture<sign::CertifyBlock> certifyBlock(DocumentSession& session);
QFuture<sign::DocMdp> docMdp(DocumentSession& session);

// «Очистить подпись»: можно ли и сама правка (шаг «Clear signature»; записать — DocumentSession::save,
// инкремент). Нельзя — std::runtime_error в QFuture.
QFuture<sign::ClearBlock> clearBlock(DocumentSession& session, QString field);
QFuture<void> clearSignature(DocumentSession& session, QString field);
// «Просмотреть подписанную версию»: байты [0, конец /ByteRange) подписи field — во временный файл
// %TEMP%\AcroBatya\<имя>.signed-<N>.pdf (удаляет вызывающий); нет такой подписи — пусто.
QFuture<QString> signedVersion(DocumentSession& session, QString field);

}  // namespace ot::signing
