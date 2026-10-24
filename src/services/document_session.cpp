#include "services/document_session.h"

#include <QThread>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/props.h"
#include "core/links/layers.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/pages/labels.h"
#include "core/render/display_cache.h"
#include "core/render/fit.h"
#include "core/render/renderer.h"
#include "core/text/stext.h"
#include "core/util/paths.h"
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

// Видимость слоёв открытого документа; испорченный /OCProperties не роняет открытие — как без слоёв.
quint64 layersHashOf(Document& doc) {
    try {
        return layers::stateHash(doc);
    } catch (const MuError& e) {
        spdlog::warn("layers: cannot read optional content: {}", e.what());
        return 0;
    }
}

// Логические номера страниц; испорченное дерево /PageLabels — номера.
QStringList pageLabelsOf(Document& doc) {
    QStringList out;
    try {
        for (const std::string& label : pages::pageLabels(doc)) {
            out << QString::fromStdString(label);
        }
    } catch (const MuError& e) {
        spdlog::warn("page labels: {}", e.what());
        out.clear();
    }
    return out;
}

InitialView initialViewOf(Document& doc) {
    try {
        return readInitialView(doc);
    } catch (const MuError& e) {
        spdlog::warn("initial view: {}", e.what());
        return {};
    }
}

// Размеры страниц для раскладки: пустые — Letter, больше 200 дюймов — уменьшены (fit.h).
QVector<QSizeF> layoutSizes(const Document& doc, int pageCount) {
    QVector<QSizeF> sizes;
    sizes.reserve(pageCount);
    for (int i = 0; i < pageCount; ++i) {
        // Повреждённая страница не роняет открытие: в раскладке — Letter, при рендере — заглушка
        // «Страница не отрисована» (эталон 3.1, «Граничные случаи»).
        PageSize size{0, 0};
        try {
            size = doc.pageSize(i);
        } catch (const MuError& e) {
            spdlog::warn("page {}: cannot measure for layout: {}", i + 1, e.what());
        }
        const LayoutSize layout = layoutPageSize(size.width, size.height);
        sizes.push_back(QSizeF(layout.width, layout.height));
    }
    return sizes;
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

std::shared_ptr<const TextPage> TaskContext::textPage(int index, const TextOptions& options) const {
    return session_.textCache_->get(document(), index, options, cookie_);
}

void TaskContext::touchPage(int index) {
    session_.displayCache_->invalidate(index);
    session_.textCache_->invalidate(index);
    if (op_) {
        op_->invalidate(index);
    }
    if (std::find(touched_.begin(), touched_.end(), index) == touched_.end()) {
        touched_.push_back(index);
    }
}

void TaskContext::markHeavyEdit() {
    if (op_) {
        op_->markHeavy();
    }
}

void TaskContext::setLayersState(std::uint64_t hash) {
    session_.displayCache_->clear();
    session_.textCache_->clear();
    layersChanged_ = true;
    layersHash_ = hash;
}

void TaskContext::touchAllPages() {
    session_.displayCache_->clear();
    session_.textCache_->clear();
    if (op_) {
        op_->invalidateAll();
    }
    touchedAll_ = true;
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
    : QObject(parent),
      id_(nextSessionId.fetch_add(1)),
      displayCache_(std::make_unique<DisplayCache>()),
      textCache_(std::make_unique<TextCache>()) {
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

void DocumentSession::enqueue(QString name, TaskPriority priority, bool isEdit, RunFn run, FinishFn finish,
                              AbandonFn abandon) {
    Task task;
    task.name = std::move(name);
    task.priority = priority;
    task.isEdit = isEdit;
    task.run = std::move(run);
    task.finish = std::move(finish);
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
        if (tc.touchedAll_) {
            refreshLayout();  // до сигналов: получатель documentChanged читает новую раскладку
        }
        {
            const std::lock_guard lock(mutex_);
            running_.reset();
            busy_ = false;
        }
        wake_.notify_all();
        if (task.isEdit && !error) {
            const quint64 revision = revision_.fetch_add(1) + 1;
            if (tc.touchedAll_) {
                emit structureChanged();
            }
            emit documentChanged(revision);
        }
        for (const int page : tc.touched_) {
            emit pageChanged(page);
        }
        if (tc.layersChanged_) {
            layersHash_.store(tc.layersHash_);
            emit layersChanged();
        }
        if (task.isEdit) {
            refreshUndoState();
        }
        task.finish();  // QFuture — после revision, раскладки и истории
        if (error) {
            const TaskFailed failed = describe(error, task.name);
            if (failed.kind != ErrorKind::Aborted) {
                emit taskFailed(failed);
            }
        }
    }
    // Остановка: документ закрывается в своём потоке (его контекст — только этого потока).
    displayCache_->clear();
    textCache_->clear();
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

// ---------------------------------------------------------------- правки и история

void DocumentSession::beginEdit(TaskContext& tc, const QString& name) {
    if (tc.hasDocument() && tc.document().journaled()) {
        tc.op_ = std::make_unique<Journal::Operation>(tc.document(), name.toStdString());
    }
}

void DocumentSession::commitEdit(TaskContext& tc) {
    if (tc.op_) {
        const std::unique_ptr<Journal::Operation> op = std::move(tc.op_);
        op->commit();  // ошибка — операция откатана, исключение уходит в QFuture
    }
}

void DocumentSession::abandonEdit(TaskContext& tc) noexcept { tc.op_.reset(); }

QFuture<bool> DocumentSession::undo() { return history(true); }

QFuture<bool> DocumentSession::redo() { return history(false); }

QFuture<bool> DocumentSession::history(bool back) {
    auto pending = makePending<bool>();
    QFuture<bool> future = pending->promise.future();
    auto step = [back](TaskContext& tc) {
        if (!tc.hasDocument() || !tc.document().journaled()) {
            return false;
        }
        UndoStack& history = tc.document().undoStack();
        if (back ? !history.canUndo() : !history.canRedo()) {
            return false;
        }
        // pdf_undo/pdf_redo пересобирают дерево страниц MuPDF; fz_page задачи не держат — сбросить списки.
        const TouchedPages touched = back ? history.undo() : history.redo();
        if (touched.all) {
            tc.touchAllPages();
        }
        for (const int page : touched.pages) {
            tc.touchPage(page);
        }
        return true;
    };
    enqueue(back ? QStringLiteral("undo") : QStringLiteral("redo"), TaskPriority::Normal, true,
            wrap<bool>(pending, std::move(step)), finisher<bool>(pending), abandoner<bool>(pending));
    return future;
}

QFuture<SaveReport> DocumentSession::save(const SaveOptions& options) { return saveTo(QString(), options); }

QFuture<SaveReport> DocumentSession::saveAs(const QString& path, const SaveOptions& options) {
    return saveTo(path, options);
}

QFuture<SaveReport> DocumentSession::saveCopy(const QString& path, const SaveOptions& options) {
    return post(
        [path, options](TaskContext& tc) {
            return Saver::saveCopy(tc.document(), std::filesystem::path(path.toStdWString()), options);
        },
        TaskPriority::Normal, QStringLiteral("save copy"));
}

QFuture<SaveReport> DocumentSession::saveTo(const QString& path, const SaveOptions& options) {
    return post(
        [this, path, options](TaskContext& tc) {
            Document& doc = tc.document();
            const SaveReport report =
                path.isEmpty() ? Saver::save(doc, options)
                               : Saver::save(doc, std::filesystem::path(path.toStdWString()), options);
            afterSave(report);
            return report;
        },
        TaskPriority::Normal, QStringLiteral("save"));
}

void DocumentSession::afterSave(const SaveReport& report) {
    displayCache_->clear();
    textCache_->clear();
    if (report.error == SaveError::ReopenFailed || doc_->handle() == nullptr) {
        // Записанный файл не открылся — документа больше нет.
        doc_.reset();
        {
            const std::lock_guard lock(mutex_);
            summary_ = OpenSummary{};
            path_.clear();
            error_ = QString::fromStdString(report.message);
        }
        refreshUndoState();
        emit closed();
        return;
    }
    const bool reopened = report.ok() || report.error == SaveError::ReplaceFailed;
    if (reopened) {
        OpenSummary s;
        try {
            s.ok = true;
            s.isPdf = doc_->isPdf();
            s.repaired = doc_->repaired();
            s.pdfa = doc_->isPdfA();
            s.isSigned = doc_->isSigned();
            s.hasForm = doc_->hasForm();
            s.hasXfa = doc_->hasXfa();
            s.pageCount = doc_->pageCount();
            s.pageSizes = layoutSizes(*doc_, s.pageCount);
            s.pageLayout = QString::fromStdString(props::pageLayout(*doc_));
            s.pageLabels = pageLabelsOf(*doc_);
            s.initial = initialViewOf(*doc_);
            s.permissions = Permissions::of(*doc_);
            s.pdfx = doc_->isPdfX();
            const QString docPath = QString::fromStdWString(doc_->path().wstring());
            const std::lock_guard lock(mutex_);
            summary_ = s;
            path_ = docPath;
        } catch (const MuError&) {  // NOLINT(bugprone-empty-catch): сводка останется прежней
        }
    }
    refreshUndoState();
    // Переоткрытый документ — снова в конфигурации слоёв /D.
    const quint64 layers = doc_ ? layersHashOf(*doc_) : 0;
    if (layersHash_.exchange(layers) != layers) {
        emit layersChanged();
    }
    if (report.ok()) {
        emit saved(path());
    }
}

void DocumentSession::refreshLayout() {
    if (!doc_) {
        return;
    }
    try {
        const int count = doc_->pageCount();
        QVector<QSizeF> sizes = layoutSizes(*doc_, count);
        QStringList labels = pageLabelsOf(*doc_);
        const std::lock_guard lock(mutex_);
        summary_.pageCount = count;
        summary_.pageSizes = std::move(sizes);
        summary_.pageLabels = std::move(labels);
    } catch (const MuError&) {  // NOLINT(bugprone-empty-catch): раскладка останется прежней, ошибку
                                // страницы покажет её рендер
    }
}

void DocumentSession::refreshUndoState() {
    UndoState state;
    if (doc_ && doc_->journaled()) {
        try {
            UndoStack& history = doc_->undoStack();
            state.canUndo = history.canUndo();
            state.canRedo = history.canRedo();
            state.undoName = QString::fromStdString(history.undoName());
            state.redoName = QString::fromStdString(history.redoName());
        } catch (const std::exception&) {
            state = UndoState{};
        }
    }
    bool changed = false;
    {
        const std::lock_guard lock(mutex_);
        changed = !(undoState_ == state);
        undoState_ = state;
    }
    if (changed) {
        emit undoStateChanged();
    }
}

UndoState DocumentSession::undoState() const {
    const std::lock_guard lock(mutex_);
    return undoState_;
}

// ---------------------------------------------------------------- открытие и закрытие

QFuture<OpenSummary> DocumentSession::openAsync(const QString& path, const QString& password) {
    return post(
        [this, path, password](TaskContext&) {
            displayCache_->clear();
            textCache_->clear();
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
            if (r.needsPassword) {
                s.error.message = QStringLiteral("password required");
            }
            install(r.ok ? std::move(r.document) : nullptr, s, path);
            return s;
        },
        TaskPriority::Urgent, QStringLiteral("open"));
}

QFuture<OpenSummary> DocumentSession::recoverAsync(const QString& autosaveDir, const AutosaveEntry& entry,
                                                   const QString& password) {
    return post(
        [this, autosaveDir, entry, password](TaskContext&) {
            displayCache_->clear();
            textCache_->clear();
            doc_.reset();
            OpenSummary s;
            RecoverResult r = Autosave(std::filesystem::path(autosaveDir.toStdWString()))
                                  .recover(entry, password.toStdString());
            s.ok = r.ok();
            if (s.ok) {
                s.repaired = r.document->repaired();
                s.pdfa = r.document->isPdfA();
                s.isSigned = r.document->isSigned();
                s.hasForm = r.document->hasForm();
                s.hasXfa = r.document->hasXfa();
            } else {
                s.error.message = QString::fromStdString(r.message);
                s.error.hint = r.error == RecoverError::SourceChanged
                                   ? QStringLiteral("recover-source-changed")
                               : r.error == RecoverError::JournalMismatch ? QStringLiteral("recover-mismatch")
                                                                          : QStringLiteral("recover-failed");
            }
            install(std::move(r.document), s, QString::fromStdWString(entry.path.wstring()));
            return s;
        },
        TaskPriority::Urgent, QStringLiteral("recover"));
}

void DocumentSession::install(std::unique_ptr<Document> doc, OpenSummary& s, const QString& path) {
    if (doc) {
        try {
            s.isPdf = doc->isPdf();
            s.pageCount = doc->pageCount();
            s.pageSizes = layoutSizes(*doc, s.pageCount);
            s.pageLayout = QString::fromStdString(props::pageLayout(*doc));
            s.pageLabels = pageLabelsOf(*doc);
            s.initial = initialViewOf(*doc);
            s.permissions = Permissions::of(*doc);
            s.pdfx = doc->isPdfX();
            layersHash_.store(layersHashOf(*doc));
            doc_ = std::shared_ptr<Document>(std::move(doc));
        } catch (const MuError& e) {
            s = OpenSummary{};
            s.error.code = e.code();
            s.error.kind = e.kind();
            s.error.message = QString::fromUtf8(e.what());
            s.error.hint = hintFor(e.kind());
        }
    }
    {
        const std::lock_guard lock(mutex_);
        summary_ = s;
        path_ = s.ok ? path : QString();
        error_ = s.ok ? QString() : s.error.message;
    }
    refreshUndoState();
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
            textCache_->clear();
            doc_.reset();
            {
                const std::lock_guard lock(mutex_);
                summary_ = OpenSummary{};
                path_.clear();
            }
            refreshUndoState();
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

QFuture<std::shared_ptr<const TextPage>> DocumentSession::textPage(int page, const TextOptions& options,
                                                                   TaskPriority priority) {
    return post([page, options](TaskContext& tc) { return tc.textPage(page, options); }, priority,
                QStringLiteral("text-page"));
}

QFuture<void> DocumentSession::exportText(const QString& path, const TextExportOptions& options) {
    return post(
        [path, options](TaskContext& tc) {
            const std::string bytes = ot::exportText(tc.document(), options, [&tc] { return tc.aborted(); });
            paths::writeFile(std::filesystem::path(path.toStdWString()), bytes);
        },
        TaskPriority::Normal, QStringLiteral("export-text"));
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
