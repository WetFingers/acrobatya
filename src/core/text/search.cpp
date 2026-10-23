#include "core/text/search.h"

#include <map>

#include <spdlog/spdlog.h>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/text/stext.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

namespace {

using SearchHandle = mu::Handle<fz_search, fz_drop_search>;
using SearchClock = std::chrono::steady_clock;

QuadF searchQuadOf(const fz_quad& q) {
    return {{q.ul.x, q.ul.y}, {q.ur.x, q.ur.y}, {q.ll.x, q.ll.y}, {q.lr.x, q.lr.y}};
}

// Совпадение — целые слова: по краям внутри строки нет букв, цифр и меток (край из не-буквы — всегда
// граница).
bool isWholeWord(const TextPage* begin, int first, const TextPage* end, int last) {
    if (begin == nullptr || end == nullptr || first < 0 || last < 0) {
        return true;  // символа не нашли (синтетический пробел) — не отбрасывать
    }
    const auto& b = begin->chars();
    if (first > 0 && b[first - 1].line == b[first].line && TextPage::isWordChar(b[first - 1].c) &&
        TextPage::isWordChar(b[first].c)) {
        return false;
    }
    const auto& e = end->chars();
    if (last + 1 < static_cast<int>(e.size()) && e[last + 1].line == e[last].line &&
        TextPage::isWordChar(e[last + 1].c) && TextPage::isWordChar(e[last].c)) {
        return false;
    }
    return true;
}

using HitFn = std::function<void(SearchHit&&)>;
// Страница подана (номер): false — остановить поиск.
using FedFn = std::function<bool(int)>;

// Поиск по страницам [first, last): страницы подаются по запросу MuPDF. false — остановлен onFed.
bool runSearch(std::string_view needleView, const SearchOptions& options, int first, int last,
               const SearchSession::PageSource& source, const HitFn& onHit, const FedFn& onFed) {
    if (needleView.empty() || first >= last) {
        return true;
    }
    std::shared_ptr<const TextPage> start = source(first);
    // Контекст — семьи документа: её даёт любой слой; нет слоя у первой страницы — корень процесса.
    std::unique_ptr<MuContext> temporary;
    fz_context* ctx = start ? start->contextForThisThread(temporary) : MuContext::forThisThread().get();
    const std::string needle(needleView);
    const auto flags = static_cast<fz_search_options>(searchFlags(options));
    SearchHandle search(ctx);
    mu::call(ctx, [&] { search.reset(fz_new_search(ctx, needle.c_str(), flags)); });

    // Слои последних трёх поданных страниц: совпадение начинается на одной из них.
    std::map<int, std::shared_ptr<const TextPage>> alive;
    auto feed = [&](int seq, std::shared_ptr<const TextPage> page, bool endOfDocument) {
        fz_stext_page* text = page ? page->stext() : nullptr;
        mu::call(ctx, [&] {
            if (endOfDocument) {
                fz_feed_search(ctx, search, nullptr, seq);
            } else if (text != nullptr) {
                fz_feed_search(ctx, search, fz_keep_stext_page(ctx, text), seq);
            } else {
                fz_feed_search(ctx, search, fz_new_stext_page(ctx, fz_empty_rect), seq);  // нет слоя — пусто
            }
        });
        if (page) {
            alive[seq] = std::move(page);
            while (!alive.empty() && alive.begin()->first < seq - 2) {
                alive.erase(alive.begin());
            }
        }
    };
    auto pageOf = [&](int seq) -> const TextPage* {
        const auto it = alive.find(seq);
        return it == alive.end() ? nullptr : it->second.get();
    };

    feed(first, std::move(start), false);
    if (!onFed(first)) {
        return false;
    }
    fz_search_result result{};
    for (;;) {
        mu::call(ctx, [&] { result = fz_search_forwards(ctx, search); });
        if (result.reason == FZ_SEARCH_COMPLETE) {
            return true;
        }
        if (result.reason == FZ_SEARCH_MORE_INPUT) {
            const int seq = result.u.seq_needed;
            if (seq < first || seq >= last) {
                feed(seq, nullptr, true);
                continue;
            }
            feed(seq, source(seq), false);
            if (!onFed(seq)) {
                return false;
            }
            continue;
        }
        const fz_search_match* m = result.u.match;
        SearchHit hit;
        hit.page = m->begin_seq;
        hit.endPage = m->end_seq;
        const TextPage* bp = pageOf(m->begin_seq);
        const TextPage* ep = pageOf(m->end_seq);
        hit.firstChar = bp != nullptr ? bp->charIndexOf(m->begin.ch) : -1;
        hit.lastChar = ep != nullptr ? ep->charIndexOf(m->end.ch) : -1;
        hit.quads.reserve(static_cast<std::size_t>(m->num_quads));
        for (int i = 0; i < m->num_quads; ++i) {
            hit.quads.push_back({m->quads[i].seq, searchQuadOf(m->quads[i].quad)});
        }
        if (options.wholeWord && !isWholeWord(bp, hit.firstChar, ep, hit.lastChar)) {
            continue;
        }
        onHit(std::move(hit));
    }
}

}  // namespace

int searchFlags(const SearchOptions& options) noexcept {
    int flags = FZ_SEARCH_EXACT;
    if (!options.caseSensitive) {
        flags |= FZ_SEARCH_IGNORE_CASE;
    }
    if (options.ignoreDiacritics) {
        flags |= FZ_SEARCH_IGNORE_DIACRITICS;
    }
    if (options.regex) {
        flags |= FZ_SEARCH_REGEXP;
    }
    return flags;
}

std::vector<SearchHit> searchPage(const TextPage& page, std::string_view needle,
                                  const SearchOptions& options) {
    std::vector<SearchHit> hits;
    // Слой не владеет собой через shared_ptr: подаём его «псевдовладельцем» — страница живёт дольше поиска.
    const std::shared_ptr<const TextPage> alias(std::shared_ptr<const TextPage>{}, &page);
    runSearch(
        needle, options, page.page(), page.page() + 1, [&](int) { return alias; },
        [&](SearchHit&& h) { hits.push_back(std::move(h)); }, [](int) { return true; });
    return hits;
}

SearchSession::SearchSession(std::string needle, SearchOptions options)
    : needle_(std::move(needle)), options_(options) {}

void SearchSession::run(int pageCount, const PageSource& source, const ProgressFn& progress,
                        const CancelFn& cancel) {
    hits_.clear();
    current_ = -1;
    pagesDone_ = 0;
    finished_ = false;
    aborted_ = false;
    auto last = SearchClock::now();
    auto report = [&](bool done) {
        if (progress) {
            progress({pagesDone_, pageCount, count(), done});
        }
    };
    // Ошибка слоя одной страницы — пропуск страницы; отмена — наверх.
    auto layer = [&](int page) -> std::shared_ptr<const TextPage> {
        try {
            return source(page);
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted) {
                throw;
            }
            spdlog::warn("search: page {} skipped: {}", page, e.what());
            return nullptr;
        }
    };
    bool complete = false;
    try {
        complete = runSearch(
            needle_, options_, 0, pageCount, layer, [&](SearchHit&& h) { hits_.push_back(std::move(h)); },
            [&](int seq) {
                pagesDone_ = seq + 1;
                if (cancel && cancel()) {
                    return false;
                }
                if (const auto now = SearchClock::now(); now - last >= kProgressInterval) {
                    last = now;
                    report(false);
                }
                return true;
            });
    } catch (const MuError& e) {
        if (e.kind() != ErrorKind::Aborted) {
            throw;
        }
    }
    if (complete) {
        finished_ = true;
        pagesDone_ = pageCount;
    } else {
        aborted_ = true;
    }
    report(true);
}

std::vector<int> SearchSession::hitsOnPage(int page) const {
    std::vector<int> out;
    for (int i = 0; i < count(); ++i) {
        for (const SearchQuad& q : hits_[i].quads) {
            if (q.page == page) {
                out.push_back(i);
                break;
            }
        }
    }
    return out;
}

const SearchHit* SearchSession::currentHit() const noexcept {
    return current_ >= 0 && current_ < count() ? &hits_[current_] : nullptr;
}

int SearchSession::next() noexcept {
    if (hits_.empty()) {
        return current_ = -1;
    }
    current_ = (current_ + 1) % count();
    return current_;
}

int SearchSession::previous() noexcept {
    if (hits_.empty()) {
        return current_ = -1;
    }
    current_ = current_ <= 0 ? count() - 1 : current_ - 1;
    return current_;
}

int SearchSession::selectFrom(int page) noexcept {
    if (hits_.empty()) {
        return current_ = -1;
    }
    current_ = 0;
    for (int i = 0; i < count(); ++i) {
        if (hits_[i].page >= page) {
            current_ = i;
            break;
        }
    }
    return current_;
}

}  // namespace ot
