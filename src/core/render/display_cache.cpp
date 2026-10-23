#include "core/render/display_cache.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

namespace ot {

DisplayList::DisplayList(std::unique_ptr<MuContext> owner, fz_display_list* list, const RectF& bounds,
                         int page, bool annotations) noexcept
    : owner_(std::move(owner)),
      family_(owner_ ? owner_->family() : nullptr),
      list_(list),
      bounds_(bounds),
      page_(page),
      annotations_(annotations) {}

DisplayList::~DisplayList() {
    if (list_ != nullptr && owner_) {
        // Последняя ссылка умирает в каком-то одном потоке (пул, GUI, поток документа): свой клон — только
        // здесь, store и замки — общие с семьёй документа.
        fz_drop_display_list(owner_->get(), list_);
    }
}

RectF DisplayList::contentBounds() const {
    // Клон этого потока, если он той же семьи; иначе — временный клон семьи документа.
    std::unique_ptr<MuContext> temporary;
    MuContext* local = &MuContext::forThisThread();
    if (local->family() != family_) {
        temporary = owner_->clone();
        local = temporary.get();
    }
    fz_context* ctx = local->get();
    mu::DeviceHandle dev(ctx);
    fz_rect box = fz_empty_rect;
    mu::call(ctx, [&] {
        dev.reset(fz_new_bbox_device(ctx, &box));
        fz_run_display_list(ctx, list_, dev, fz_identity, fz_infinite_rect, nullptr);
        fz_close_device(ctx, dev);
    });
    dev.reset();
    const fz_rect page = fz_make_rect(bounds_.x0, bounds_.y0, bounds_.x1, bounds_.y1);
    box = fz_intersect_rect(box, page);
    if (fz_is_empty_rect(box)) {
        return {};
    }
    return {box.x0, box.y0, box.x1, box.y1};
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
    auto owner = doc.context().clone();
    return std::make_shared<const DisplayList>(std::move(owner), list.release(),
                                               RectF{bounds.x0, bounds.y0, bounds.x1, bounds.y1}, page,
                                               annotations);
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
