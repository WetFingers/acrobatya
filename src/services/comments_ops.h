// Комментарии через DocumentSession (план 5.4; core/annot/comments.h): дерево документа, ответы, статусы и
// удаление ветки — задачи потока документа для ui (панель «Комментарии», окно записки, --script). Правки —
// DocumentSession::edit со своим шагом истории (подпись «Отменить: ответ» выбирает ui).
#pragma once

#include <QFuture>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

#include "core/annot/comments.h"
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

}  // namespace ot::comments
