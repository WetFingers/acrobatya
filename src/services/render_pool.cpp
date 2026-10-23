#include "services/render_pool.h"

#include <QPointer>
#include <QThread>

#include <algorithm>
#include <atomic>
#include <exception>
#include <vector>

#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/render/display_cache.h"
#include "core/render/renderer.h"
#include "services/task_context.h"

namespace ot {

class RenderPool::Job : public QRunnable {
public:
    Job(RenderPool& pool, quint64 id, TileRequest request, QObject* context, Callback done)
        : pool_(pool),
          id_(id),
          request_(std::move(request)),
          context_(context),
          hasContext_(context != nullptr),
          done_(std::move(done)) {
        setAutoDelete(false);  // владеет RenderPool::jobs_; QThreadPool проверяет autoDelete до run()
    }

    void run() override {
        // Держим себя: finished() убирает задачу из jobs_ до выхода из run().
        std::shared_ptr<Job> self = pool_.find(id_);
        if (!self) {
            return;
        }
        TileResult result;
        result.key = request_.key;
        result.rect = request_.rect;
        try {
            if (cancel_.cookie.abort != 0) {
                std::rethrow_exception(DocumentSession::abortedError());
            }
            if (!request_.list) {
                throw std::invalid_argument("RenderPool: tile request without display list");
            }
            QImage image(request_.rect.width(), request_.rect.height(), QImage::Format_ARGB32_Premultiplied);
            if (image.isNull()) {
                throw MuError(FZ_ERROR_LIMIT, "tile raster does not fit in memory");
            }
            // Свой клон контекста у каждого потока пула; пиксели — прямо в память QImage.
            renderDisplayList(MuContext::forThisThread().get(), *request_.list, request_.matrix,
                              request_.rect, image.bits(), static_cast<int>(image.bytesPerLine()),
                              PixelFormat::Bgra32Premultiplied, &cancel_.cookie, request_.aa);
            image.setDevicePixelRatio(request_.dpr);
            result.image = std::move(image);
        } catch (...) {
            result.error = DocumentSession::describe(std::current_exception());
            result.aborted = result.error.kind == ErrorKind::Aborted;
        }
        pool_.deliver(self, std::move(result));
        pool_.finished(id_);
    }

    quint64 id() const noexcept { return id_; }
    const TileRequest& request() const noexcept { return request_; }
    void setPriority(int priority) noexcept { request_.priority = priority; }
    void abort() noexcept { cancel_.cookie.abort = 1; }

    RenderPool& pool_;
    const quint64 id_;
    TileRequest request_;
    QPointer<QObject> context_;
    const bool hasContext_;
    Callback done_;
    TaskCancel cancel_;
    std::atomic<bool> delivered_{false};
};

RenderPool::RenderPool(int threads, QObject* parent) : QObject(parent) {
    qRegisterMetaType<ot::TileResult>();
    pool_.setMaxThreadCount(threads > 0 ? threads : std::max(1, QThread::idealThreadCount() - 2));
    pool_.setObjectName(QStringLiteral("RenderPool"));
}

RenderPool::~RenderPool() {
    cancelAll();
    pool_.waitForDone();
}

int RenderPool::maxThreads() const { return pool_.maxThreadCount(); }

quint64 RenderPool::submit(TileRequest request, QObject* context, Callback done) {
    const int priority = request.priority;
    std::shared_ptr<Job> job;
    {
        const std::lock_guard lock(mutex_);
        const quint64 id = nextId_++;
        job = std::make_shared<Job>(*this, id, std::move(request), context, std::move(done));
        jobs_.emplace(id, job);
    }
    pool_.start(job.get(), priority);
    return job->id();
}

std::shared_ptr<RenderPool::Job> RenderPool::find(quint64 id) const {
    const std::lock_guard lock(mutex_);
    const auto it = jobs_.find(id);
    return it == jobs_.end() ? nullptr : it->second;
}

void RenderPool::finished(quint64 id) {
    const std::lock_guard lock(mutex_);
    jobs_.erase(id);
}

void RenderPool::deliver(const std::shared_ptr<Job>& job, TileResult result) {
    if (job->delivered_.exchange(true)) {
        return;
    }
    emit tileReady(result);
    if (!job->done_) {
        return;
    }
    if (job->hasContext_) {
        // Через сам пул (живёт до конца задач): объект context проверяется уже в его потоке.
        QMetaObject::invokeMethod(
            this,
            [context = job->context_, done = job->done_, result = std::move(result)] {
                if (context) {
                    done(result);
                }
            },
            Qt::QueuedConnection);
    } else {
        job->done_(result);
    }
}

void RenderPool::abortJob(const std::shared_ptr<Job>& job) {
    if (pool_.tryTake(job.get())) {
        // Ещё не начата: снята с очереди, результат «отменено» — сразу.
        TileResult result;
        result.key = job->request().key;
        result.rect = job->request().rect;
        result.aborted = true;
        result.error = DocumentSession::describe(DocumentSession::abortedError());
        deliver(job, std::move(result));
        finished(job->id());
    } else {
        job->abort();  // идёт: fz_cookie прервёт рендер между узлами display list
    }
}

void RenderPool::cancel(quint64 id) {
    if (const auto job = find(id)) {
        abortJob(job);
    }
}

void RenderPool::cancelDocument(std::uint64_t doc) {
    std::vector<std::shared_ptr<Job>> victims;
    {
        const std::lock_guard lock(mutex_);
        for (const auto& [id, job] : jobs_) {
            if (job->request().key.doc == doc) {
                victims.push_back(job);
            }
        }
    }
    for (const auto& job : victims) {
        abortJob(job);
    }
}

void RenderPool::cancelAll() {
    std::vector<std::shared_ptr<Job>> victims;
    {
        const std::lock_guard lock(mutex_);
        for (const auto& entry : jobs_) {
            victims.push_back(entry.second);
        }
    }
    for (const auto& job : victims) {
        abortJob(job);
    }
}

void RenderPool::retarget(std::uint64_t doc, const std::function<bool(const TileKey&)>& keep,
                          int visiblePriority) {
    std::vector<std::shared_ptr<Job>> jobs;
    {
        const std::lock_guard lock(mutex_);
        for (const auto& [id, job] : jobs_) {
            if (job->request().key.doc == doc) {
                jobs.push_back(job);
            }
        }
    }
    for (const auto& job : jobs) {
        if (keep(job->request().key)) {
            (void)setPriority(job->id(), visiblePriority);
        } else {
            abortJob(job);
        }
    }
}

bool RenderPool::setPriority(quint64 id, int priority) {
    const auto job = find(id);
    if (!job || !pool_.tryTake(job.get())) {
        return false;
    }
    job->setPriority(priority);
    pool_.start(job.get(), priority);
    return true;
}

int RenderPool::pending() const {
    const std::lock_guard lock(mutex_);
    return static_cast<int>(jobs_.size());
}

bool RenderPool::waitForDone(int msecs) { return pool_.waitForDone(msecs); }

}  // namespace ot
