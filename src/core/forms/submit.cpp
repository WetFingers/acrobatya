// Отправка формы: план /SubmitForm (см. submit.h).
#include "core/forms/submit.h"

#include <algorithm>
#include <cctype>

#include "core/doc/document.h"

namespace ot::forms {

namespace submit_impl {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string percentDecode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            const int hi = hexValue(s[i + 1]);
            const int lo = hexValue(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(s[i]);
    }
    return out;
}

std::string percentEncode(std::string_view s) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char ch : s) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || c == '-' || c == '.' || c == '_' || c == '~') {
            out.push_back(ch);
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0xF]);
        }
    }
    return out;
}

std::vector<std::string> splitList(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t end = s.find(sep, start);
        const std::string_view part = s.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!part.empty()) {
            out.emplace_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return out;
}

bool hasValue(const FieldValue& v) {
    return std::any_of(v.values.begin(), v.values.end(),
                       [&v](const std::string& s) { return !s.empty() && !(v.state && s == "Off"); });
}

}  // namespace submit_impl

const char* submitFormatName(SubmitFormat format) noexcept {
    switch (format) {
        case SubmitFormat::Fdf:
            return "FDF";
        case SubmitFormat::Xfdf:
            return "XFDF";
        case SubmitFormat::Html:
            return "HTML";
        case SubmitFormat::Pdf:
            break;
    }
    return "PDF";
}

const char* submitContentType(SubmitFormat format) noexcept {
    switch (format) {
        case SubmitFormat::Fdf:
            return "application/vnd.fdf";
        case SubmitFormat::Xfdf:
            return "application/xml";
        case SubmitFormat::Html:
            return "application/x-www-form-urlencoded";
        case SubmitFormat::Pdf:
            break;
    }
    return "application/pdf";
}

const char* submitExtension(SubmitFormat format) noexcept {
    switch (format) {
        case SubmitFormat::Fdf:
            return ".fdf";
        case SubmitFormat::Xfdf:
            return ".xfdf";
        case SubmitFormat::Html:
            return ".txt";
        case SubmitFormat::Pdf:
            break;
    }
    return ".pdf";
}

SubmitFormat submitFormat(int flags) noexcept {
    if ((flags & submit_flags::kSubmitPdf) != 0) {
        return SubmitFormat::Pdf;
    }
    if ((flags & submit_flags::kXfdf) != 0) {
        return SubmitFormat::Xfdf;
    }
    if ((flags & submit_flags::kExportFormat) != 0) {
        return SubmitFormat::Html;
    }
    return SubmitFormat::Fdf;
}

std::optional<MailTo> parseMailto(std::string_view url) {
    using namespace submit_impl;
    if (lower(url.substr(0, 7)) != "mailto:") {
        return std::nullopt;
    }
    url.remove_prefix(7);
    MailTo m;
    const std::size_t q = url.find('?');
    for (const std::string& addr : splitList(url.substr(0, q), ',')) {
        m.to.push_back(percentDecode(addr));
    }
    if (q != std::string_view::npos) {
        for (const std::string& pair : splitList(url.substr(q + 1), '&')) {
            const std::size_t eq = pair.find('=');
            const std::string key = lower(percentDecode(pair.substr(0, eq)));
            const std::string value = eq == std::string::npos ? std::string() : percentDecode(pair.substr(eq + 1));
            if (key == "subject") {
                m.subject = value;
            } else if (key == "body") {
                m.body = value;
            } else if (key == "cc" || key == "bcc" || key == "to") {
                std::vector<std::string>& list = key == "cc" ? m.cc : key == "bcc" ? m.bcc : m.to;
                for (const std::string& a : splitList(value, ',')) {
                    list.push_back(a);
                }
            }
        }
    }
    return m;
}

std::string urlEncodeForm(const std::vector<FieldValue>& fields) {
    std::string out;
    for (const FieldValue& f : fields) {
        for (const std::string& v : f.values) {
            if (!out.empty()) {
                out.push_back('&');
            }
            out += submit_impl::percentEncode(f.name) + "=" + submit_impl::percentEncode(v);
        }
    }
    return out;
}

bool isFdfResponse(std::string_view contentType, std::string_view body) {
    std::string type = submit_impl::lower(contentType);
    if (const std::size_t semi = type.find(';'); semi != std::string::npos) {
        type.resize(semi);
    }
    return type == "application/vnd.fdf" || body.substr(0, 5) == "%FDF-";
}

SubmitPlan planSubmit(const Document& doc, const FieldAction& action, const std::string& documentFile) {
    using namespace submit_impl;
    SubmitPlan p;
    p.url = action.target;
    p.flags = action.flags;
    const std::string scheme = lower(p.url.substr(0, std::min<std::size_t>(p.url.size(), 8)));
    if (scheme.rfind("mailto:", 0) == 0) {
        p.target = SubmitTarget::Mail;
        p.mail = parseMailto(p.url);
    } else if (scheme.rfind("https://", 0) == 0) {
        p.target = SubmitTarget::Https;
    } else if (scheme.rfind("http://", 0) == 0) {
        p.target = SubmitTarget::Http;
    }
    p.format = submitFormat(action.flags);
    p.contentType = submitContentType(p.format);
    p.get = p.format == SubmitFormat::Html && (action.flags & submit_flags::kGetMethod) != 0 &&
            p.target != SubmitTarget::Mail;

    const bool exclude = (action.flags & submit_flags::kExclude) != 0;
    const bool empties = (action.flags & submit_flags::kIncludeNoValueFields) != 0;
    const auto listed = [&action](const std::string& name) {
        return std::any_of(action.fields.begin(), action.fields.end(), [&name](const std::string& n) {
            return name == n || (name.size() > n.size() && name.compare(0, n.size(), n) == 0 && name[n.size()] == '.');
        });
    };
    FormData data = collectData(doc, documentFile, true);
    for (FieldValue& v : data.fields) {
        const bool chosen = action.fields.empty() || (exclude ? !listed(v.name) : listed(v.name));
        if (chosen && (empties || hasValue(v))) {
            p.fields.push_back(std::move(v));
        }
    }
    FormData selected{p.fields, documentFile, {}};
    switch (p.format) {
        case SubmitFormat::Fdf:
            p.body = writeData(selected, DataFormat::Fdf);
            break;
        case SubmitFormat::Xfdf:
            p.body = writeData(selected, DataFormat::Xfdf);
            break;
        case SubmitFormat::Html:
            p.body = urlEncodeForm(p.fields);
            break;
        case SubmitFormat::Pdf:
            break;
    }
    if (p.get) {
        p.url += (p.url.find('?') == std::string::npos ? "?" : "&") + p.body;
        p.body.clear();
    }
    std::string stem = documentFile;
    if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) {
        stem.resize(dot);
    }
    p.attachmentName = (stem.empty() ? std::string("form") : stem) + submitExtension(p.format);
    return p;
}

}  // namespace ot::forms
