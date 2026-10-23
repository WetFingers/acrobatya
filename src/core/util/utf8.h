// UTF-8 ↔ UTF-32 без Qt и без MuPDF: текстовый слой (char32_t символов stext) → строки UTF-8 для поиска,
// выделения, индекса и экспорта. Неверная последовательность при разборе — U+FFFD.
#pragma once

#include <string>
#include <string_view>

namespace ot::utf8 {

inline void append(std::string& out, char32_t c) {
    if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
        c = 0xFFFD;
    }
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

inline std::string encode(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char32_t c : text) {
        append(out, c);
    }
    return out;
}

inline std::u32string decode(std::string_view text) {
    std::u32string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto b = static_cast<unsigned char>(text[i]);
        int extra = 0;
        char32_t c = 0;
        if (b < 0x80) {
            c = b;
        } else if ((b & 0xE0) == 0xC0) {
            c = b & 0x1F;
            extra = 1;
        } else if ((b & 0xF0) == 0xE0) {
            c = b & 0x0F;
            extra = 2;
        } else if ((b & 0xF8) == 0xF0) {
            c = b & 0x07;
            extra = 3;
        } else {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        if (extra > 0 && i + extra >= text.size()) {  // обрыв в конце строки
            out.push_back(0xFFFD);
            break;
        }
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            const auto n = static_cast<unsigned char>(text[i + k]);
            if ((n & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            c = (c << 6) | (n & 0x3F);
        }
        if (!ok) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        out.push_back(c);
        i += static_cast<std::size_t>(extra) + 1;
    }
    return out;
}

}  // namespace ot::utf8
