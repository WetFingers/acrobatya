#include "core/create/rtf_writer.h"

#include <cmath>

#include "core/text/stext.h"
#include "core/util/utf8.h"

namespace ot {

namespace {

bool endsWith(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

bool isUpper(char c) { return c >= 'A' && c <= 'Z'; }
bool isLower(char c) { return c >= 'a' && c <= 'z'; }

void appendUnicode(std::string& out, char32_t c) {
    auto unit = [&out](unsigned v) {
        const int n = v > 0x7FFF ? static_cast<int>(v) - 0x10000 : static_cast<int>(v);
        out += "\\u" + std::to_string(n) + "?";
    };
    if (c > 0xFFFF) {
        const char32_t v = c - 0x10000;
        unit(0xD800 + static_cast<unsigned>(v >> 10));
        unit(0xDC00 + static_cast<unsigned>(v & 0x3FF));
    } else {
        unit(static_cast<unsigned>(c));
    }
}

}  // namespace

std::string rtfText(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char32_t c : text) {
        if (c == U'\\' || c == U'{' || c == U'}') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == U'\t') {
            out += "\\tab ";
        } else if (c < 0x20) {
            continue;
        } else if (c < 0x80) {
            out += static_cast<char>(c);
        } else {
            appendUnicode(out, c);
        }
    }
    return out;
}

std::string rtfFontFamily(std::string_view name) {
    if (name.size() > 7 && name[6] == '+') {
        name.remove_prefix(7);
    }
    if (const auto cut = name.find_first_of("-,"); cut != std::string_view::npos) {
        name = name.substr(0, cut);
    }
    for (const std::string_view tail : {"PSMT", "MT", "PS"}) {
        if (endsWith(name, tail) && name.size() > tail.size()) {
            name.remove_suffix(tail.size());
            break;
        }
    }
    std::string out;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i > 0 && isUpper(c) &&
            (isLower(name[i - 1]) || (isUpper(name[i - 1]) && i + 1 < name.size() && isLower(name[i + 1])))) {
            out += ' ';
        }
        out += c;
    }
    return out.empty() ? std::string("Arial") : out;
}

int RtfWriter::fontIndex(const std::string& family) {
    for (int i = 0; i < static_cast<int>(fonts_.size()); ++i) {
        if (fonts_[i] == family) {
            return i;
        }
    }
    fonts_.push_back(family);
    return static_cast<int>(fonts_.size()) - 1;
}

void RtfWriter::addPage(const TextPage& page) {
    if (pages_ > 0) {
        body_ += "\\page\n";
    }
    ++pages_;
    const auto& chars = page.chars();
    for (const TextBlock& block : page.blocks()) {
        if (block.kind != TextBlockKind::Text || block.lineCount == 0) {
            continue;
        }
        body_ += "\\pard\\plain ";
        int font = -1;
        int halfPoints = -1;
        int bold = -1;
        int italic = -1;
        std::u32string run;
        auto flush = [&] {
            body_ += rtfText(run);
            run.clear();
        };
        for (int l = block.firstLine; l < block.firstLine + block.lineCount; ++l) {
            const TextLine& line = page.lines()[l];
            if (l > block.firstLine && !run.empty() && run.back() != U' ') {
                run += U' ';  // строки абзаца — через пробел
            }
            for (int i = line.firstChar; i < line.firstChar + line.charCount; ++i) {
                const TextChar& ch = chars[i];
                const TextFont* info = ch.font >= 0 ? &page.fonts()[ch.font] : nullptr;
                const int f = fontIndex(rtfFontFamily(info != nullptr ? info->name : std::string()));
                const int hp = static_cast<int>(std::lround(ch.size * 2));
                const int b = (info != nullptr && info->bold) || (ch.flags & kCharBold) != 0 ? 1 : 0;
                const int it = info != nullptr && info->italic ? 1 : 0;
                // Пробел стиль не меняет: «Жирная строка» — одним \b.
                const bool space = ch.c == U' ';
                if (!space && (f != font || hp != halfPoints || b != bold || it != italic)) {
                    flush();
                    body_ += "\\f" + std::to_string(f) + " \\fs" + std::to_string(hp) + " " +
                             (b != 0 ? "\\b " : "\\b0 ") + (it != 0 ? "\\i " : "\\i0 ");
                    font = f;
                    halfPoints = hp;
                    bold = b;
                    italic = it;
                }
                run += ch.c;
            }
        }
        flush();
        body_ += "\\par\n";
    }
}

std::string RtfWriter::finish() const {
    std::string out = "{\\rtf1\\ansi\\ansicpg1251\\deff0\\uc1\n{\\fonttbl";
    const std::vector<std::string> fonts = fonts_.empty() ? std::vector<std::string>{"Arial"} : fonts_;
    for (std::size_t i = 0; i < fonts.size(); ++i) {
        out += "{\\f" + std::to_string(i) + "\\fnil\\fcharset204 " + rtfText(utf8::decode(fonts[i])) + ";}";
    }
    out += "}\n\\viewkind4\n";
    out += body_;
    out += "}";
    return out;
}

}  // namespace ot
