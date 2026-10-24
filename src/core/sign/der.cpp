#include "core/sign/der.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <string>

namespace ot::sign::der {

namespace {

// Разбор заголовка в data[pos…]: тег, длина (или -1 — неопределённая), длина заголовка.
struct Header {
    std::uint8_t tag = 0;
    std::int64_t length = 0;
    std::size_t headerSize = 0;
};

Header readHeader(std::span<const std::uint8_t> data, std::size_t pos) {
    if (pos + 2 > data.size()) {
        throw DerError("DER: truncated header");
    }
    Header h;
    h.tag = data[pos];
    if ((h.tag & 0x1f) == 0x1f) {
        throw DerError("DER: multi-byte tags are not supported");
    }
    const std::uint8_t first = data[pos + 1];
    h.headerSize = 2;
    if (first < 0x80) {
        h.length = first;
    } else if (first == 0x80) {
        if ((h.tag & 0x20) == 0) {
            throw DerError("DER: indefinite length of a primitive element");
        }
        h.length = -1;
    } else {
        const std::size_t n = first & 0x7f;
        if (n > 4 || pos + 2 + n > data.size()) {
            throw DerError("DER: bad length");
        }
        std::int64_t len = 0;
        for (std::size_t i = 0; i < n; ++i) {
            len = (len << 8) | data[pos + 2 + i];
        }
        h.length = len;
        h.headerSize = 2 + n;
    }
    return h;
}

// Конец элемента, начинающегося в pos (индекс за последним октетом), и границы содержимого.
std::size_t elementEnd(std::span<const std::uint8_t> data, std::size_t pos, std::size_t* valueBegin,
                       std::size_t* valueEnd, int depth) {
    if (depth > 64) {
        throw DerError("DER: nesting too deep");
    }
    const Header h = readHeader(data, pos);
    const std::size_t begin = pos + h.headerSize;
    if (h.length >= 0) {
        const auto len = static_cast<std::size_t>(h.length);
        if (begin + len > data.size()) {
            throw DerError("DER: element exceeds data");
        }
        *valueBegin = begin;
        *valueEnd = begin + len;
        return begin + len;
    }
    // Неопределённая длина: дочерние элементы до 00 00.
    std::size_t p = begin;
    for (;;) {
        if (p + 2 > data.size()) {
            throw DerError("DER: missing end-of-contents");
        }
        if (data[p] == 0 && data[p + 1] == 0) {
            *valueBegin = begin;
            *valueEnd = p;
            return p + 2;
        }
        std::size_t vb = 0;
        std::size_t ve = 0;
        p = elementEnd(data, p, &vb, &ve, depth + 1);
    }
}

}  // namespace

Node Reader::next() {
    if (atEnd()) {
        throw DerError("DER: unexpected end of data");
    }
    std::size_t vb = 0;
    std::size_t ve = 0;
    const std::size_t end = elementEnd(data_, pos_, &vb, &ve, 0);
    Node n;
    n.tag = data_[pos_];
    n.value = data_.subspan(vb, ve - vb);
    n.encoded = data_.subspan(pos_, end - pos_);
    pos_ = end;
    return n;
}

Node Reader::expect(std::uint8_t tag, const char* what) {
    if (atEnd() || data_[pos_] != tag) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s: expected tag 0x%02X, got 0x%02X", what, tag,
                      atEnd() ? 0 : data_[pos_]);
        throw DerError(buf);
    }
    return next();
}

bool Reader::nextIf(std::uint8_t tag, Node& out) {
    if (atEnd() || data_[pos_] != tag) {
        return false;
    }
    out = next();
    return true;
}

std::string toOid(std::span<const std::uint8_t> value) {
    if (value.empty()) {
        throw DerError("DER: empty OID");
    }
    std::string out;
    std::uint64_t acc = 0;
    bool first = true;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (acc > (UINT64_MAX >> 7)) {
            throw DerError("DER: OID arc too large");
        }
        acc = (acc << 7) | (value[i] & 0x7f);
        if ((value[i] & 0x80) != 0) {
            if (i + 1 == value.size()) {
                throw DerError("DER: truncated OID");
            }
            continue;
        }
        if (first) {
            const std::uint64_t a = acc < 40 ? 0 : (acc < 80 ? 1 : 2);
            out = std::to_string(a) + "." + std::to_string(acc - a * 40);
            first = false;
        } else {
            out += "." + std::to_string(acc);
        }
        acc = 0;
    }
    return out;
}

std::int64_t toInt(const Node& node) {
    if (node.tag != kInteger && node.tag != 0x0a /* ENUMERATED */) {
        throw DerError("DER: not an INTEGER");
    }
    if (node.value.empty() || node.value.size() > 8) {
        throw DerError("DER: INTEGER out of range");
    }
    std::int64_t v = (node.value[0] & 0x80) != 0 ? -1 : 0;
    for (const std::uint8_t b : node.value) {
        v = static_cast<std::int64_t>((static_cast<std::uint64_t>(v) << 8) | b);
    }
    return v;
}

std::string toTime(std::uint8_t tag, std::span<const std::uint8_t> value) {
    const std::string s(value.begin(), value.end());
    std::string year;
    std::size_t rest = 0;
    if (tag == kUtcTime) {
        if (s.size() < 11) {
            throw DerError("DER: bad UTCTime");
        }
        const int yy = std::stoi(s.substr(0, 2));
        year = std::to_string(yy < 50 ? 2000 + yy : 1900 + yy);
        rest = 2;
    } else if (tag == kGeneralizedTime) {
        if (s.size() < 13) {
            throw DerError("DER: bad GeneralizedTime");
        }
        year = s.substr(0, 4);
        rest = 4;
    } else {
        throw DerError("DER: not a time");
    }
    for (std::size_t i = 0; i < rest + 8 && i < s.size(); ++i) {
        if (s[i] < '0' || s[i] > '9') {
            throw DerError("DER: bad time digits");
        }
    }
    const std::string mm = s.substr(rest, 2);
    const std::string dd = s.substr(rest + 2, 2);
    const std::string hh = s.substr(rest + 4, 2);
    const std::string mi = s.substr(rest + 6, 2);
    std::string ss = "00";
    if (s.size() >= rest + 10 && s[rest + 8] >= '0' && s[rest + 8] <= '9') {
        ss = s.substr(rest + 8, 2);
    }
    return year + "-" + mm + "-" + dd + "T" + hh + ":" + mi + ":" + ss + "Z";
}

// --- запись DER

Encoded encode(std::uint8_t tag, std::span<const std::uint8_t> content) {
    Encoded out;
    out.reserve(content.size() + 6);
    out.push_back(tag);
    const std::size_t n = content.size();
    if (n < 0x80) {
        out.push_back(static_cast<std::uint8_t>(n));
    } else {
        int bytes = 0;
        for (std::size_t v = n; v != 0; v >>= 8) {
            ++bytes;
        }
        if (bytes > 4) {
            throw DerError("DER: element too long");
        }
        out.push_back(static_cast<std::uint8_t>(0x80 | bytes));
        for (int i = bytes - 1; i >= 0; --i) {
            out.push_back(static_cast<std::uint8_t>((n >> (8 * i)) & 0xff));
        }
    }
    out.insert(out.end(), content.begin(), content.end());
    return out;
}

Encoded join(std::initializer_list<std::span<const std::uint8_t>> parts) {
    Encoded out;
    for (const auto& p : parts) {
        out.insert(out.end(), p.begin(), p.end());
    }
    return out;
}

Encoded encodeSetOf(std::vector<Encoded> elements, std::uint8_t tag) {
    // Как der_cmp OpenSSL: общий префикс, затем короче — раньше.
    std::sort(elements.begin(), elements.end(),
              [](const Encoded& a, const Encoded& b) { return std::ranges::lexicographical_compare(a, b); });
    Encoded content;
    for (const Encoded& e : elements) {
        content.insert(content.end(), e.begin(), e.end());
    }
    return encode(tag, content);
}

Encoded encodeOid(std::string_view dotted) {
    std::vector<std::uint64_t> arcs;
    std::size_t pos = 0;
    while (pos <= dotted.size()) {
        const std::size_t dot = std::min(dotted.find('.', pos), dotted.size());
        std::uint64_t v = 0;
        const char* first = dotted.data() + pos;
        const char* last = dotted.data() + dot;
        const auto [ptr, ec] = std::from_chars(first, last, v);
        if (first == last || ec != std::errc() || ptr != last) {
            throw DerError("DER: bad OID " + std::string(dotted));
        }
        arcs.push_back(v);
        pos = dot + 1;
    }
    if (arcs.size() < 2 || arcs[0] > 2 || (arcs[0] < 2 && arcs[1] >= 40)) {
        throw DerError("DER: bad OID " + std::string(dotted));
    }
    Encoded content;
    auto put = [&](std::uint64_t v) {
        std::uint8_t tmp[10];
        int n = 0;
        do {
            tmp[n++] = static_cast<std::uint8_t>(v & 0x7f);
            v >>= 7;
        } while (v != 0);
        for (int i = n - 1; i >= 0; --i) {
            content.push_back(static_cast<std::uint8_t>(tmp[i] | (i != 0 ? 0x80 : 0)));
        }
    };
    put(arcs[0] * 40 + arcs[1]);
    for (std::size_t i = 2; i < arcs.size(); ++i) {
        put(arcs[i]);
    }
    return encode(kOid, content);
}

}  // namespace ot::sign::der
