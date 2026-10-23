#include "core/mu/context.h"

#include <mutex>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "core/mu/call.h"

namespace ot {

namespace {

// Колбэки MuPDF вызываются изнутри fz_try/fz_catch: исключение C++ отсюда пересекло бы кадр setjmp.
void logToSpdlog(spdlog::level::level_enum level, const char* message) noexcept {
    try {
        spdlog::default_logger_raw()->log(level, "mupdf: {}", message != nullptr ? message : "");
    } catch (...) {  // NOLINT(bugprone-empty-catch): журнал недоступен — молчим, но не бросаем
    }
}

void onMuWarning(void* /*user*/, const char* message) { logToSpdlog(spdlog::level::warn, message); }
void onMuError(void* /*user*/, const char* message) { logToSpdlog(spdlog::level::err, message); }

}  // namespace

struct MuContext::Locks {
    std::mutex mutexes[FZ_LOCK_MAX];
    fz_locks_context api{};

    Locks() {
        api.user = this;
        api.lock = [](void* user, int lock) { static_cast<Locks*>(user)->mutexes[lock].lock(); };
        api.unlock = [](void* user, int lock) { static_cast<Locks*>(user)->mutexes[lock].unlock(); };
    }
};

MuContext::MuContext() : locks_(std::make_shared<Locks>()) {
    // Реестр spdlog создаётся раньше, чем завершится конструктор global(), — значит, разрушится позже
    // него: сброс предупреждений в ~MuContext при выходе из процесса пишет в живой журнал.
    (void)spdlog::default_logger_raw();
    ctx_ = fz_new_context(nullptr, &locks_->api, FZ_STORE_DEFAULT);
    if (ctx_ == nullptr) {
        throw std::runtime_error("fz_new_context failed");
    }
    fz_set_warning_callback(ctx_, onMuWarning, nullptr);
    fz_set_error_callback(ctx_, onMuError, nullptr);
    try {
        mu::call(ctx_, [&] { fz_register_document_handlers(ctx_); });
    } catch (...) {
        fz_drop_context(ctx_);
        ctx_ = nullptr;
        throw;
    }
}

MuContext::MuContext(fz_context* ctx, std::shared_ptr<Locks> locks) : locks_(std::move(locks)), ctx_(ctx) {}

MuContext::~MuContext() {
    if (ctx_ != nullptr) {
        // Повторы последнего предупреждения — в журнал, затем колбэки снимаются: drop в журнал не пишет.
        fz_flush_warnings(ctx_);
        fz_set_warning_callback(ctx_, nullptr, nullptr);
        fz_set_error_callback(ctx_, nullptr, nullptr);
        fz_drop_context(ctx_);
    }
}

MuContext& MuContext::global() {
    static MuContext instance;
    return instance;
}

std::unique_ptr<MuContext> MuContext::clone() const {
    fz_context* cloned = fz_clone_context(ctx_);
    if (cloned == nullptr) {
        throw std::runtime_error("fz_clone_context failed");
    }
    return std::unique_ptr<MuContext>(new MuContext(cloned, locks_));
}

std::string MuContext::version() { return FZ_VERSION; }

}  // namespace ot
