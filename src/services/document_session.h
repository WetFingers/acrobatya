// DocumentSession — документ, открытый в программе, и его поток (эталон 4.1–4.2). Один QThread на документ
// с собственной очередью задач (std::deque + условная переменная, не QEventLoop в потоке — проще отмена и
// приоритеты). Всё, что трогает fz_document, fz_page и pdf_*, идёт задачей в этом потоке:
//   post(fn, priority) → QFuture<T> (через QPromise); fn(TaskContext&) выполняется в потоке документа;
//   edit(name, fn)      → правка: fn идёт внутри Journal::Operation(name) (эталон 4.3; исключение —
//                         pdf_abandon_operation, документ как был), после неё revision++, documentChanged,
//                         pageChanged по tc.touchPage(n); undo()/redo() — шаги истории той же очередью;
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
#include <optional>
#include <thread>
#include <type_traits>
#include <vector>

#include "core/doc/autosave.h"
#include "core/doc/saver.h"
#include "core/mu/error.h"

namespace ot {

class Document;
class DisplayCache;
class DisplayList;
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

// Состояние истории правок для меню «Отменить: …» / «Вернуть: …» (имена — английские коды операций edit();
// подпись по-русски выбирает ui).
struct UndoState {
    bool canUndo = false;
    bool canRedo = false;
    QString undoName;
    QString redoName;
    bool operator==(const UndoState&) const = default;
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
    // Размеры для раскладки, пункты, с учётом /CropBox и /Rotate (fit.h layoutPageSize: 0 × 0 — Letter,
    // больше 14 400 pt — уменьшено; масштаб такой страницы — размер раскладки / DisplayList::bounds()).
    QVector<QSizeF> pageSizes;
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
    // Открыть документ с правками, восстановленными автосохранением (Autosave::recover в потоке документа):
    // путь — путь записи, история undo/redo — из журнала, файл не сохранён. Ошибка — в OpenSummary::error
    // (hint "recover-source-changed", "recover-mismatch", "recover-failed").
    QFuture<OpenSummary> recoverAsync(const QString& autosaveDir, const AutosaveEntry& entry,
                                      const QString& password = {});
    // Закрыть документ после уже поставленных задач (приоритет Background) и дождаться; сигнал closed.
    void close();

    bool isOpen() const;
    QString path() const;
    QString errorString() const;
    OpenSummary summary() const;
    int pageCount() const;
    QSizeF pageSizePoints(int index) const;
    // Номер правки: растёт после каждой успешной edit(), undo() и redo() (поле revision ключа тайлов).
    quint64 revision() const noexcept { return revision_.load(); }
    // История правок — из любого потока (обновляется после каждой правки, undo, redo, открытия).
    UndoState undoState() const;
    // Задач в очереди и выполняется сейчас.
    int pendingTasks() const;

    // Display list страницы из кеша документа (LRU 40 страниц, правка страницы его сбрасывает) — для
    // RenderPool: единственное, что уходит из потока документа.
    QFuture<std::shared_ptr<const DisplayList>> displayList(int page, bool annotations = true,
                                                            TaskPriority priority = TaskPriority::High);

    // Растр страницы при dpi (BGRA → QImage ARGB32_Premultiplied) с ожиданием; пустой — ошибка
    // (errorString()). Для окна этапа 0 — до тайлов PageView (сессия 2.2).
    QImage renderPage(int index, qreal dpi);

    template <class F>
    auto post(F&& fn, TaskPriority priority = TaskPriority::Normal, QString name = {})
        -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>>;

    template <class F>
    auto edit(QString name, F&& fn) -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>>;

    // Отменить / вернуть шаг истории (очередь правок): true — сделано, false — нечего. Страницы шага
    // сбрасывают display lists и приходят pageChanged; структурный шаг пересчитывает раскладку (summary()).
    QFuture<bool> undo();
    QFuture<bool> redo();

    // Сохранить по правилам Saver (эталон 4.4) — очередью правок: «Сохранить» в свой путь, «Сохранить как» —
    // документ переходит на новый файл. Записанный файл переоткрывается в этом же потоке (тот же Document):
    // display lists сброшены, флаги и раскладка — заново, история пуста; успех — сигнал saved(path). Отказ и
    // ошибка — в SaveReport, не исключение (нет документа — исключение в QFuture).
    QFuture<SaveReport> save(const SaveOptions& options = {});
    QFuture<SaveReport> saveAs(const QString& path, const SaveOptions& options = {});

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
    void undoStateChanged();
    void saved(const QString& path);

private:
    friend class TaskContext;
    // run — выполнить и положить результат или исключение в QPromise (без finish); finish — завершить QFuture
    // после того, как цикл обновил revision, раскладку и историю (ждущий future видит их новыми);
    // abandon — задача снята с очереди (MuError Aborted).
    using RunFn = std::function<std::exception_ptr(TaskContext&)>;
    using FinishFn = std::function<void()>;
    using AbandonFn = std::function<void()>;

    struct Task {
        QString name;
        TaskPriority priority = TaskPriority::Normal;
        bool isEdit = false;
        RunFn run;
        FinishFn finish;
        AbandonFn abandon;
        std::shared_ptr<TaskCancel> cancel;
    };

    void enqueue(QString name, TaskPriority priority, bool isEdit, RunFn run, FinishFn finish,
                 AbandonFn abandon);
    void loop();
    TaskContext* currentContext() const noexcept { return current_; }

    // Обещание задачи: результат ждёт finish — QFuture::result() возвращается, когда результат добавлен, а он
    // добавляется только после того, как цикл обновил revision, раскладку и историю.
    template <class R>
    struct Pending {
        QPromise<R> promise;
        std::optional<std::conditional_t<std::is_void_v<R>, int, R>> value;
    };
    template <class R>
    static std::shared_ptr<Pending<R>> makePending();

    template <class R, class F>
    static RunFn wrap(const std::shared_ptr<Pending<R>>& pending, F&& fn);
    // Правка: fn внутри операции журнала (beginEdit → fn → commitEdit; исключение → abandonEdit).
    template <class R, class F>
    static RunFn wrapEdit(const std::shared_ptr<Pending<R>>& pending, QString name, F&& fn);
    static void beginEdit(TaskContext& tc, const QString& name);
    static void commitEdit(TaskContext& tc);
    static void abandonEdit(TaskContext& tc) noexcept;
    QFuture<bool> history(bool back);
    QFuture<SaveReport> saveTo(const QString& path, const SaveOptions& options);
    // После Saver::save: документ переоткрыт — сводка, списки и история заново (в потоке документа).
    void afterSave(const SaveReport& report);
    // После задачи: раскладка (структурная правка) и состояние истории — в summary_/undoState_ с сигналами.
    void refreshLayout();
    void refreshUndoState();
    // Поставить открытый документ (или ошибку) в сессию: сводка, путь, история (в потоке документа).
    void install(std::unique_ptr<Document> doc, OpenSummary& s, const QString& path);
    template <class R>
    static FinishFn finisher(const std::shared_ptr<Pending<R>>& pending);
    template <class R>
    static AbandonFn abandoner(const std::shared_ptr<Pending<R>>& pending);

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
    std::unique_ptr<DisplayCache> displayCache_;
    TaskContext* current_ = nullptr;

    // Итог открытия — читается из любого потока (под mutex_).
    OpenSummary summary_;
    QString path_;
    QString error_;
    UndoState undoState_;
    std::atomic<quint64> revision_{0};
};

// ---------------------------------------------------------------- шаблоны

template <class R>
std::shared_ptr<DocumentSession::Pending<R>> DocumentSession::makePending() {
    auto pending = std::make_shared<Pending<R>>();
    pending->promise.start();
    return pending;
}

template <class R, class F>
DocumentSession::RunFn DocumentSession::wrap(const std::shared_ptr<Pending<R>>& pending, F&& fn) {
    // fn — в shared_ptr: std::function требует копируемого, а задача может держать move-only.
    auto body = std::make_shared<std::decay_t<F>>(std::forward<F>(fn));
    return [pending, body](TaskContext& tc) -> std::exception_ptr {
        if (pending->promise.isCanceled()) {  // QFuture::cancel() до начала — не выполнять
            return {};
        }
        std::exception_ptr error;
        try {
            if constexpr (std::is_void_v<R>) {
                (*body)(tc);
            } else {
                pending->value.emplace((*body)(tc));
            }
        } catch (...) {
            error = std::current_exception();
            pending->promise.setException(error);
        }
        return error;
    };
}

template <class R, class F>
DocumentSession::RunFn DocumentSession::wrapEdit(const std::shared_ptr<Pending<R>>& pending, QString name,
                                                 F&& fn) {
    auto body = std::make_shared<std::decay_t<F>>(std::forward<F>(fn));
    return [pending, body, name = std::move(name)](TaskContext& tc) -> std::exception_ptr {
        if (pending->promise.isCanceled()) {
            return {};
        }
        std::exception_ptr error;
        try {
            beginEdit(tc, name);
            if constexpr (std::is_void_v<R>) {
                (*body)(tc);
                commitEdit(tc);
            } else {
                R result = (*body)(tc);
                commitEdit(tc);  // результат отдаётся только зафиксированной правки
                pending->value.emplace(std::move(result));
            }
        } catch (...) {
            abandonEdit(tc);
            error = std::current_exception();
            pending->promise.setException(error);
        }
        return error;
    };
}

template <class R>
DocumentSession::FinishFn DocumentSession::finisher(const std::shared_ptr<Pending<R>>& pending) {
    return [pending] {
        if constexpr (!std::is_void_v<R>) {
            if (pending->value) {
                pending->promise.addResult(std::move(*pending->value));
                pending->value.reset();
            }
        }
        pending->promise.finish();
    };
}

template <class R>
DocumentSession::AbandonFn DocumentSession::abandoner(const std::shared_ptr<Pending<R>>& pending) {
    return [pending] {
        pending->promise.setException(abortedError());
        pending->promise.finish();
    };
}

template <class F>
auto DocumentSession::post(F&& fn, TaskPriority priority, QString name)
    -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>> {
    using R = std::invoke_result_t<std::decay_t<F>&, TaskContext&>;
    auto pending = makePending<R>();
    QFuture<R> future = pending->promise.future();
    enqueue(std::move(name), priority, false, wrap<R>(pending, std::forward<F>(fn)), finisher<R>(pending),
            abandoner<R>(pending));
    return future;
}

template <class F>
auto DocumentSession::edit(QString name, F&& fn)
    -> QFuture<std::invoke_result_t<std::decay_t<F>&, TaskContext&>> {
    using R = std::invoke_result_t<std::decay_t<F>&, TaskContext&>;
    auto pending = makePending<R>();
    QFuture<R> future = pending->promise.future();
    // Правки идут по порядку: обычный приоритет, FIFO среди равных.
    QString op = name;
    enqueue(std::move(name), TaskPriority::Normal, true,
            wrapEdit<R>(pending, std::move(op), std::forward<F>(fn)), finisher<R>(pending),
            abandoner<R>(pending));
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
