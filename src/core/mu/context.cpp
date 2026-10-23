#include "core/mu/context.h"

#include <mutex>
#include <stdexcept>

#include "core/mu/call.h"

namespace ot {

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
    ctx_ = fz_new_context(nullptr, &locks_->api, FZ_STORE_DEFAULT);
    if (ctx_ == nullptr) {
        throw std::runtime_error("fz_new_context failed");
    }
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
        fz_drop_context(ctx_);
    }
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
