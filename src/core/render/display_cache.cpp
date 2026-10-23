#include "core/render/display_cache.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

namespace ot {

DisplayList::~DisplayList() {
    if (list_ != nullptr) {
        // Последняя ссылка может умереть в любом потоке (пул, GUI): отдаём клоном этого потока — store,
        // аллокатор и замки у клонов общие.
        fz_drop_display_list(MuContext::forThisThread().get(), list_);
    }
}

std::shared_ptr<const DisplayList> DisplayList::build(const Document& doc, int page, bool annotations,
                                                      fz_cookie* cookie) {
    fz_context* ctx = doc.context().get();
    mu::PageHandle p(ctx);
    mu::DisplayListHandle list(ctx);
    mu::DeviceHandle dev(ctx);
    fz_rect bounds{};
    // Как fz_new_display_list_from_page(_contents), но с cookie: отмена прерывает интерпретацию.
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        bounds = fz_bound_page(ctx, p);
        list.reset(fz_new_display_list(ctx, bounds));
        dev.reset(fz_new_list_device(ctx, list));
        if (annotations) {
            fz_run_page(ctx, p, dev, fz_identity, cookie);
        } else {
            fz_run_page_contents(ctx, p, dev, fz_identity, cookie);
        }
        fz_close_device(ctx, dev);
    });
    if (cookie != nullptr && cookie->abort != 0) {
        throw MuError(FZ_ERROR_ABORT, "display list aborted");
    }
    dev.reset();
    return std::make_shared<const DisplayList>(
        list.release(), RectF{bounds.x0, bounds.y0, bounds.x1, bounds.y1}, page, annotations);
}

std::shared_ptr<const DisplayList> DisplayCache::get(const Document& doc, int page, bool annotations,
                                                     fz_cookie* cookie) {
    const Key key{page, annotations};
    if (const auto it = index_.find(key); it != index_.end()) {
        order_.splice(order_.begin(), order_, it->second);
        ++hits_;
        return it->second->second;
    }
    ++misses_;
    auto list = DisplayList::build(doc, page, annotations, cookie);
    order_.emplace_front(key, list);
    index_[key] = order_.begin();
    trim();
    return list;
}

std::shared_ptr<const DisplayList> DisplayCache::find(int page, bool annotations) const {
    const auto it = index_.find(Key{page, annotations});
    return it == index_.end() ? nullptr : it->second->second;
}

void DisplayCache::invalidate(int page) {
    for (const bool annotations : {true, false}) {
        if (const auto it = index_.find(Key{page, annotations}); it != index_.end()) {
            order_.erase(it->second);
            index_.erase(it);
        }
    }
}

void DisplayCache::clear() {
    index_.clear();
    order_.clear();
}

void DisplayCache::setCapacity(std::size_t capacity) {
    capacity_ = capacity == 0 ? 1 : capacity;
    trim();
}

void DisplayCache::trim() {
    while (order_.size() > capacity_) {
        index_.erase(order_.back().first);
        order_.pop_back();
    }
}

}  // namespace ot
