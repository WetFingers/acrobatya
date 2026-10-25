// Свой вид текстового поля формы (см. forms_internal.h writeOwnTextAppearance; эталон 3.6 «Граничные
// случаи»): синтез MuPDF берёт base14 из /DA и для кириллицы — невстроенный «HelvCYR», поле пароля рисует
// открытым текстом. Здесь — тот же макет, что у синтеза MuPDF (pdf-appearance.c
// pdf_write_tx_widget_appearance: фон /MK /BG, рамка /MK /BC шириной /BS /W, отступ — две ширины рамки,
// однострочный — по центру высоты, многострочный — с переносом по словам, гребёнка — знак в ячейке), но шрифт
// — встроенный TrueType семейства /DA (core/annot/freetext.h FieldFont) в /AcroForm /DR, /DA поля указывает
// на него.
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/annot/freetext.h"
#include "core/doc/document.h"
#include "core/forms/fields.h"
#include "core/forms/forms_internal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/utf8.h"

namespace ot::forms {

namespace widget_ap {

// Знаки WinAnsi (их синтез MuPDF рисует base14 Helvetica, своему виду они не нужны).
bool isWinAnsi(char32_t c) {
    if (c < 0x80 || (c >= 0xA0 && c <= 0xFF)) {
        return true;
    }
    static constexpr char32_t kExtra[] = {0x152,  0x153,  0x160,  0x161,  0x178,  0x17D,  0x17E,
                                          0x192,  0x2C6,  0x2DC,  0x2013, 0x2014, 0x2018, 0x2019,
                                          0x201A, 0x201C, 0x201D, 0x201E, 0x2020, 0x2021, 0x2022,
                                          0x2026, 0x2030, 0x2039, 0x203A, 0x20AC, 0x2122};
    return std::find(std::begin(kExtra), std::end(kExtra), c) != std::end(kExtra);
}

bool ownResource(const std::string& font) {
    return std::any_of(std::begin(annot::kTextFonts), std::end(annot::kTextFonts),
                       [&font](const annot::TextFont& f) { return font == f.resource; });
}

struct Color {
    int n = 0;
    std::array<float, 4> c{};
};

// Состояние виджета, прочитанное внутри mu::call (строки — снаружи).
struct State {
    int isText = 0;
    int flags = 0;
    int maxLen = 0;
    int quadding = 0;
    int rotation = 0;
    fz_rect rect{};
    float border = 1;
    char borderStyle = 'S';
    Color bg;
    Color bc;
    pdf_obj* value = nullptr;
    pdf_obj* da = nullptr;
};

void appendColor(fz_context* ctx, fz_buffer* buf, const Color& c, bool stroke) {
    if (c.n == 1) {
        fz_append_printf(ctx, buf, "%g %s\n", c.c[0], stroke ? "G" : "g");
    } else if (c.n == 3) {
        fz_append_printf(ctx, buf, "%g %g %g %s\n", c.c[0], c.c[1], c.c[2], stroke ? "RG" : "rg");
    } else if (c.n == 4) {
        fz_append_printf(ctx, buf, "%g %g %g %g %s\n", c.c[0], c.c[1], c.c[2], c.c[3], stroke ? "K" : "k");
    }
}

Color colorOf(fz_context* ctx, pdf_obj* array) {
    Color c;
    const int n = pdf_array_len(ctx, array);
    if (n == 1 || n == 3 || n == 4) {
        c.n = n;
        for (int i = 0; i < n; ++i) {
            c.c[static_cast<std::size_t>(i)] = pdf_array_get_real(ctx, array, i);
        }
    }
    return c;
}

struct Line {
    float x = 0;
    float y = 0;
    std::u32string text;
};

}  // namespace widget_ap

namespace detail {

bool writeOwnTextAppearance(Document& doc, pdf_annot* widget) {
    using namespace widget_ap;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = pdfOf(doc);
    State s;
    mu::call(ctx, [&] {
        pdf_obj* obj = pdf_annot_obj(ctx, widget);
        s.isText = pdf_name_eq(ctx, pdf_dict_get_inheritable(ctx, obj, PDF_NAME(FT)), PDF_NAME(Tx));
        s.flags = pdf_field_flags(ctx, obj);
        s.maxLen = pdf_dict_get_inheritable_int(ctx, obj, PDF_NAME(MaxLen));
        s.quadding = pdf_annot_quadding(ctx, widget);
        s.rect = pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect));
        pdf_obj* mk = pdf_dict_get(ctx, obj, PDF_NAME(MK));
        s.rotation = pdf_dict_get_int(ctx, mk, PDF_NAME(R));
        s.bg = colorOf(ctx, pdf_dict_get(ctx, mk, PDF_NAME(BG)));
        s.bc = colorOf(ctx, pdf_dict_get(ctx, mk, PDF_NAME(BC)));
        pdf_obj* bs = pdf_dict_get(ctx, obj, PDF_NAME(BS));
        if (pdf_dict_get(ctx, bs, PDF_NAME(W)) != nullptr) {
            s.border = pdf_dict_get_real(ctx, bs, PDF_NAME(W));
        }
        const char* style = pdf_dict_get_name(ctx, bs, PDF_NAME(S));
        if (style != nullptr && style[0] != '\0') {
            s.borderStyle = style[0];
        }
        s.value = pdf_dict_get_inheritable(ctx, obj, PDF_NAME(V));
        s.da = pdf_dict_get_inheritable(ctx, obj, PDF_NAME(DA));
        if (s.da == nullptr) {
            s.da = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/DA");
        }
    });
    if (s.isText == 0) {
        return false;
    }
    // Значение для показа: формат /AA /F при включённом JavaScript.
    std::string value = textOf(ctx, s.value);
    if (pdf->js != nullptr) {
        char* formatted = nullptr;
        mu::call(ctx, [&] { formatted = pdf_field_event_format(ctx, pdf, pdf_annot_obj(ctx, widget)); });
        if (formatted != nullptr) {
            value = formatted;
            fz_free(ctx, formatted);
        }
    }
    const std::string daText = textOf(ctx, s.da);
    char fontName[64] = {};
    float size = 0;
    int n = 0;
    float color[4] = {};
    const char* daPtr = daText.c_str();
    mu::call(ctx, [&] {
        pdf_parse_default_appearance_unmapped(ctx, daPtr, fontName, sizeof fontName, &size, &n, color);
    });
    const std::string font = fontName;
    const bool password = (s.flags & ff::kPassword) != 0;
    const std::u32string text = utf8::decode(value);
    const bool foreign = std::any_of(text.begin(), text.end(), [](char32_t c) { return !isWinAnsi(c); });
    const bool own = ownResource(font);
    if (!own && !password && !foreign) {
        return false;
    }
    const std::unique_ptr<annot::FieldFont> ff = annot::FieldFont::open(ctx, font);
    if (!ff) {
        spdlog::warn("forms: no embeddable font for field /DA {} — MuPDF appearance", font);
        return false;
    }
    const std::u32string shown = password ? std::u32string(text.size(), U'*') : text;
    if (!ff->covers(shown)) {
        return false;  // знаков нет и в нашем шрифте (CJK) — пусть рисует MuPDF своими шрифтами
    }
    const int fontNum = ff->install(doc, shown);
    const std::string resource = ff->font().resource;

    // Макет как у pdf_write_tx_widget_appearance.
    float w = s.rect.x1 - s.rect.x0;
    float h = s.rect.y1 - s.rect.y0;
    const int r = ((s.rotation % 360) + 360) % 360;
    if (r == 90 || r == 270) {
        std::swap(w, h);
    }
    const float b = s.bc.n > 0 || s.borderStyle == 'U' ? std::max(0.0f, s.border) : 0.0f;
    const bool multiline = (s.flags & ff::kMultiline) != 0;
    const bool comb = (s.flags & ff::kComb) != 0 && s.maxLen > 0 && !multiline && !password;
    const int q = std::clamp(s.quadding, 0, 2);
    const auto advance = [&ff](char32_t c) { return ff->advance(c); };
    const auto width = [&advance](std::u32string_view line) {
        float sum = 0;
        for (const char32_t c : line) {
            sum += advance(c);
        }
        return sum;
    };

    std::vector<Line> lines;
    if (comb) {
        if (size <= 0) {
            size = std::max(1.0f, std::min(h * 0.8f, 12.0f));
        }
        const float cell = w / static_cast<float>(s.maxLen);
        const float y = h / 2 - 0.3f * size;
        for (std::size_t i = 0; i < shown.size() && static_cast<int>(i) < s.maxLen; ++i) {
            const float cw = advance(shown[i]) * size;
            lines.push_back({cell * static_cast<float>(i) + (cell - cw) / 2, y, std::u32string(1, shown[i])});
        }
    } else if (multiline) {
        const float pad = 2 * b;
        const float wi = std::max(1.0f, w - 2 * pad);
        const float hi = h - 2 * pad;
        if (size <= 0) {
            size = 12;
        }
        const float lineHeight = 1.116f * size;
        float y = pad + hi - lineHeight;
        for (const std::u32string& line : annot::wrapText(shown, wi / size, advance)) {
            const float lw = width(line) * size;
            const float x = pad + (q == 1 ? (wi - lw) / 2 : q == 2 ? wi - lw : 0.0f);
            lines.push_back({x, y, line});
            y -= lineHeight;
        }
    } else {
        const float pad = 2 * b;
        const float wi = w - 2 * pad;
        const float hi = h - 2 * pad;
        std::u32string line = shown;
        line.erase(
            std::remove_if(line.begin(), line.end(), [](char32_t c) { return c == U'\r' || c == U'\n'; }),
            line.end());
        if (size <= 0) {
            const float ms = width(line);
            size = ms > 0 ? wi / ms : 12;
            size = std::min(size, hi);
        }
        const float lw = width(line) * size;
        const float x = pad + (q == 1 ? (wi - lw) / 2 : q == 2 ? wi - lw : 0.0f);
        lines.push_back({x, pad + hi / 2 - 0.3f * size, line});
    }
    struct Placed {
        float x = 0;
        float y = 0;
        std::string hex;
    };
    std::vector<Placed> placed;
    for (const Line& l : lines) {
        if (!l.text.empty()) {
            placed.push_back({l.x, l.y, ff->hex(l.text)});
        }
    }
    Color textColor;
    textColor.n = n;
    std::copy(color, color + 4, textColor.c.begin());

    // /DA поля — встроенный шрифт; кегль и цвет прежние (0 — авто — остаётся авто).
    const char* res = resource.c_str();
    mu::BufferHandle buf(ctx);
    mu::PdfObjHandle resources(ctx);
    const float autoSize = size;
    const bool autoFont = daText.find(" 0 Tf") != std::string::npos;
    char daOut[256] = {};
    mu::call(ctx, [&] {
        pdf_print_default_appearance(ctx, daOut, sizeof daOut, res, autoFont ? 0.0f : autoSize, n, color);
        pdf_obj* obj = pdf_annot_obj(ctx, widget);
        pdf_dict_put_text_string(ctx, obj, PDF_NAME(DA), daOut);

        resources.reset(pdf_new_dict(ctx, pdf, 1));
        pdf_obj* fonts = pdf_dict_put_dict(ctx, resources, PDF_NAME(Font), 1);
        pdf_dict_puts_drop(ctx, fonts, res, pdf_new_indirect(ctx, pdf, fontNum, 0));
        buf.reset(fz_new_buffer(ctx, 512));
        fz_buffer* out = buf;
        fz_append_string(ctx, out, "/Tx BMC\nq\n");
        if (s.bg.n > 0) {
            appendColor(ctx, out, s.bg, false);
            fz_append_printf(ctx, out, "0 0 %g %g re\nf\n", w, h);
        }
        if (b > 0 && s.bc.n > 0) {
            appendColor(ctx, out, s.bc, true);
            fz_append_printf(ctx, out, "%g w\n", b);
            if (s.borderStyle == 'D') {
                fz_append_string(ctx, out, "[3] 0 d\n");
            }
            if (s.borderStyle == 'U') {
                fz_append_printf(ctx, out, "0 %g m %g %g l\nS\n", b / 2, w, b / 2);
            } else {
                fz_append_printf(ctx, out, "%g %g %g %g re\nS\n", b / 2, b / 2, w - b, h - b);
            }
            if (comb && s.maxLen > 1) {
                const float cell = (w - 2 * b) / static_cast<float>(s.maxLen);
                for (int i = 1; i < s.maxLen; ++i) {
                    const float x = b + cell * static_cast<float>(i);
                    fz_append_printf(ctx, out, "%g %g m %g %g l S\n", x, b, x, h - b);
                }
            }
            if (s.borderStyle == 'D') {
                fz_append_string(ctx, out, "[] 0 d\n");
            }
        }
        fz_append_printf(ctx, out, "%g %g %g %g re\nW\nn\n", b, b, std::max(0.0f, w - 2 * b),
                         std::max(0.0f, h - 2 * b));
        fz_append_printf(ctx, out, "BT\n/%s %g Tf\n", res, autoSize);
        appendColor(ctx, out, textColor, false);
        for (const Placed& p : placed) {
            fz_append_printf(ctx, out, "1 0 0 1 %g %g Tm\n<%s> Tj\n", p.x, p.y, p.hex.c_str());
        }
        fz_append_string(ctx, out, "ET\nQ\nEMC\n");
        const fz_matrix matrix = fz_rotate(static_cast<float>(r));
        pdf_set_annot_appearance(ctx, widget, "N", nullptr, matrix, fz_make_rect(0, 0, w, h), resources, buf);
    });
    return true;
}

}  // namespace detail

}  // namespace ot::forms
