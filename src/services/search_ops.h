// Поиск по открытому документу через DocumentSession (план 2.5; эталон 3.2 «Поиск»; view-18, view-19): панель
// «Найти» и «Расширенный поиск» (текущий документ, все открытые). Задача документа: SearchSession по слоям из
// кеша сессии (SearchSession::textOptions — без сегментации), прогресс — колбэком из потока документа (новые
// совпадения с прошлого вызова, не чаще раза в 100 мс и в конце; получатель в GUI-потоке переносит их сам —
// QMetaObject::invokeMethod), отмена — флаг Control (между страницами) или DocumentSession::cancelAll.
// Документ без текста на всех страницах (скан без OCR) — pagesWithText == 0: интерфейс предлагает «Распознать
// текст» (8.1). Закладки (заголовки дерева /Outlines) и PDF-вложения (/EmbeddedFiles и /FileAttachment,
// открываются из памяти) — флажки «Включить закладки и вложения» «Расширенного поиска». ui держит только
// значения.
#pragma once

#include <QFuture>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

#include "core/text/search.h"

namespace ot {

class DocumentSession;

namespace search {

// Отмена из любого потока: поиск останавливается после текущей страницы, найденное остаётся.
struct Control {
    std::atomic<bool> cancelled{false};
    void cancel() noexcept { cancelled = true; }
};

struct Request {
    QString needle;
    SearchOptions options;
    bool snippets = false;  // контекст каждого совпадения (searchSnippet) — для «Расширенного поиска»
    int snippetRadius = 40;
};

struct Result {
    std::vector<SearchHit> hits;
    QStringList snippets;  // по совпадению (если просили): совпадение между U+0002 и U+0003
    int pageCount = 0;
    int pagesDone = 0;
    int pagesWithText = 0;  // 0 при pageCount > 0 — текста нет (скан без OCR)
    bool aborted = false;

    bool noText() const noexcept { return pageCount > 0 && pagesDone == pageCount && pagesWithText == 0; }
};

// Колбэк прогресса — из потока документа: progress и совпадения, найденные с прошлого вызова.
using ProgressFn = std::function<void(const SearchProgress& progress, const std::vector<SearchHit>& fresh)>;

// Поиск по всему документу (неверное регулярное выражение — MuError в QFuture).
QFuture<Result> run(DocumentSession& session, const Request& request, std::shared_ptr<Control> control = {},
                    ProgressFn progress = {});

struct BookmarkHit {
    QString title;
    int page = -1;  // −1 — у закладки нет перехода на страницу
    QVector<int> path;
};

// Закладки, в заголовке которых есть образец (регистр, целое слово, регулярное выражение — как у поиска).
QFuture<QVector<BookmarkHit>> bookmarks(DocumentSession& session, const Request& request);

struct AttachmentHits {
    QString fileName;  // как в файле (недоверенное — только показ)
    std::vector<SearchHit> hits;
    QStringList snippets;
};

// PDF-вложения документа с совпадениями (вложения без совпадений не возвращаются; битые и зашифрованные
// пропускаются).
QFuture<QVector<AttachmentHits>> attachments(DocumentSession& session, const Request& request,
                                             std::shared_ptr<Control> control = {});

// «Расширенный поиск» по одному документу — одной задачей: текст страниц (с контекстом), закладки и
// PDF-вложения по флажкам.
struct DocumentResults {
    Result text;
    QVector<BookmarkHit> bookmarks;
    QVector<AttachmentHits> attachments;
};
QFuture<DocumentResults> runAll(DocumentSession& session, const Request& request, bool withBookmarks,
                                bool withAttachments, std::shared_ptr<Control> control = {});

// Совпадает ли текст с образцом по опциям поиска (закладки; ignoreDiacritics не учитывается).
bool matches(const QString& text, const Request& request);

}  // namespace search
}  // namespace ot
