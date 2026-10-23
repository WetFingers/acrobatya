// TaskContext — то, что видит задача DocumentSession в потоке документа (эталон 4.1): документ, его контекст
// MuPDF, fz_cookie задачи (отмена и прогресс) и страницы на время задачи (PageHandle). Заголовок для кода
// задач в services/core: включает MuPDF; ui работает с document_session.h, где MuPDF нет.
#pragma once

#include <thread>
#include <vector>

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class Document;
class DocumentSession;

// fz_cookie задачи: abort = 1 ставит DocumentSession::cancelAll() из другого потока.
struct TaskCancel {
    fz_cookie cookie{};
};

// fz_page на время задачи: держит одну ссылку и отдаёт её в деструкторе. Создаётся только
// TaskContext::page() и не выходит из задачи (UI хранит номер страницы, не указатель).
class PageHandle {
public:
    PageHandle(PageHandle&& other) noexcept;
    PageHandle& operator=(PageHandle&&) = delete;
    PageHandle(const PageHandle&) = delete;
    PageHandle& operator=(const PageHandle&) = delete;
    ~PageHandle();

    int index() const noexcept { return index_; }
    fz_page* get() const noexcept { return page_; }

private:
    friend class TaskContext;
    PageHandle(fz_context* ctx, fz_page* page, int index) noexcept;

    fz_context* ctx_;
    fz_page* page_;
    int index_;
    std::thread::id thread_;
};

class TaskContext {
public:
    TaskContext(const TaskContext&) = delete;
    TaskContext& operator=(const TaskContext&) = delete;

    // Открытый документ; нет документа — std::logic_error.
    Document& document() const;
    bool hasDocument() const noexcept;
    // Контекст MuPDF документа (клон global(); только в этом потоке).
    fz_context* context() const;
    // fz_cookie задачи: передавать в fz_run_* и Renderer::render; abort = 1 ставит cancelAll().
    fz_cookie* cookie() const noexcept { return cookie_; }
    bool aborted() const noexcept { return cookie_ != nullptr && cookie_->abort != 0; }
    // Загрузить страницу на время задачи; ошибка — MuError.
    PageHandle page(int index) const;
    // Правка затронула страницу: после задачи — сигнал pageChanged(index).
    void touchPage(int index);
    DocumentSession& session() const noexcept { return session_; }

private:
    friend class DocumentSession;
    TaskContext(DocumentSession& session, fz_cookie* cookie) noexcept : session_(session), cookie_(cookie) {}

    DocumentSession& session_;
    fz_cookie* cookie_;
    std::vector<int> touched_;
};

}  // namespace ot
