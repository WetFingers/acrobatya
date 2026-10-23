#include "core/pages/labels.h"

#include <algorithm>
#include <cctype>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::pages {

namespace {

std::string_view labelTrim(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())) != 0) {
        s.remove_prefix(1);
    }
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())) != 0) {
        s.remove_suffix(1);
    }
    return s;
}

// Без учёта регистра латиницы (римские цифры «IV» = «iv»); остальное — байт в байт.
bool labelEqualsFold(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               const auto ux = static_cast<unsigned char>(x);
               const auto uy = static_cast<unsigned char>(y);
               return ux < 0x80 && uy < 0x80 ? std::tolower(ux) == std::tolower(uy) : x == y;
           });
}

// Целое без знака; не число или вне int — nullopt.
std::optional<int> labelNumber(std::string_view s) {
    if (s.empty() || s.size() > 9) {
        return std::nullopt;
    }
    int n = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        n = n * 10 + (c - '0');
    }
    return n;
}

}  // namespace

std::vector<std::string> pageLabels(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    int has = 0;
    int count = 0;
    mu::call(ctx, [&] {
        has = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/PageLabels") != nullptr;
        count = pdf_count_pages(ctx, pdf);
    });
    if (has == 0 || count <= 0) {
        return {};
    }
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        char buf[128] = {};
        mu::call(ctx, [&] { pdf_page_label(ctx, pdf, i, buf, sizeof(buf)); });
        out.emplace_back(buf);
    }
    return out;
}

std::string pageLabel(const std::vector<std::string>& labels, int page) {
    if (page >= 0 && page < static_cast<int>(labels.size()) &&
        !labels[static_cast<std::size_t>(page)].empty()) {
        return labels[static_cast<std::size_t>(page)];
    }
    return std::to_string(page + 1);
}

std::optional<int> parsePageInput(const std::vector<std::string>& labels, std::string_view input,
                                  int pageCount, bool useLabels) {
    const std::string_view text = labelTrim(input);
    if (text.empty() || pageCount <= 0) {
        return std::nullopt;
    }
    const auto physical = [pageCount](std::optional<int> n) -> std::optional<int> {
        if (n && *n >= 1 && *n <= pageCount) {
            return *n - 1;
        }
        return std::nullopt;
    };
    // «(3)» — физический номер.
    if (text.size() > 2 && text.front() == '(' && text.back() == ')') {
        return physical(labelNumber(labelTrim(text.substr(1, text.size() - 2))));
    }
    if (useLabels && !labels.empty()) {
        const int n = std::min(pageCount, static_cast<int>(labels.size()));
        for (int i = 0; i < n; ++i) {
            if (labels[static_cast<std::size_t>(i)] == text) {
                return i;
            }
        }
        for (int i = 0; i < n; ++i) {
            if (labelEqualsFold(labels[static_cast<std::size_t>(i)], text)) {
                return i;
            }
        }
    }
    return physical(labelNumber(text));
}

}  // namespace ot::pages
