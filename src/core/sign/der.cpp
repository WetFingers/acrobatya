#include "core/sign/der.h"

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

}  // namespace ot::sign::der
