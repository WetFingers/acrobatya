// Сводка комментариев (план 5.4; эталон 3.4 «Сводка комментариев»; comment-25): новый PDF через
// fz_new_document_writer_with_buffer «pdf» и устройство страницы — макеты Acrobat:
//   * DocumentAndComments — «документ и комментарии на одной странице с выносками»: страница документа (с
//     пометками, векторно) — слева, 60 % ширины листа; справа колонка карточек (номер, тип, автор, дата,
//     статус, текст, ответы); линии-связки от пометки к карточке; не уместилось — продолжение на следующем
//     листе с той же страницей;
//   * CommentsOnly — «только комментарии»: A4, по страницам заголовок и карточки;
//   * SeparatePages — «документ и комментарии на отдельных страницах»: страница документа, за ней — лист(ы)
//     её комментариев.
// Текст — fz_new_text шрифтом Segoe UI из системных (кириллица; нет — Helvetica MuPDF), перенос по словам по
// ширине глифов. Порядок и состав — как в панели: arrange(tree, sort, filter) (core/annot/comments.h),
// диапазон страниц, кегль. Подписи (тип, статус, «Страница», «ответ») даёт ui — ядро без строк интерфейса.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/annot/comments.h"

namespace ot {

class Document;

namespace annot {

enum class SummaryLayout { DocumentAndComments, CommentsOnly, SeparatePages };

// Подписи сводки (язык интерфейса).
struct SummaryTexts {
    std::string title;                                    // заголовок листа «только комментарии»
    std::function<std::string(int page)> pageTitle;       // «Страница 3» (номер — с 1 или метка)
    std::function<std::string(const Comment&)> typeName;  // «Записка»…
    std::function<std::string(ReviewState)> stateName;    // «Принято»…
    std::string noComments;                               // «Комментариев нет»
};

struct SummaryOptions {
    SummaryLayout layout = SummaryLayout::DocumentAndComments;
    CommentSort sort = CommentSort::Page;
    bool descending = false;
    CommentFilter filter;
    std::function<bool(const Comment&)> isUnread;
    int firstPage = 0;  // с 0
    int lastPage = -1;  // −1 — до конца
    float fontSize = 9.0f;
    bool includeEmptyPages = false;  // страницы без комментариев (макеты с документом)
    SummaryTexts texts;
};

struct SummaryResult {
    std::string pdf;
    int pages = 0;
    int comments = 0;  // корней в сводке
};

// Сводка документа doc по дереву tree. Не PDF — std::logic_error.
SummaryResult buildSummary(Document& doc, const CommentTree& tree, const SummaryOptions& options);

}  // namespace annot
}  // namespace ot
