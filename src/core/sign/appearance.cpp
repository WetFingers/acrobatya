#include "core/sign/appearance.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <ctime>
#include <stdexcept>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace fs = std::filesystem;

namespace {

constexpr float kPadding = 2.0f;        // поле внутри рамки, пт
constexpr float kLogoAlpha = 0.3f;      // логотип фоном
constexpr float kColor[3] = {0, 0, 0};  // текст и имя — чёрным

std::string join(const std::vector<std::string>& lines) {
    std::string out;
    for (const std::string& l : lines) {
        if (!out.empty()) {
            out += '\n';
        }
        out += l;
    }
    return out;
}

fs::path segoeUiPath() {
    wchar_t dir[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return fs::path(dir) / L"Fonts" / L"segoeui.ttf";
}

// Segoe UI из системных шрифтов; нет — Helvetica MuPDF (запасные шрифты — fz_encode_character_with_fallback).
mu::FontHandle loadFont(fz_context* ctx) {
    mu::FontHandle font(ctx);
    const fs::path path = segoeUiPath();
    std::error_code ec;
    if (!path.empty() && fs::exists(path, ec)) {
        const std::string bytes = paths::readFile(path);
        mu::BufferHandle buf(ctx);
        const char* data = bytes.data();
        const std::size_t len = bytes.size();
        try {
            mu::call(ctx, [&] {
                buf.reset(
                    fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(data), len));
                font.reset(fz_new_font_from_buffer(ctx, "SegoeUI", buf, 0, 0));
            });
            return font;
        } catch (const MuError&) {  // NOLINT(bugprone-empty-catch): дальше — запасной шрифт
        }
    }
    mu::call(ctx, [&] { font.reset(fz_new_base14_font(ctx, "Helvetica")); });
    return font;
}

// Картинка из файла (байты читаем сами — путь UTF-16); нет файла или не картинка — runtime_error.
mu::ImageHandle loadImage(fz_context* ctx, const fs::path& path) {
    std::error_code ec;
    if (path.empty() || !fs::is_regular_file(path, ec)) {
        throw std::runtime_error("signature image not found: " + paths::toUtf8(path));
    }
    const std::string bytes = paths::readFile(path);
    mu::BufferHandle buf(ctx);
    mu::ImageHandle image(ctx);
    const char* data = bytes.data();
    const std::size_t len = bytes.size();
    try {
        mu::call(ctx, [&] {
            buf.reset(fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(data), len));
            image.reset(fz_new_image_from_buffer(ctx, buf));
        });
    } catch (const MuError& e) {
        throw std::runtime_error("signature image " + paths::toUtf8(path) + " cannot be read: " + e.what());
    }
    return image;
}

// Матрица картинки, вписанной в area с сохранением пропорций, по центру (y вниз — картинка прямо).
fz_matrix fitImage(const fz_image* image, const fz_rect& area) {
    const float w = area.x1 - area.x0;
    const float h = area.y1 - area.y0;
    const float iw = static_cast<float>(std::max(image->w, 1));
    const float ih = static_cast<float>(std::max(image->h, 1));
    const float scale = std::min(w / iw, h / ih);
    const float sw = iw * scale;
    const float sh = ih * scale;
    const float x = area.x0 + (w - sw) / 2.0f;
    const float y = area.y0 + (h - sh) / 2.0f;
    return fz_make_matrix(sw, 0, 0, sh, x, y);
}

fz_rect inset(const fz_rect& r, float d) {
    fz_rect out = r;
    if (r.x1 - r.x0 > 2 * d && r.y1 - r.y0 > 2 * d) {
        out.x0 += d;
        out.y0 += d;
        out.x1 -= d;
        out.y1 -= d;
    }
    return out;
}

// Символ текста: глиф (с запасным шрифтом MuPDF) и ширина при кегле 1.
struct Measured {
    char32_t c = 0;
    fz_font* font = nullptr;  // не владеет: шрифт держит MuPDF (запасной) или вызывающий
    int gid = 0;
    float adv = 0;
};

struct Placed {
    std::size_t index = 0;  // в Measured
    float x = 0;            // от левого края строки при кегле 1
};

using Line = std::vector<Placed>;

// Перенос по пробелам при ширине width (в единицах кегля 1); breakWords — длинное слово рвётся по символам.
// fits — все слова поместились целиком.
std::vector<Line> wrapText(const std::vector<Measured>& m, float spaceAdv, float width, bool breakWords,
                           bool* fits) {
    std::vector<Line> lines(1);
    float x = 0;
    *fits = true;
    std::size_t i = 0;
    while (i < m.size()) {
        if (m[i].c == U'\n') {
            lines.emplace_back();
            x = 0;
            ++i;
            continue;
        }
        if (m[i].c == U' ') {
            ++i;
            continue;
        }
        std::size_t j = i;
        float w = 0;
        while (j < m.size() && m[j].c != U' ' && m[j].c != U'\n') {
            w += m[j].adv;
            ++j;
        }
        if (!lines.back().empty() && x + spaceAdv + w > width) {
            lines.emplace_back();
            x = 0;
        }
        if (!lines.back().empty()) {
            x += spaceAdv;
        }
        if (w > width) {
            *fits = false;
        }
        for (std::size_t k = i; k < j; ++k) {
            if (breakWords && !lines.back().empty() && x + m[k].adv > width) {
                lines.emplace_back();
                x = 0;
            }
            lines.back().push_back({k, x});
            x += m[k].adv;
        }
        i = j;
    }
    return lines;
}

// Текст в area: наибольший кегль, при котором слова не рвутся (иначе — самый мелкий с переносом по символам),
// строки влево, блок по центру по высоте; глифы — контурами (fz_outline_glyph).
void drawText(fz_context* ctx, fz_device* dev, fz_font* font, fz_text_language lang, const std::string& str,
              const fz_rect& area) {
    const float width = area.x1 - area.x0;
    const float height = area.y1 - area.y0;
    if (str.empty() || width <= 1 || height <= 1) {
        return;
    }
    const std::u32string text = utf8::decode(str);
    std::vector<Measured> m(text.size());
    Measured* data = m.data();
    const char32_t* chars = text.data();
    const std::size_t count = text.size();
    float spaceAdv = 0;
    float ascender = 0.9f;
    float descender = -0.2f;
    mu::call(ctx, [&] {
        for (std::size_t k = 0; k < count; ++k) {
            fz_font* used = nullptr;
            data[k].c = chars[k];
            data[k].gid =
                fz_encode_character_with_fallback(ctx, font, static_cast<int>(chars[k]), 0, lang, &used);
            data[k].font = used;
            data[k].adv = fz_advance_glyph(ctx, used, data[k].gid, 0);
        }
        fz_font* spaceFont = nullptr;
        const int spaceGid = fz_encode_character_with_fallback(ctx, font, ' ', 0, lang, &spaceFont);
        spaceAdv = fz_advance_glyph(ctx, spaceFont, spaceGid, 0);
        ascender = fz_font_ascender(ctx, font);
        descender = fz_font_descender(ctx, font);
    });
    constexpr float kLineStep = 1.2f;
    const float blockEm = std::max(ascender - descender, 0.5f);
    // Наибольший кегль: высота — (n − 1) шагов строки + высота строки, ширина — слова целиком.
    float size = std::min(height / blockEm, 72.0f);
    std::vector<Line> lines;
    for (;;) {
        bool fits = false;
        lines = wrapText(m, spaceAdv, width / size, false, &fits);
        const float h = (static_cast<float>(lines.size() - 1) * kLineStep + blockEm) * size;
        if (fits && h <= height) {
            break;
        }
        if (size <= 3.0f) {
            lines = wrapText(m, spaceAdv, width / size, true, &fits);
            break;
        }
        size *= 0.95f;
    }
    const float blockHeight = (static_cast<float>(lines.size() - 1) * kLineStep + blockEm) * size;
    float baseline = area.y0 + (height - blockHeight) / 2.0f + ascender * size;
    const Placed* placed = nullptr;
    std::size_t placedCount = 0;
    float y = 0;
    const float x0 = area.x0;
    for (const Line& line : lines) {
        placed = line.data();
        placedCount = line.size();
        y = baseline;
        mu::call(ctx, [&] {
            fz_colorspace* rgb = fz_device_rgb(ctx);
            for (std::size_t k = 0; k < placedCount; ++k) {
                const Measured& g = data[placed[k].index];
                const fz_matrix trm = fz_make_matrix(size, 0, 0, -size, x0 + placed[k].x * size, y);
                fz_path* path = fz_outline_glyph(ctx, g.font, g.gid, trm);
                if (path != nullptr) {
                    fz_fill_path(ctx, dev, path, 0, fz_identity, rgb, kColor, 1.0f, fz_default_color_params);
                    fz_drop_path(ctx, path);
                }
            }
        });
        baseline += kLineStep * size;
    }
}

}  // namespace

SignatureAppearance SignatureAppearance::fromFlags(int flags) {
    SignatureAppearance look;
    look.graphic =
        (flags & PDF_SIGNATURE_SHOW_GRAPHIC_NAME) != 0 ? AppearanceGraphic::Name : AppearanceGraphic::None;
    look.text.labels = (flags & PDF_SIGNATURE_SHOW_LABELS) != 0;
    look.text.dn = (flags & PDF_SIGNATURE_SHOW_DN) != 0;
    look.text.date = (flags & PDF_SIGNATURE_SHOW_DATE) != 0;
    look.text.name = (flags & PDF_SIGNATURE_SHOW_TEXT_NAME) != 0;
    return look;
}

int SignatureAppearance::flags() const {
    int f = 0;
    f |= text.labels ? PDF_SIGNATURE_SHOW_LABELS : 0;
    f |= text.dn ? PDF_SIGNATURE_SHOW_DN : 0;
    f |= text.date ? PDF_SIGNATURE_SHOW_DATE : 0;
    f |= text.name ? PDF_SIGNATURE_SHOW_TEXT_NAME : 0;
    f |= graphic == AppearanceGraphic::Name ? PDF_SIGNATURE_SHOW_GRAPHIC_NAME : 0;
    return f;
}

std::string formatSignatureDate(std::int64_t time) {
    const std::time_t tt = static_cast<std::time_t>(time);
    std::tm local{};
    if (localtime_s(&local, &tt) != 0) {
        return {};
    }
    char date[40];
    char zone[8];
    if (std::strftime(date, sizeof date, "%Y.%m.%d %H:%M:%S", &local) == 0 ||
        std::strftime(zone, sizeof zone, "%z", &local) != 5) {
        return {};
    }
    return std::string(date) + ' ' + std::string(zone, 3) + '\'' + std::string(zone + 3, 2) + '\'';
}

std::vector<std::string> appearanceLines(const SignatureAppearance& look, const AppearanceData& data) {
    std::vector<std::string> lines;
    if (look.imageOnly) {
        return lines;
    }
    const auto add = [&](bool on, const std::string& label, const std::string& value) {
        if (on && !value.empty()) {
            lines.push_back(look.text.labels && !label.empty() ? label + ' ' + value : value);
        }
    };
    add(look.text.name, look.labels.signedBy, data.name);
    add(look.text.dn, look.labels.dn, data.dn);
    add(look.text.reason, look.labels.reason, data.reason);
    add(look.text.location, look.labels.location, data.location);
    add(look.text.date && data.time != 0, look.labels.date,
        data.time != 0 ? formatSignatureDate(data.time) : "");
    return lines;
}

fz_display_list* buildAppearance(fz_context* ctx, const RectF& r, const SignatureAppearance& look,
                                 const AppearanceData& data) {
    const fz_rect rect = fz_make_rect(r.x0, r.y0, r.x1, r.y1);
    const fz_rect inner = inset(rect, kPadding);
    const std::string info = join(appearanceLines(look, data));
    const bool useImage = look.imageOnly || look.graphic == AppearanceGraphic::Image;
    mu::ImageHandle image(ctx);
    if (useImage) {
        image = loadImage(ctx, look.image);
    }
    mu::ImageHandle logo(ctx);
    if (!look.logo.empty()) {
        logo = loadImage(ctx, look.logo);
    }
    mu::FontHandle font = loadFont(ctx);
    fz_text_language lang = FZ_LANG_UNSET;
    const char* language = look.language.c_str();
    mu::call(ctx, [&] { lang = fz_text_language_from_string(language); });

    // Раскладка: графика слева и текст справа; одно из двух — на всю рамку.
    const bool graphic = look.imageOnly || look.graphic != AppearanceGraphic::None;
    const bool hasText = !info.empty();
    fz_rect graphicArea = inner;
    fz_rect textArea = inner;
    if (graphic && hasText) {
        const float mid = (inner.x0 + inner.x1) / 2.0f;
        graphicArea.x1 = mid - kPadding;
        textArea.x0 = mid + kPadding;
    }

    mu::DisplayListHandle list(ctx);
    mu::DeviceHandle dev(ctx);
    mu::call(ctx, [&] {
        list.reset(fz_new_display_list(ctx, rect));
        dev.reset(fz_new_list_device(ctx, list));
    });
    if (logo) {
        const fz_matrix m = fitImage(logo.get(), inner);
        mu::call(ctx, [&] { fz_fill_image(ctx, dev, logo, m, kLogoAlpha, fz_default_color_params); });
    }
    if (image && graphic) {
        const fz_matrix m = fitImage(image.get(), graphicArea);
        mu::call(ctx, [&] { fz_fill_image(ctx, dev, image, m, 1.0f, fz_default_color_params); });
    } else if (!look.imageOnly && look.graphic == AppearanceGraphic::Name) {
        drawText(ctx, dev, font, lang, data.name, graphicArea);
    }
    if (hasText) {
        drawText(ctx, dev, font, lang, info, textArea);
    }
    mu::call(ctx, [&] { fz_close_device(ctx, dev); });
    dev.reset();
    return list.release();
}

Bitmap previewAppearance(const SignatureAppearance& look, const AppearanceData& data, float widthPt,
                         float heightPt, float dpi) {
    MuContext& mu = MuContext::forThisThread();
    fz_context* ctx = mu.get();
    const RectF rect{0, 0, widthPt, heightPt};
    mu::DisplayListHandle list(ctx, buildAppearance(ctx, rect, look, data));
    const float scale = dpi / 72.0f;
    const int w = std::max(1, static_cast<int>(std::lround(widthPt * scale)));
    const int h = std::max(1, static_cast<int>(std::lround(heightPt * scale)));
    mu::PixmapHandle pix(ctx);
    mu::DeviceHandle dev(ctx);
    mu::call(ctx, [&] {
        pix.reset(fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, nullptr, 0));
        fz_clear_pixmap_with_value(ctx, pix, 255);
        dev.reset(fz_new_draw_device(ctx, fz_scale(scale, scale), pix));
        fz_run_display_list(ctx, list, dev, fz_identity, fz_infinite_rect, nullptr);
        fz_close_device(ctx, dev);
    });
    dev.reset();
    Bitmap out;
    out.width = w;
    out.height = h;
    out.components = 3;
    out.stride = w * 3;
    out.pixels.resize(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(h));
    const unsigned char* src = nullptr;
    int stride = 0;
    mu::call(ctx, [&] {
        src = fz_pixmap_samples(ctx, pix);
        stride = static_cast<int>(fz_pixmap_stride(ctx, pix));
    });
    for (int y = 0; y < h; ++y) {
        std::copy_n(src + static_cast<std::ptrdiff_t>(y) * stride, out.stride, out.row(y));
    }
    return out;
}

}  // namespace ot::sign
