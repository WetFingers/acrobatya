// Комментарии через DocumentSession (план 5.4; core/annot/comments.h): дерево документа, ответы, статусы и
// удаление ветки — задачи потока документа для ui (панель «Комментарии», окно записки, --script). Правки —
// DocumentSession::edit со своим шагом истории (подпись «Отменить: ответ» выбирает ui).
#pragma once

#include <QByteArray>
#include <QFuture>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include "core/annot/comments.h"
#include "core/annot/fdf.h"
#include "core/annot/summary.h"
#include "services/document_session.h"

namespace ot::comments {

inline constexpr const char* kReplyStep = "Add reply";
inline constexpr const char* kStatusStep = "Set status";
inline constexpr const char* kCheckStep = "Set check mark";
inline constexpr const char* kDeleteCommentStep = "Delete comment";
inline constexpr const char* kEditCommentStep = "Edit comment text";

// Дерево всех страниц и ключ документа для «прочитанных» (commentsDocumentKey).
struct Snapshot {
    annot::CommentTree tree;
    std::string documentKey;
};
QFuture<Snapshot> snapshot(DocumentSession& session, TaskPriority priority = TaskPriority::Normal);

QFuture<annot::AnnotRef> reply(DocumentSession& session, annot::AnnotRef parent, annot::ReplyProps props);
// model — Review | Marked (annot::kReviewModel, kMarkedModel); when — секунды Unix (0 — сейчас).
QFuture<annot::AnnotRef> setStatus(DocumentSession& session, annot::AnnotRef target, std::string model,
                                   std::string state, std::string author, std::int64_t when = 0);
// Ветка записи (сама, ответы, статусы, части групп) — одним шагом «Delete comment».
QFuture<void> removeBranch(DocumentSession& session, annot::AnnotRef ref);
// Текст записи (корня или ответа): /Contents и /RC (пусто — снять) одним шагом «Edit comment text».
QFuture<void> editText(DocumentSession& session, annot::AnnotRef ref, std::string contents, std::string rich);

// ---------------------------------------------------------------- FDF/XFDF и сводка (comment-27, comment-25)

inline constexpr const char* kImportStep = "Import comments";

enum class ExchangeFormat { Fdf, Xfdf };

// Записать комментарии в файл (QSaveFile): refs пусто — все аннотации документа (без окон и полей), иначе —
// refs с ветками; /F (<f href>) — имя файла документа. Ошибка — исключение будущего (std::runtime_error).
QFuture<void> exportFile(DocumentSession& session, std::vector<annot::AnnotRef> refs, ExchangeFormat format,
                         QString path);

// Файл обмена, прочитанный для подтверждения (эталон 7.3): формат — по содержимому («%FDF-» — FDF, иначе
// XFDF), что внутри. Не читается / не FDF и не XFDF — annot::ExchangeError или std::runtime_error.
struct ExchangeFile {
    ExchangeFormat format = ExchangeFormat::Xfdf;
    QByteArray bytes;
    annot::ExchangeInfo info;
};
ExchangeFile readExchange(const QString& path);
// Импорт одним шагом «Import comments». /F на другой документ — annot::ExchangeError (ForeignDocument).
QFuture<annot::ImportReport> importFile(DocumentSession& session, ExchangeFile file);

// Сводка комментариев по свежему дереву документа (core/annot/summary.h) — PDF в памяти.
QFuture<annot::SummaryResult> summary(DocumentSession& session, annot::SummaryOptions options);

}  // namespace ot::comments
