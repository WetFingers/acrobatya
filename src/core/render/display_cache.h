// DisplayList и DisplayCache (эталон 4.1–4.2). Display list страницы — единственное, что уходит из потока
// документа в другие потоки: его рисуют потоки RenderPool каждый своим клоном контекста той же семьи
// (MuContext::family(): общие store и замки; fz_run_display_list со scissor = тайл). DisplayList держит одну
// ссылку на fz_display_list и свой клон контекста документа — им список отдаётся там, где разрушается
// последняя копия shared_ptr (в любом потоке).
// DisplayCache — LRU списков страниц документа (по умолчанию 40; эталон 4.1), ключ — {страница, с пометками};
// правка страницы сбрасывает её списки (invalidate). Строится и читается только в потоке документа.
#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <memory>
#include <tuple>
#include <utility>

#include "core/util/geometry.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class Document;
class MuContext;

class DisplayList {
public:
    // Принимает ссылку list (из fz_new_display_list…) и owner — клон контекста документа (только для отдачи
    // списка и для contentBounds в чужом потоке). bounds — рамка страницы (fz_bound_page).
    DisplayList(std::unique_ptr<MuContext> owner, fz_display_list* list, const RectF& bounds, int page,
                bool annotations, bool usesOverprint = false, std::uint32_t annotsHidden = 0) noexcept;
    ~DisplayList();
    DisplayList(const DisplayList&) = delete;
    DisplayList& operator=(const DisplayList&) = delete;

    fz_display_list* get() const noexcept { return list_; }
    const RectF& bounds() const noexcept { return bounds_; }
    int page() const noexcept { return page_; }
    bool annotations() const noexcept { return annotations_; }
    // Скрытые подтипы аннотаций (бит i — AnnotType i, 5.1 «Показать/скрыть комментарии»): их нет в списке.
    std::uint32_t annotsHidden() const noexcept { return annotsHidden_; }
    // Страница рисует с overprint (fz_page_uses_overprint: /OP, /op в ExtGState ресурсов) — симуляции
    // наложения (RenderStyle::overprint) есть что показать; иначе она не включается (дешевле).
    bool usesOverprint() const noexcept { return usesOverprint_; }
    // Семья контекстов документа: рисовать список можно только её контекстами (RenderPool проверяет).
    const void* family() const noexcept { return family_; }
    // Рамка нарисованного (bbox-устройство по списку) в пунктах ∩ рамка страницы — для «По видимому»;
    // пустая страница — пустой прямоугольник. Любой поток (свой клон контекста).
    RectF contentBounds() const;

    // Построить список страницы в контексте документа (поток документа). cookie: abort = 1 прерывает
    // интерпретацию (MuError, ErrorKind::Aborted) — недостроенный список не возвращается.
    // annotsHidden ≠ 0 — содержимое, затем аннотации (pdf_run_annot) без скрытых подтипов, затем поля.
    static std::shared_ptr<const DisplayList> build(const Document& doc, int page, bool annotations = true,
                                                    fz_cookie* cookie = nullptr,
                                                    std::uint32_t annotsHidden = 0);

private:
    std::unique_ptr<MuContext> owner_;
    const void* family_;
    fz_display_list* list_;
    RectF bounds_;
    int page_;
    bool annotations_;
    bool usesOverprint_;
    std::uint32_t annotsHidden_;
};

class DisplayCache {
public:
    static constexpr std::size_t kDefaultCapacity = 40;

    explicit DisplayCache(std::size_t capacity = kDefaultCapacity)
        : capacity_(capacity == 0 ? 1 : capacity) {}

    // Список страницы: из кеша (становится самым свежим) или построенный (вытесняет самый старый).
    // Прерванное построение не кешируется (MuError Aborted).
    std::shared_ptr<const DisplayList> get(const Document& doc, int page, bool annotations = true,
                                           fz_cookie* cookie = nullptr, std::uint32_t annotsHidden = 0);
    // Без построения и без смены порядка; nullptr — нет в кеше.
    std::shared_ptr<const DisplayList> find(int page, bool annotations = true,
                                            std::uint32_t annotsHidden = 0) const;

    // Правка страницы: сбросить оба варианта (с пометками и без).
    void invalidate(int page);
    void clear();

    std::size_t size() const noexcept { return order_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }
    void setCapacity(std::size_t capacity);

    std::size_t hits() const noexcept { return hits_; }
    std::size_t misses() const noexcept { return misses_; }

private:
    using Key = std::tuple<int, bool, std::uint32_t>;  // {страница, с пометками, скрытые подтипы}
    using Entry = std::pair<Key, std::shared_ptr<const DisplayList>>;

    void trim();

    std::size_t capacity_;
    std::list<Entry> order_;  // спереди — самые свежие
    std::map<Key, std::list<Entry>::iterator> index_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};

}  // namespace ot
