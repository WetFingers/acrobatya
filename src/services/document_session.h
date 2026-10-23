// DocumentSession — документ, открытый в программе, и его поток (эталон 4.1–4.2). Один QThread на документ
// с собственной очередью задач (std::deque + условная переменная, не QEventLoop в потоке — проще отмена и
// приоритеты). Всё, что трогает fz_document, fz_page и pdf_*, идёт задачей в этом потоке:
//   post(fn, priority) → QFuture<T> (через QPromise); fn(TaskContext&) выполняется в потоке документа;
//   edit(name, fn)      → правка: после неё revision++ и сигналы documentChanged, pageChanged;
//   postSync(fn)        → то же с ожиданием (тесты, простое окно этапа 0);
//   cancelAll()         → задачи в очереди завершаются MuError(FZ_ERROR_ABORT), у выполняемой — abort = 1
//                         в её fz_cookie (рендер прерывается между узлами display list).
// Ошибка задачи — исключение в QFuture (MuError как есть) и сигнал taskFailed(TaskFailed{code, message,
// hint}); отмена сигнала не даёт. Ни один указатель MuPDF не выходит из задачи: PageHandle живёт только в
// ней. Код задач включает services/task_context.h (TaskContext, PageHandle — с MuPDF); здесь MuPDF нет.
// Сигналы испускаются из потока документа — получателям в GUI-потоке они приходят QueuedConnection.
#pragma once

#include <QFuture>
#include <QImage>
#include <QMetaType>
#include <QObject>
#include <QPromise>
#include <QSizeF>
#include <QString>
#include <QVector>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#include "core/mu/error.h"

namespace ot {

class Document;
class TaskContext;
struct TaskCancel;  // fz_cookie задачи (task_context.h)

// Приоритет задачи в очереди документа: старший выполняется раньше, равные — по порядку постановки.
enum class TaskPriority : int { Background = 0, Low = 1, Normal = 2, High = 3, Urgent = 4 };

// Ошибка задачи для интерфейса (эталон 4.1): код MuPDF, английский текст для журнала и код подсказки
// (строку по-русски выбирает ui): "open-repaired" (FORMAT/SYNTAX — «Открыть восстановленным»),
// "unsupported", "limit", "close-document" (SYSTEM — нехватка памяти, ввод-вывод), "report" (прочее).
struct TaskFailed {
    QString task;  // имя задачи (edit) или пусто
    int code = 0;  // FZ_ERROR_*; 0 — ошибка не из MuPDF
    ErrorKind kind = ErrorKind::Other;
    QString message;
    QString hint;
};

// Итог открытия (копия флагов OpenResult и размеры страниц — раскладка знает только их).
struct OpenSummary {
    bool ok = false;
    bool needsPassword = false;
    bool repaired = false;
    bool pdfa = false;
    bool isSigned = false;
    bool hasForm = false;
    bool hasXfa = false;
    bool isPdf = false;
    int pageCount = 0;
    QVector<QSizeF> pageSizes;  // пункты, с учётом /CropBox и /Rotate
    TaskFailed error;
};

class DocumentSession : public QObject {
    Q_OBJECT
public:
    explicit DocumentSession(QObject* parent = nullptr);
    // Отменяет задачи (cancelAll) и останавливает поток; документ закрывается в своём потоке.
    ~DocumentSession() override;

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    // Уникален в процессе: id вкладки в DocumentRegistry и поле doc ключа тайлов.
    quint64 id() const noexcept { return id_; }
    // Поток документа (для проверок «задача идёт не в GUI-потоке»).
    std::thread::id threadId() const noexcept { return threadId_; }
    bool isSessionThread() const noexcept { return std::this_thread::get_id() == threadId_; }

    // Открыть файл в потоке документа (прежний закрывается). Ошибка — в OpenSummary::error, не исключение.
    QFuture<OpenSummary> openAsync(const QString& path, const QString& password = {});
    // То же с ожиданием (окно этапа 0, тесты); false — ошибка, текст — errorString(). Сигнал opened —
    // из вызывающего потока до возврата.
    bool open(const QString& path, const QString& password = {});
    // Закрыть документ после уже поставленных задач (приоритет Background) и дождаться; сигнал closed.
    void close();

    bool isOpen() const;
    QString path() const;
    QString errorString() const;
    OpenSummary summary() const;
    int pageCount() const;
    QSizeF pageSizePoints(int index) const;
    // Номер правки: растёт после каждой успешной edit() (поле revision ключа тайлов).
    quint64 revision() const noexcept { return revision_.load(); }
    // Задач в очереди и выполняется сейчас.
    int pendingTasks() const;

    // Растр страницы при dpi (BGRA → QImage ARGB32_Premultiplied) с ожиданием; пустой — ошибка
    // (errorString()). Для окна этапа 0 — до тайлов PageView (сессия 2.2).
    QImage renderPage(int index, qreal dpi);

    template <class F>
    auto post(F&& fn, TaskPriority priority = TaskPriority::Normal, QString name = {})
        -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>>;

    template <class F>
    auto edit(QString name, F&& fn) -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>>;

    // С ожиданием: исключение задачи бросается здесь. Из потока документа (внутри задачи) выполняется сразу.
    template <class F>
    auto postSync(F&& fn, TaskPriority priority = TaskPriority::Urgent)
        -> std::invoke_result_t<std::decay_t<F>&, TaskContext&>;

    // Отменить всё: очередь — MuError(FZ_ERROR_ABORT) в QFuture, выполняемой задаче — abort = 1.
    void cancelAll();

    // TaskFailed по исключению задачи (MuError → код и вид, прочее — Other).
    static TaskFailed describe(const std::exception_ptr& error, const QString& task = {});
    // Исключение «задача отменена» (MuError с FZ_ERROR_ABORT).
    static std::exception_ptr abortedError();

signals:
    void opened(const QString& path);
    void closed();
    void pageChanged(int page);
    void documentChanged(quint64 revision);
    void taskFailed(const ot::TaskFailed& failure);

private:
    friend class TaskContext;
    using RunFn = std::function<std::exception_ptr(TaskContext&)>;
    using AbandonFn = std::function<void()>;

    struct Task {
        QString name;
        TaskPriority priority = TaskPriority::Normal;
        bool isEdit = false;
        RunFn run;
        AbandonFn abandon;
        std::shared_ptr<TaskCancel> cancel;
    };

    void enqueue(QString name, TaskPriority priority, bool isEdit, RunFn run, AbandonFn abandon);
    void loop();
    TaskContext* currentContext() const noexcept { return current_; }

    template <class R, class F>
    static RunFn wrap(const std::shared_ptr<QPromise<R>>& promise, F&& fn);
    template <class R>
    static AbandonFn abandoner(const std::shared_ptr<QPromise<R>>& promise);

    const quint64 id_;
    std::thread::id threadId_;
    class Thread;
    std::unique_ptr<Thread> thread_;

    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> queue_;
    std::shared_ptr<TaskCancel> running_;  // cookie выполняемой задачи (под mutex_)
    bool busy_ = false;
    bool stopping_ = false;

    // Только в потоке документа.
    std::shared_ptr<Document> doc_;
    TaskContext* current_ = nullptr;

    // Итог открытия — читается из любого потока (под mutex_).
    OpenSummary summary_;
    QString path_;
    QString error_;
    std::atomic<quint64> revision_{0};
};

// ---------------------------------------------------------------- шаблоны

template <class R, class F>
DocumentSession::RunFn DocumentSession::wrap(const std::shared_ptr<QPromise<R>>& promise, F&& fn) {
    // fn — в shared_ptr: std::function требует копируемого, а задача может держать move-only.
    auto body = std::make_shared<std::decay_t<F>>(std::forward<F>(fn));
    return [promise, body](TaskContext& tc) -> std::exception_ptr {
        if (promise->isCanceled()) {  // QFuture::cancel() до начала — не выполнять
            promise->finish();
            return {};
        }
        std::exception_ptr error;
        try {
            if constexpr (std::is_void_v<R>) {
                (*body)(tc);
            } else {
                promise->addResult((*body)(tc));
            }
        } catch (...) {
            error = std::current_exception();
            promise->setException(error);
        }
        promise->finish();
        return error;
    };
}

template <class R>
DocumentSession::AbandonFn DocumentSession::abandoner(const std::shared_ptr<QPromise<R>>& promise) {
    return [promise] {
        promise->setException(abortedError());
        promise->finish();
    };
}

template <class F>
auto DocumentSession::post(F&& fn, TaskPriority priority, QString name)
    -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>> {
    using R = std::invoke_result_t<std::decay_t<F>&, TaskContext&>;
    auto promise = std::make_shared<QPromise<R>>();
    QFuture<R> future = promise->future();
    promise->start();
    enqueue(std::move(name), priority, false, wrap<R>(promise, std::forward<F>(fn)), abandoner<R>(promise));
    return future;
}

template <class F>
auto DocumentSession::edit(QString name, F&& fn)
    -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>> {
    using R = std::invoke_result_t<std::decay_t<F>&, TaskContext&>;
    auto promise = std::make_shared<QPromise<R>>();
    QFuture<R> future = promise->future();
    promise->start();
    // Правки идут по порядку: обычный приоритет, FIFO среди равных.
    enqueue(std::move(name), TaskPriority::Normal, true, wrap<R>(promise, std::forward<F>(fn)),
            abandoner<R>(promise));
    return future;
}

template <class F>
auto DocumentSession::postSync(F&& fn, TaskPriority priority)
    -> std::invoke_result_t<std::decay_t<F>&, TaskContext&> {
    using R = std::invoke_result_t<std::decay_t<F>&, TaskContext&>;
    if (isSessionThread() && current_ != nullptr) {
        return fn(*current_);  // изнутри задачи: очередь ждала бы сама себя
    }
    QFuture<R> future = post(std::forward<F>(fn), priority);
    future.waitForFinished();  // бросает исключение задачи
    if constexpr (!std::is_void_v<R>) {
        return future.result();
    }
}

}  // namespace ot

Q_DECLARE_METATYPE(ot::TaskFailed)
