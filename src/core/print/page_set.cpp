#include "core/print/page_set.h"

#include <algorithm>
#include <cstddef>

#include "core/pages/labels.h"

namespace ot::print {

namespace {

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

void appendRun(std::vector<int>& out, int from, int to) {
    if (from <= to) {
        for (int i = from; i <= to; ++i) {
            out.push_back(i);
        }
    } else {
        for (int i = from; i >= to; --i) {
            out.push_back(i);
        }
    }
}

// Одна часть диапазона → страницы; false — не разобрана.
bool parsePart(const std::vector<std::string>& labels, std::string_view part, int pageCount, bool useLabels,
               std::vector<int>& out) {
    // Целиком — одна страница (подписи с дефисом «A-1»).
    if (const auto one = pages::parsePageInput(labels, part, pageCount, useLabels)) {
        out.push_back(*one);
        return true;
    }
    for (std::size_t dash = part.find('-'); dash != std::string_view::npos; dash = part.find('-', dash + 1)) {
        const std::string_view left = trim(part.substr(0, dash));
        const std::string_view right = trim(part.substr(dash + 1));
        if (left.empty() && right.empty()) {
            continue;
        }
        const std::optional<int> from =
            left.empty() ? std::optional<int>(0) : pages::parsePageInput(labels, left, pageCount, useLabels);
        const std::optional<int> to = right.empty()
                                          ? std::optional<int>(pageCount - 1)
                                          : pages::parsePageInput(labels, right, pageCount, useLabels);
        if (from && to) {
            appendRun(out, *from, *to);
            return true;
        }
    }
    return false;
}

std::string labelOf(int page, const std::vector<std::string>& labels, bool useLabels) {
    if (useLabels && !labels.empty()) {
        return pages::pageLabel(labels, page);
    }
    return std::to_string(page + 1);
}

}  // namespace

std::optional<std::vector<int>> parsePageRange(const std::vector<std::string>& labels, std::string_view text,
                                               int pageCount, bool useLabels) {
    if (pageCount <= 0) {
        return std::nullopt;
    }
    std::vector<int> out;
    bool any = false;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of(",;", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string_view part = trim(text.substr(start, end - start));
        if (!part.empty()) {
            if (!parsePart(labels, part, pageCount, useLabels, out)) {
                return std::nullopt;
            }
            any = true;
        }
        start = end + 1;
    }
    if (!any) {
        return std::nullopt;
    }
    return out;
}

std::optional<std::vector<int>> resolvePages(const PageSetOptions& options, int pageCount,
                                             const std::vector<std::string>& labels) {
    std::vector<int> pages;
    switch (options.range) {
        case PageRange::All:
            appendRun(pages, 0, pageCount - 1);
            break;
        case PageRange::Current:
            if (options.current >= 0 && options.current < pageCount) {
                pages.push_back(options.current);
            }
            break;
        case PageRange::Custom: {
            auto parsed = parsePageRange(labels, options.custom, pageCount, options.useLabels);
            if (!parsed) {
                return std::nullopt;
            }
            pages = std::move(*parsed);
            break;
        }
        case PageRange::Selected:
            for (const int p : options.selected) {
                if (p >= 0 && p < pageCount) {
                    pages.push_back(p);
                }
            }
            break;
    }
    if (pageCount <= 0) {
        pages.clear();
    }
    if (options.parity != PageParity::Both) {
        const int keep = options.parity == PageParity::Odd ? 0 : 1;  // с нуля: нечётные — чётные индексы
        std::erase_if(pages, [keep](int p) { return p % 2 != keep; });
    }
    if (options.reverse) {
        std::reverse(pages.begin(), pages.end());
    }
    return pages;
}

std::string formatPageRange(const std::vector<int>& pages, const std::vector<std::string>& labels,
                            bool useLabels) {
    std::string out;
    std::size_t i = 0;
    while (i < pages.size()) {
        std::size_t j = i;
        while (j + 1 < pages.size() && pages[j + 1] == pages[j] + 1) {
            ++j;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += labelOf(pages[i], labels, useLabels);
        if (j > i) {
            out += '-';
            out += labelOf(pages[j], labels, useLabels);
        }
        i = j + 1;
    }
    return out;
}

}  // namespace ot::print
