#include "core/edit/system_fonts.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <utility>

#include "core/edit/font_info.h"

namespace ot::edit {

namespace system_fonts_detail {

// Ключ сравнения имён: строчные латинские, без пробелов, дефисов и подчёркиваний.
std::string squash(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == ' ' || c == '-' || c == '_') {
            continue;
        }
        out.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
    }
    return out;
}

struct Substitute {
    std::string_view key;  // squash()
    std::string_view family;
};

constexpr std::array<Substitute, 30> kSubstitutes = {{
    {"helvetica", "Arial"},
    {"helveticaneue", "Arial"},
    {"arial", "Arial"},
    {"liberationsans", "Arial"},
    {"nimbussans", "Arial"},
    {"nimbussansl", "Arial"},
    {"arimo", "Arial"},
    {"helveticanarrow", "Arial Narrow"},
    {"arialnarrow", "Arial Narrow"},
    {"liberationsansnarrow", "Arial Narrow"},
    {"nimbussansnarrow", "Arial Narrow"},
    {"times", "Times New Roman"},
    {"timesroman", "Times New Roman"},
    {"timesnewroman", "Times New Roman"},
    {"tmsrmn", "Times New Roman"},
    {"liberationserif", "Times New Roman"},
    {"nimbusroman", "Times New Roman"},
    {"nimbusromanno9l", "Times New Roman"},
    {"tinos", "Times New Roman"},
    {"courier", "Courier New"},
    {"couriernew", "Courier New"},
    {"liberationmono", "Courier New"},
    {"nimbusmono", "Courier New"},
    {"nimbusmonol", "Courier New"},
    {"nimbusmonops", "Courier New"},
    {"cousine", "Courier New"},
    {"symbol", "Symbol"},
    {"standardsymbolsps", "Symbol"},
    {"zapfdingbats", "Segoe UI Symbol"},
    {"dingbats", "Segoe UI Symbol"},
}};

struct KnownFace {
    const char* family;
    const char* file;
    int weight;
    bool italic;
    const char* postscript;
    int index;
};

constexpr std::array<KnownFace, 45> kKnownFaces = {{
    {"Arial", "arial.ttf", 400, false, "ArialMT", 0},
    {"Arial", "arialbd.ttf", 700, false, "Arial-BoldMT", 0},
    {"Arial", "ariali.ttf", 400, true, "Arial-ItalicMT", 0},
    {"Arial", "arialbi.ttf", 700, true, "Arial-BoldItalicMT", 0},
    {"Arial Narrow", "ARIALN.TTF", 400, false, "ArialNarrow", 0},
    {"Arial Narrow", "ARIALNB.TTF", 700, false, "ArialNarrow-Bold", 0},
    {"Arial Narrow", "ARIALNI.TTF", 400, true, "ArialNarrow-Italic", 0},
    {"Arial Narrow", "ARIALNBI.TTF", 700, true, "ArialNarrow-BoldItalic", 0},
    {"Times New Roman", "times.ttf", 400, false, "TimesNewRomanPSMT", 0},
    {"Times New Roman", "timesbd.ttf", 700, false, "TimesNewRomanPS-BoldMT", 0},
    {"Times New Roman", "timesi.ttf", 400, true, "TimesNewRomanPS-ItalicMT", 0},
    {"Times New Roman", "timesbi.ttf", 700, true, "TimesNewRomanPS-BoldItalicMT", 0},
    {"Courier New", "cour.ttf", 400, false, "CourierNewPSMT", 0},
    {"Courier New", "courbd.ttf", 700, false, "CourierNewPS-BoldMT", 0},
    {"Courier New", "couri.ttf", 400, true, "CourierNewPS-ItalicMT", 0},
    {"Courier New", "courbi.ttf", 700, true, "CourierNewPS-BoldItalicMT", 0},
    {"Segoe UI", "segoeui.ttf", 400, false, "SegoeUI", 0},
    {"Segoe UI", "segoeuib.ttf", 700, false, "SegoeUI-Bold", 0},
    {"Segoe UI", "segoeuii.ttf", 400, true, "SegoeUI-Italic", 0},
    {"Segoe UI", "segoeuiz.ttf", 700, true, "SegoeUI-BoldItalic", 0},
    {"Segoe UI", "segoeuil.ttf", 300, false, "SegoeUI-Light", 0},
    {"Segoe UI", "segoeuisl.ttf", 350, false, "SegoeUI-Semilight", 0},
    {"Segoe UI Symbol", "seguisym.ttf", 400, false, "SegoeUISymbol", 0},
    {"Calibri", "calibri.ttf", 400, false, "Calibri", 0},
    {"Calibri", "calibrib.ttf", 700, false, "Calibri-Bold", 0},
    {"Calibri", "calibrii.ttf", 400, true, "Calibri-Italic", 0},
    {"Calibri", "calibriz.ttf", 700, true, "Calibri-BoldItalic", 0},
    {"Calibri", "calibril.ttf", 300, false, "Calibri-Light", 0},
    {"Calibri", "calibrili.ttf", 300, true, "Calibri-LightItalic", 0},
    {"Cambria", "cambria.ttc", 400, false, "Cambria", 0},
    {"Cambria", "cambriab.ttf", 700, false, "Cambria-Bold", 0},
    {"Cambria", "cambriai.ttf", 400, true, "Cambria-Italic", 0},
    {"Cambria", "cambriaz.ttf", 700, true, "Cambria-BoldItalic", 0},
    {"Georgia", "georgia.ttf", 400, false, "Georgia", 0},
    {"Georgia", "georgiab.ttf", 700, false, "Georgia-Bold", 0},
    {"Georgia", "georgiai.ttf", 400, true, "Georgia-Italic", 0},
    {"Georgia", "georgiaz.ttf", 700, true, "Georgia-BoldItalic", 0},
    {"Verdana", "verdana.ttf", 400, false, "Verdana", 0},
    {"Verdana", "verdanab.ttf", 700, false, "Verdana-Bold", 0},
    {"Verdana", "verdanai.ttf", 400, true, "Verdana-Italic", 0},
    {"Verdana", "verdanaz.ttf", 700, true, "Verdana-BoldItalic", 0},
    {"Tahoma", "tahoma.ttf", 400, false, "Tahoma", 0},
    {"Tahoma", "tahomabd.ttf", 700, false, "Tahoma-Bold", 0},
    {"Symbol", "symbol.ttf", 400, false, "SymbolMT", 0},
    {"Wingdings", "wingding.ttf", 400, false, "Wingdings-Regular", 0},
}};

const char* weightWord(int weight) {
    switch (weight) {
        case 100:
            return "Thin";
        case 200:
            return "ExtraLight";
        case 300:
            return "Light";
        case 350:
            return "Semilight";
        case 500:
            return "Medium";
        case 600:
            return "Semibold";
        case 700:
            return "Bold";
        case 800:
            return "ExtraBold";
        case 900:
            return "Black";
        default:
            return "";
    }
}

// Полное имя грани, как у DirectWrite FULL_NAME: «Arial», «Arial Bold», «Arial Bold Italic», «Segoe UI
// Light».
std::string fullName(const SystemFace& f) {
    std::string out = f.family;
    if (const char* w = weightWord(f.weight); *w != '\0') {
        out += ' ';
        out += w;
    }
    if (f.italic) {
        out += " Italic";
    }
    return out;
}

}  // namespace system_fonts_detail

namespace sfd = system_fonts_detail;

std::string lowerAscii(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

std::string_view metricSubstitute(std::string_view family) {
    const std::string key = sfd::squash(family);
    if (key.empty()) {
        return {};
    }
    for (const sfd::Substitute& s : sfd::kSubstitutes) {
        if (s.key == key) {
            return s.family;
        }
    }
    return {};
}

SystemFace pickFace(const std::vector<SystemFace>& faces, int weight, bool italic) {
    const bool anyItalicMatch =
        std::any_of(faces.begin(), faces.end(), [&](const SystemFace& f) { return f.italic == italic; });
    const SystemFace* best = nullptr;
    auto better = [&](const SystemFace& a, const SystemFace& b) {
        const int da = std::abs(a.weight - weight);
        const int db = std::abs(b.weight - weight);
        if (da != db) {
            return da < db;
        }
        return weight <= 400 ? a.weight < b.weight : a.weight > b.weight;
    };
    for (const SystemFace& f : faces) {
        if (anyItalicMatch && f.italic != italic) {
            continue;
        }
        if (best == nullptr || better(f, *best)) {
            best = &f;
        }
    }
    SystemFace out = *best;
    out.simulatedBold = weight >= 600 && out.weight < 600;
    out.simulatedItalic = italic && !out.italic;
    return out;
}

std::optional<SystemFace> findSystemFont(SystemFonts& fonts, const FontQuery& q) {
    const std::string name = stripSubsetTag(q.name);
    const bool bold = q.weight >= 600;
    if (!name.empty()) {
        if (auto f = fonts.byName(name); f && (f->weight >= 600) == bold && f->italic == q.italic) {
            return f;
        }
    }
    const std::string family = q.family.empty() ? parseFontName(q.name).family : q.family;
    if (family.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> candidates = {family};
    if (std::string squashed; family.find(' ') != std::string::npos) {
        squashed = family;
        squashed.erase(std::remove(squashed.begin(), squashed.end(), ' '), squashed.end());
        candidates.push_back(squashed);
    }
    for (const std::string& c : candidates) {
        if (auto f = fonts.face(c, q.weight, q.italic)) {
            return f;
        }
    }
    if (q.metricSubstitute) {
        if (const std::string_view sub = metricSubstitute(family); !sub.empty()) {
            return fonts.face(sub, q.weight, q.italic);
        }
    }
    return std::nullopt;
}

std::filesystem::path FakeSystemFonts::defaultDir() {
    const char* windir =
        std::getenv("WINDIR");  // NOLINT(concurrency-mt-unsafe): чтение при создании подделки
    return std::filesystem::path(windir != nullptr && *windir != '\0' ? windir : "C:\\Windows") / "Fonts";
}

FakeSystemFonts::FakeSystemFonts(std::filesystem::path dir) {
    std::error_code ec;
    for (const sfd::KnownFace& k : sfd::kKnownFaces) {
        std::filesystem::path file = dir / k.file;
        if (!std::filesystem::exists(file, ec)) {
            continue;
        }
        SystemFace f;
        f.family = k.family;
        f.postscript = k.postscript;
        f.file = std::move(file);
        f.index = k.index;
        f.weight = k.weight;
        f.italic = k.italic;
        faces_.push_back(std::move(f));
    }
}

std::optional<SystemFace> FakeSystemFonts::face(std::string_view family, int weight, bool italic) {
    const std::string key = lowerAscii(family);
    std::vector<SystemFace> same;
    for (const SystemFace& f : faces_) {
        if (lowerAscii(f.family) == key) {
            same.push_back(f);
        }
    }
    if (same.empty()) {
        return std::nullopt;
    }
    return pickFace(same, weight, italic);
}

std::optional<SystemFace> FakeSystemFonts::byName(std::string_view name) {
    const std::string key = lowerAscii(name);
    for (const SystemFace& f : faces_) {
        if (lowerAscii(f.postscript) == key || lowerAscii(sfd::fullName(f)) == key) {
            return f;
        }
    }
    return std::nullopt;
}

std::vector<std::string> FakeSystemFonts::families() {
    std::vector<std::string> out;
    for (const SystemFace& f : faces_) {
        out.push_back(f.family);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void FakeSystemFonts::remove(std::string_view family) {
    const std::string key = lowerAscii(family);
    faces_.erase(std::remove_if(faces_.begin(), faces_.end(),
                                [&](const SystemFace& f) { return lowerAscii(f.family) == key; }),
                 faces_.end());
}

void FakeSystemFonts::add(SystemFace face) { faces_.push_back(std::move(face)); }

}  // namespace ot::edit
