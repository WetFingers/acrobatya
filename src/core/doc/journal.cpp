#include "core/doc/journal.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"

namespace ot {

namespace {

pdf_document* pdfOf(Document& doc) {
    pdf_document* pdf = pdf_document_from_fz_document(doc.context().get(), doc.handle());
    if (pdf == nullptr) {
        throw std::logic_error("journal: not a PDF document");
    }
    return pdf;
}

}  // namespace

// ---------------------------------------------------------------- TouchedPages

void TouchedPages::add(int page) {
    const auto it = std::lower_bound(pages.begin(), pages.end(), page);
    if (it == pages.end() || *it != page) {
        pages.insert(it, page);
    }
}

void TouchedPages::merge(const TouchedPages& other) {
    all = all || other.all;
    for (const int page : other.pages) {
        add(page);
    }
}

// ---------------------------------------------------------------- UndoStack

UndoStack::UndoStack(Document& doc) : doc_(doc), now_([] { return Clock::now(); }) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = pdfOf(doc);
    mu::call(ctx, [&] { pdf_enable_journal(ctx, pdf); });
}

UndoStack::MuState UndoStack::muState() {
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    MuState s;
    // Операция самого MuPDF могла не закрыться при ошибке (begin до fz_try) — тогда история «во время
    // операции» недоступна: откатить незакрытую и спросить снова.
    for (int attempt = 0;; ++attempt) {
        try {
            mu::call(ctx, [&] { s.position = pdf_undoredo_state(ctx, pdf, &s.steps); });
            return s;
        } catch (const MuError& e) {
            if (e.kind() != ErrorKind::Argument || attempt >= 8) {
                throw;
            }
            spdlog::warn("journal: MuPDF left an operation open ({}), abandoning it", e.what());
            mu::call(ctx, [&] { pdf_abandon_operation(ctx, pdf); });
        }
    }
}

bool UndoStack::sync() {
    if (depth_ > 0) {
        return false;  // во время операции история MuPDF недоступна; шаги сверятся после неё
    }
    const MuState s = muState();
    int total = 0;
    int done = 0;
    for (std::size_t i = 0; i < steps_.size(); ++i) {
        total += steps_[i].operations;
        if (static_cast<int>(i) < cursor_) {
            done += steps_[i].operations;
        }
    }
    if (total == s.steps && done == s.position) {
        return false;
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    auto foreign = [&](int index) {
        const char* title = nullptr;
        mu::call(ctx, [&] { title = pdf_undoredo_step(ctx, pdf, index); });
        Step step;
        step.name = title != nullptr ? title : "";
        step.touched.addAll();  // страницы чужой операции неизвестны
        step.last = now_();
        step.sealed = true;
        return step;
    };
    if (s.position == s.steps && s.steps > done) {
        // Операции MuPDF вне нашей (pdf_set_metadata, pdf_create_signature_widget…) поверх сделанных шагов:
        // отменённое ими отброшено, новые — отдельными шагами.
        steps_.resize(static_cast<std::size_t>(cursor_));
        for (int i = done; i < s.steps; ++i) {
            steps_.push_back(foreign(i));
        }
    } else {
        // Журнал сброшен (ремонт) или разошёлся иначе — шаги заново по MuPDF.
        spdlog::warn("journal: history rebuilt from MuPDF ({} of {} steps, was {} of {})", s.position,
                     s.steps, done, total);
        steps_.clear();
        for (int i = 0; i < s.steps; ++i) {
            steps_.push_back(foreign(i));
        }
        cursor_ = s.position;
        return true;
    }
    cursor_ = static_cast<int>(steps_.size());
    return true;
}

void UndoStack::notify() const {
    if (listener_) {
        listener_();
    }
}

bool UndoStack::canUndo() {
    (void)sync();
    return cursor_ > 0;
}

bool UndoStack::canRedo() {
    (void)sync();
    return cursor_ < static_cast<int>(steps_.size());
}

std::string UndoStack::undoName() {
    return canUndo() ? steps_[static_cast<std::size_t>(cursor_ - 1)].name : "";
}

std::string UndoStack::redoName() { return canRedo() ? steps_[static_cast<std::size_t>(cursor_)].name : ""; }

int UndoStack::size() {
    (void)sync();
    return static_cast<int>(steps_.size());
}

int UndoStack::position() {
    (void)sync();
    return cursor_;
}

TouchedPages UndoStack::undo() {
    if (depth_ > 0) {
        throw MuError(FZ_ERROR_ARGUMENT, "cannot undo during an operation");
    }
    (void)sync();
    if (cursor_ == 0) {
        throw MuError(FZ_ERROR_ARGUMENT, "nothing to undo");
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    const Step& step = steps_[static_cast<std::size_t>(cursor_ - 1)];
    const TouchedPages touched = step.touched;
    try {
        for (int i = 0; i < step.operations; ++i) {
            mu::call(ctx, [&] { pdf_undo(ctx, pdf); });
        }
    } catch (...) {
        (void)sync();
        notify();
        throw;
    }
    --cursor_;
    if (cursor_ > 0) {
        steps_[static_cast<std::size_t>(cursor_ - 1)].sealed = true;
    }
    notify();
    return touched;
}

TouchedPages UndoStack::redo() {
    if (depth_ > 0) {
        throw MuError(FZ_ERROR_ARGUMENT, "cannot redo during an operation");
    }
    (void)sync();
    if (cursor_ >= static_cast<int>(steps_.size())) {
        throw MuError(FZ_ERROR_ARGUMENT, "nothing to redo");
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    Step& step = steps_[static_cast<std::size_t>(cursor_)];
    try {
        for (int i = 0; i < step.operations; ++i) {
            mu::call(ctx, [&] { pdf_redo(ctx, pdf); });
        }
    } catch (...) {
        (void)sync();
        notify();
        throw;
    }
    step.sealed = true;
    ++cursor_;
    notify();
    return step.touched;
}

void UndoStack::clear() {
    if (depth_ > 0) {
        throw MuError(FZ_ERROR_ARGUMENT, "cannot clear history during an operation");
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    mu::call(ctx, [&] { pdf_discard_journal(ctx, pdf->journal); });
    const bool had = !steps_.empty();
    steps_.clear();
    cursor_ = 0;
    if (had) {
        notify();
    }
}

bool UndoStack::begin(const std::string& name) {
    if (depth_ > 0) {
        ++depth_;
        return false;
    }
    if (sync()) {
        notify();  // чужие операции до нашей
    }
    before_ = muState();
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    const char* title = name.c_str();
    mu::call(ctx, [&] { pdf_begin_operation(ctx, pdf, title); });
    depth_ = 1;
    name_ = name;
    pending_ = {};
    failed_ = false;
    return true;
}

void UndoStack::commit(bool outer) {
    if (!outer) {
        --depth_;
        return;
    }
    if (failed_) {
        const std::string name = name_;
        abandon(true);
        throw MuError(FZ_ERROR_ARGUMENT, "operation '" + name + "' abandoned: a nested operation failed");
    }
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = pdfOf(doc_);
    depth_ = 0;
    try {
        mu::call(ctx, [&] { pdf_end_operation(ctx, pdf); });
    } catch (...) {
        pending_ = {};
        try {
            if (sync()) {
                notify();
            }
        } catch (const std::exception& e) {
            spdlog::error("journal: after failed end of '{}': {}", name_, e.what());
        }
        throw;
    }
    const MuState after = muState();
    bool changed = false;
    if (after.position == after.steps && after.steps == before_.position + 1) {
        // Новый шаг MuPDF (отменённое после курсора MuPDF отбросил сам).
        steps_.resize(static_cast<std::size_t>(cursor_));
        const Clock::time_point now = now_();
        Step* top = steps_.empty() ? nullptr : &steps_.back();
        if (top != nullptr && window_.count() > 0 && !top->sealed && top->name == name_ &&
            now - top->last <= window_) {
            ++top->operations;
            top->touched.merge(pending_);
            top->last = now;
        } else {
            Step step;
            step.name = name_;
            step.touched = pending_;
            step.last = now;
            steps_.push_back(std::move(step));
        }
        cursor_ = static_cast<int>(steps_.size());
        changed = true;
    } else if (after.position != before_.position || after.steps != before_.steps) {
        changed = sync();  // неожиданное — сверить с MuPDF целиком
    }
    // Иначе правка ничего не изменила: MuPDF выбросил пустую операцию.
    pending_ = {};
    if (changed) {
        notify();
    }
}

void UndoStack::abandon(bool outer) noexcept {
    if (!outer) {
        --depth_;
        failed_ = true;  // вложенную не откатить отдельно — внешняя не зафиксирует полуправку
        return;
    }
    depth_ = 0;
    pending_ = {};
    failed_ = false;
    try {
        fz_context* ctx = doc_.context().get();
        pdf_document* pdf = pdfOf(doc_);
        mu::call(ctx, [&] { pdf_abandon_operation(ctx, pdf); });
        if (sync()) {
            notify();
        }
    } catch (const std::exception& e) {
        spdlog::error("journal: abandoning '{}' failed: {}", name_, e.what());
    }
}

// ---------------------------------------------------------------- Journal::Operation

Journal::Operation::Operation(Document& doc, std::string name)
    : stack_(doc.undoStack()), name_(std::move(name)) {
    outer_ = stack_.begin(name_);
}

Journal::Operation::~Operation() {
    if (!done_) {
        stack_.abandon(outer_);
    }
}

void Journal::Operation::invalidate(int page) { stack_.touch(page); }

void Journal::Operation::invalidateAll() { stack_.touchAll(); }

void Journal::Operation::commit() {
    if (done_) {
        throw std::logic_error("journal: operation '" + name_ + "' already finished");
    }
    done_ = true;
    stack_.commit(outer_);
}

}  // namespace ot
