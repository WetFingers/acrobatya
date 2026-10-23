#include "services/document_session.h"

#include <QThread>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/render/display_cache.h"
#include "core/render/fit.h"
#include "core/render/renderer.h"
#include "services/task_context.h"

namespace ot {

namespace {

std::atomic<quint64> nextSessionId{1};

QString hintFor(ErrorKind kind) {
    switch (kind) {
        case ErrorKind::Damaged:
            return QStringLiteral("open-repaired");
        case ErrorKind::Unsupported:
            return QStringLiteral("unsupported");
        case ErrorKind::Limit:
            return QStringLiteral("limit");
        case ErrorKind::System:
            return QStringLiteral("close-document");
        case ErrorKind::Aborted:
        case ErrorKind::None:
            return {};
        default:
            return QStringLiteral("report");
    }
}

}  // namespace

// ---------------------------------------------------------------- PageHandle

PageHandle::PageHandle(fz_context* ctx, fz_page* page, int index) noexcept
    : ctx_(ctx), page_(page), index_(index), thread_(std::this_thread::get_id()) {}

PageHandle::PageHandle(PageHandle&& other) noexcept
    : ctx_(other.ctx_),
      page_(std::exchange(other.page_, nullptr)),
      index_(other.index_),
      thread_(other.thread_) {}

PageHandle::~PageHandle() {
    if (page_ != nullptr) {
        // fz_page отдаётся в том же потоке документа, где загружен (контекст — один поток одновременно).
        assert(std::this_thread::get_id() == thread_);
        fz_drop_page(ctx_, page_);
    }
}

// ---------------------------------------------------------------- TaskContext

Document& TaskContext::document() const {
    if (!session_.doc_) {
        throw std::logic_error("DocumentSession: no document is open");
    }
    return *session_.doc_;
}

bool TaskContext::hasDocument() const noexcept { return static_cast<bool>(session_.doc_); }

fz_context* TaskContext::context() const { return document().context().get(); }

PageHandle TaskContext::page(int index) const {
    Document& doc = document();
    fz_context* ctx = doc.context().get();
    fz_page* loaded = nullptr;
    mu::call(ctx, [&] { loaded = fz_load_page(ctx, doc.handle(), index); });
    return PageHandle(ctx, loaded, index);
}

std::shared_ptr<const DisplayList> TaskContext::displayList(int index, bool annotations) const {
    return session_.displayCache_->get(document(), index, annotations, cookie_);
}

void TaskContext::touchPage(int index) {
    session_.displayCache_->invalidate(index);
    if (std::find(touched_.begin(), touched_.end(), index) == touched_.end()) {
        touched_.push_back(index);
    }
}

// ---------------------------------------------------------------- DocumentSession

class DocumentSession::Thread : public QThread {
public:
    explicit Thread(DocumentSession& owner) : owner_(owner) {}

protected:
    void run() override { owner_.loop(); }

private:
    DocumentSession& owner_;
};

DocumentSession::DocumentSession(QObject* parent)
    : QObject(parent), id_(nextSessionId.fetch_add(1)), displayCache_(std::make_unique<DisplayCache>()) {
    qRegisterMetaType<ot::TaskFailed>();
    thread_ = std::make_unique<Thread>(*this);
    thread_->setObjectName(QStringLiteral("DocumentSession-%1").arg(id_));
    thread_->start();
    // Дождаться id потока: isSessionThread() и postSync() опираются на него.
    std::unique_lock lock(mutex_);
    wake_.wait(lock, [this] { return threadId_ != std::thread::id(); });
}

DocumentSession::~DocumentSession() {
    cancelAll();
    {
        const std::lock_guard lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    thread_->wait();
}

void DocumentSession::enqueue(QString name, TaskPriority priority, bool isEdit, RunFn run,
                              AbandonFn abandon) {
    Task task;
    task.name = std::move(name);
    task.priority = priority;
    task.isEdit = isEdit;
    task.run = std::move(run);
    task.abandon = std::move(abandon);
    task.cancel = std::make_shared<TaskCancel>();
    bool rejected = false;
    {
        const std::lock_guard lock(mutex_);
        if (stopping_) {
            rejected = true;
        } else {
            queue_.push_back(std::move(task));
        }
    }
    if (rejected) {
        task.abandon();
        return;
    }
    wake_.notify_all();
}

void DocumentSession::loop() {
    {
        const std::lock_guard lock(mutex_);
        threadId_ = std::this_thread::get_id();
    }
    wake_.notify_all();
    for (;;) {
        Task task;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                break;  // stopping_
            }
            // Старший приоритет; среди равных — первый поставленный.
            auto best = queue_.begin();
            for (auto it = queue_.begin(); it != queue_.end(); ++it) {
                if (it->priority > best->priority) {
                    best = it;
                }
            }
            task = std::move(*best);
            queue_.erase(best);
            running_ = task.cancel;
            busy_ = true;
        }
        TaskContext tc(*this, &task.cancel->cookie);
        current_ = &tc;
        const std::exception_ptr error = task.run(tc);
        current_ = nullptr;
        {
            const std::lock_guard lock(mutex_);
            running_.reset();
            busy_ = false;
        }
        wake_.notify_all();
        if (task.isEdit && !error) {
            emit documentChanged(revision_.fetch_add(1) + 1);
        }
        for (const int page : tc.touched_) {
            emit pageChanged(page);
        }
        if (error) {
            const TaskFailed failed = describe(error, task.name);
            if (failed.kind != ErrorKind::Aborted) {
                emit taskFailed(failed);
            }
        }
    }
    // Остановка: документ закрывается в своём потоке (его контекст — только этого потока).
    displayCache_->clear();
    doc_.reset();
}

void DocumentSession::cancelAll() {
    std::deque<Task> dropped;
    {
        const std::lock_guard lock(mutex_);
        dropped.swap(queue_);
        if (running_) {
            running_->cookie.abort = 1;
        }
    }
    for (Task& task : dropped) {
        task.abandon();
    }
}

int DocumentSession::pendingTasks() const {
    const std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size()) + (busy_ ? 1 : 0);
}

TaskFailed DocumentSession::describe(const std::exception_ptr& error, const QString& task) {
    TaskFailed f;
    f.task = task;
    try {
        std::rethrow_exception(error);
    } catch (const MuError& e) {
        f.code = e.code();
        f.kind = e.kind();
        f.message = QString::fromUtf8(e.what());
    } catch (const std::exception& e) {
        f.kind = ErrorKind::Other;
        f.message = QString::fromUtf8(e.what());
    } catch (...) {
        f.kind = ErrorKind::Other;
        f.message = QStringLiteral("unknown exception");
    }
    f.hint = hintFor(f.kind);
    return f;
}

std::exception_ptr DocumentSession::abortedError() {
    return std::make_exception_ptr(MuError(FZ_ERROR_ABORT, "task cancelled"));
}

// ---------------------------------------------------------------- открытие и закрытие

QFuture<OpenSummary> DocumentSession::openAsync(const QString& path, const QString& password) {
    return post(
        [this, path, password](TaskContext&) {
            displayCache_->clear();
            doc_.reset();
            OpenSummary s;
            OpenResult r = Document::open(std::filesystem::path(path.toStdWString()), password.toStdString());
            s.ok = r.ok;
            s.needsPassword = r.needsPassword;
            s.repaired = r.repaired;
            s.pdfa = r.pdfa;
            s.isSigned = r.isSigned;
            s.hasForm = r.hasForm;
            s.hasXfa = r.hasXfa;
            s.error.code = r.error.code;
            s.error.kind = r.error.kind;
            s.error.message = QString::fromStdString(r.error.message);
            s.error.hint = hintFor(r.error.kind);
            if (r.ok) {
                try {
                    s.isPdf = r.document->isPdf();
                    s.pageCount = r.document->pageCount();
                    s.pageSizes.reserve(s.pageCount);
                    for (int i = 0; i < s.pageCount; ++i) {
                        // Раскладка: пустые страницы — Letter, больше 200 дюймов — уменьшены (fit.h).
                        const PageSize size = r.document->pageSize(i);
                        const LayoutSize layout = layoutPageSize(size.width, size.height);
                        s.pageSizes.push_back(QSizeF(layout.width, layout.height));
                    }
                    doc_ = std::shared_ptr<Document>(std::move(r.document));
                } catch (const MuError& e) {
                    s = OpenSummary{};
                    s.error.code = e.code();
                    s.error.kind = e.kind();
                    s.error.message = QString::fromUtf8(e.what());
                    s.error.hint = hintFor(e.kind());
                }
            } else if (r.needsPassword) {
                s.error.message = QStringLiteral("password required");
            }
            {
                const std::lock_guard lock(mutex_);
                summary_ = s;
                path_ = s.ok ? path : QString();
                error_ = s.ok ? QString() : s.error.message;
            }
            return s;
        },
        TaskPriority::Urgent, QStringLiteral("open"));
}

bool DocumentSession::open(const QString& path, const QString& password) {
    const OpenSummary s = openAsync(path, password).result();
    if (s.ok) {
        emit opened(path);
    }
    return s.ok;
}

void DocumentSession::close() {
    bool wasOpen = false;
    {
        const std::lock_guard lock(mutex_);
        wasOpen = summary_.ok;
    }
    QFuture<void> done = post(
        [this](TaskContext&) {
            displayCache_->clear();
            doc_.reset();
            const std::lock_guard lock(mutex_);
            summary_ = OpenSummary{};
            path_.clear();
        },
        TaskPriority::Background, QStringLiteral("close"));
    try {
        done.waitForFinished();
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): отменённое закрытие — документ
                                       // закроет деструктор
    }
    if (wasOpen) {
        emit closed();
    }
}

bool DocumentSession::isOpen() const {
    const std::lock_guard lock(mutex_);
    return summary_.ok;
}

QString DocumentSession::path() const {
    const std::lock_guard lock(mutex_);
    return path_;
}

QString DocumentSession::errorString() const {
    const std::lock_guard lock(mutex_);
    return error_;
}

OpenSummary DocumentSession::summary() const {
    const std::lock_guard lock(mutex_);
    return summary_;
}

int DocumentSession::pageCount() const {
    const std::lock_guard lock(mutex_);
    return summary_.pageCount;
}

QSizeF DocumentSession::pageSizePoints(int index) const {
    const std::lock_guard lock(mutex_);
    if (index < 0 || index >= summary_.pageSizes.size()) {
        return {};
    }
    return summary_.pageSizes[index];
}

QFuture<std::shared_ptr<const DisplayList>> DocumentSession::displayList(int page, bool annotations,
                                                                         TaskPriority priority) {
    return post([page, annotations](TaskContext& tc) { return tc.displayList(page, annotations); }, priority,
                QStringLiteral("display-list"));
}

QImage DocumentSession::renderPage(int index, qreal dpi) {
    if (!isOpen()) {
        return {};
    }
    try {
        return postSync([index, dpi](TaskContext& tc) {
            const Renderer renderer(tc.document());
            RenderRequest req;
            req.page = index;
            req.matrix = Matrix::dpi(static_cast<float>(dpi));
            req.scissor = renderer.deviceBounds(index, req.matrix);
            if (req.scissor.empty()) {
                return QImage();
            }
            QImage img(req.scissor.width(), req.scissor.height(), QImage::Format_ARGB32_Premultiplied);
            if (img.isNull()) {
                throw MuError(FZ_ERROR_LIMIT, "page raster does not fit in memory");
            }
            renderer.render(req, img.bits(), static_cast<int>(img.bytesPerLine()),
                            PixelFormat::Bgra32Premultiplied, tc.cookie());
            return img;
        });
    } catch (const std::exception& e) {
        const std::lock_guard lock(mutex_);
        error_ = QString::fromUtf8(e.what());
        return {};
    }
}

}  // namespace ot
