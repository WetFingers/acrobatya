// DisplayList и DisplayCache (эталон 4.1–4.2). Display list страницы — единственное, что уходит из потока
// документа в другие потоки: его рисуют потоки RenderPool каждый своим клоном контекста
// (fz_run_display_list со scissor = тайл). DisplayList держит одну ссылку на fz_display_list и отдаёт её
// клоном контекста того потока, где разрушается последняя копия shared_ptr (общие store и замки).
// DisplayCache — LRU списков страниц документа (по умолчанию 40; эталон 4.1), ключ — {страница, с пометками};
// правка страницы сбрасывает её списки (invalidate). Строится и читается только в потоке документа.
#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <utility>

#include "core/util/geometry.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class Document;

class DisplayList {
public:
    // Принимает ссылку list (из fz_new_display_list…). bounds — рамка страницы (fz_bound_page).
    DisplayList(fz_display_list* list, const RectF& bounds, int page, bool annotations) noexcept
        : list_(list), bounds_(bounds), page_(page), annotations_(annotations) {}
    ~DisplayList();
    DisplayList(const DisplayList&) = delete;
    DisplayList& operator=(const DisplayList&) = delete;

    fz_display_list* get() const noexcept { return list_; }
    const RectF& bounds() const noexcept { return bounds_; }
    int page() const noexcept { return page_; }
    bool annotations() const noexcept { return annotations_; }
    // Рамка нарисованного (bbox-устройство по списку) в пунктах ∩ рамка страницы — для «По видимому»;
    // пустая страница — пустой прямоугольник. Любой поток (свой клон контекста).
    RectF contentBounds() const;

    // Построить список страницы в контексте документа (поток документа). cookie: abort = 1 прерывает
    // интерпретацию (MuError, ErrorKind::Aborted) — недостроенный список не возвращается.
    static std::shared_ptr<const DisplayList> build(const Document& doc, int page, bool annotations = true,
                                                    fz_cookie* cookie = nullptr);

private:
    fz_display_list* list_;
    RectF bounds_;
    int page_;
    bool annotations_;
};

class DisplayCache {
public:
    static constexpr std::size_t kDefaultCapacity = 40;

    explicit DisplayCache(std::size_t capacity = kDefaultCapacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    // Список страницы: из кеша (становится самым свежим) или построенный (вытесняет самый старый).
    // Прерванное построение не кешируется (MuError Aborted).
    std::shared_ptr<const DisplayList> get(const Document& doc, int page, bool annotations = true,
                                           fz_cookie* cookie = nullptr);
    // Без построения и без смены порядка; nullptr — нет в кеше.
    std::shared_ptr<const DisplayList> find(int page, bool annotations = true) const;

    // Правка страницы: сбросить оба варианта (с пометками и без).
    void invalidate(int page);
    void clear();

    std::size_t size() const noexcept { return order_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }
    void setCapacity(std::size_t capacity);

    std::size_t hits() const noexcept { return hits_; }
    std::size_t misses() const noexcept { return misses_; }

private:
    using Key = std::pair<int, bool>;  // {страница, с пометками}
    using Entry = std::pair<Key, std::shared_ptr<const DisplayList>>;

    void trim();

    std::size_t capacity_;
    std::list<Entry> order_;  // спереди — самые свежие
    std::map<Key, std::list<Entry>::iterator> index_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};

}  // namespace ot
