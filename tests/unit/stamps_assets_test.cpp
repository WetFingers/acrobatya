// Генератор своих штампов (план 5.2, п. 4; эталон 3.4 «Acrobat» — штампы с именами Acrobat; comment-11, comment-13):
// stamps_assets.DISABLED_generate читает assets/stamps/stamps.json и пишет assets/stamps/<категория>/<имя>.pdf —
// одностраничные векторные PDF: «Стандартные» — двойная скруглённая рамка цветом штампа с бледной заливкой и
// надписью; «Подписать здесь» — стрелка-ярлык с белой надписью; «Динамические» — рамка повыше с надписью сверху
// и пустым местом под строки-шаблоны (их кладёт ядро при постановке, core/annot/stamps.h). Надписи — контурами
// глифов Helvetica Bold MuPDF (URW Nimbus Sans; нет глифа — segoeuib.ttf): шрифтов в файлах нет.
// Запуск (каталог вывода — OTTISK_STAMPS_OUT, иначе assets/stamps репозитория):
//   unit_tests --gtest_also_run_disabled_tests --gtest_filter=stamps_assets.*
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace fs = std::filesystem;

namespace {

struct Rgb {
    float c[3] = {0, 0, 0};
};

Rgb parseRgb(const std::string& hex) {
    Rgb out;
    const unsigned v = static_cast<unsigned>(std::stoul(hex.substr(1), nullptr, 16));
    out.c[0] = static_cast<float>((v >> 16) & 0xFF) / 255.0f;
    out.c[1] = static_cast<float>((v >> 8) & 0xFF) / 255.0f;
    out.c[2] = static_cast<float>(v & 0xFF) / 255.0f;
    return out;
}

void roundedRect(fz_context* ctx, fz_path* path, fz_rect r, float radius) {
    const float k = 0.5523f * radius;
    fz_moveto(ctx, path, r.x0 + radius, r.y0);
    fz_lineto(ctx, path, r.x1 - radius, r.y0);
    fz_curveto(ctx, path, r.x1 - radius + k, r.y0, r.x1, r.y0 + radius - k, r.x1, r.y0 + radius);
    fz_lineto(ctx, path, r.x1, r.y1 - radius);
    fz_curveto(ctx, path, r.x1, r.y1 - radius + k, r.x1 - radius + k, r.y1, r.x1 - radius, r.y1);
    fz_lineto(ctx, path, r.x0 + radius, r.y1);
    fz_curveto(ctx, path, r.x0 + radius - k, r.y1, r.x0, r.y1 - radius + k, r.x0, r.y1 - radius);
    fz_lineto(ctx, path, r.x0, r.y0 + radius);
    fz_curveto(ctx, path, r.x0, r.y0 + radius - k, r.x0 + radius - k, r.y0, r.x0 + radius, r.y0);
    fz_closepath(ctx, path);
}

struct Glyph {
    fz_font* font = nullptr;
    int gid = 0;
    float adv = 0;
};

// Надпись контурами: центр по x = cx, базовая линия y, кегль size (внутри mu::call).
void drawCaption(fz_context* ctx, fz_device* dev, const Glyph* glyphs, std::size_t count, float width, float size,
                 float cx, float y, const float* color) {
    float x = cx - width * size / 2;
    for (std::size_t i = 0; i < count; ++i) {
        const fz_matrix trm = fz_make_matrix(size, 0, 0, -size, x, y);
        if (fz_path* path = fz_outline_glyph(ctx, glyphs[i].font, glyphs[i].gid, trm)) {
            fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx), color, 1.0f, fz_default_color_params);
            fz_drop_path(ctx, path);
        }
        x += glyphs[i].adv * size;
    }
}

// Рисунок штампа (внутри mu::call): рамка или стрелка и надпись.
void drawStamp(fz_context* ctx, fz_device* dev, float w, float h, bool arrow, bool dynamic, const Glyph* glyphs,
               std::size_t count, float textW, float size, const float* color) {
    fz_path* path = fz_new_path(ctx);
    fz_stroke_state* stroke = fz_new_stroke_state(ctx);
    const float white[3] = {1, 1, 1};
    if (arrow) {
        fz_moveto(ctx, path, 1, 1);
        fz_lineto(ctx, path, w - 21, 1);
        fz_lineto(ctx, path, w - 1, h / 2);
        fz_lineto(ctx, path, w - 21, h - 1);
        fz_lineto(ctx, path, 1, h - 1);
        fz_closepath(ctx, path);
        fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx), color, 0.9f, fz_default_color_params);
        drawCaption(ctx, dev, glyphs, count, textW, size, (w - 20) / 2 + 2, h / 2 + size * 0.36f, white);
    } else {
        roundedRect(ctx, path, fz_make_rect(2, 2, w - 2, h - 2), 8);
        fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx), color, 0.08f, fz_default_color_params);
        stroke->linewidth = 2.5f;
        fz_stroke_path(ctx, dev, path, stroke, fz_identity, fz_device_rgb(ctx), color, 1.0f,
                       fz_default_color_params);
        fz_drop_path(ctx, path);
        path = fz_new_path(ctx);
        roundedRect(ctx, path, fz_make_rect(5.5f, 5.5f, w - 5.5f, h - 5.5f), 5);
        stroke->linewidth = 0.8f;
        fz_stroke_path(ctx, dev, path, stroke, fz_identity, fz_device_rgb(ctx), color, 1.0f,
                       fz_default_color_params);
        if (dynamic) {
            fz_drop_path(ctx, path);
            path = fz_new_path(ctx);
            fz_moveto(ctx, path, 14, 36);
            fz_lineto(ctx, path, w - 14, 36);
            fz_stroke_path(ctx, dev, path, stroke, fz_identity, fz_device_rgb(ctx), color, 0.6f,
                           fz_default_color_params);
            drawCaption(ctx, dev, glyphs, count, textW, size, w / 2, 29, color);
        } else {
            drawCaption(ctx, dev, glyphs, count, textW, size, w / 2, h / 2 + size * 0.36f, color);
        }
    }
    fz_drop_stroke_state(ctx, stroke);
    fz_drop_path(ctx, path);
}

}  // namespace

TEST(stamps_assets, DISABLED_generate) {
    const fs::path assets = ot::test::sourceDir() / "assets" / "stamps";
    const char* outEnv = std::getenv("OTTISK_STAMPS_OUT");
    const fs::path out = outEnv != nullptr && *outEnv != '\0' ? ot::paths::fromUtf8(outEnv) : assets;
    const nlohmann::json catalog = nlohmann::json::parse(ot::paths::readFile(assets / "stamps.json"));
    fz_context* ctx = ot::MuContext::global().get();
    ot::mu::FontHandle font(ctx);
    ot::mu::FontHandle backup(ctx);
    const char* windir = std::getenv("WINDIR");
    const std::string segoe =
        ot::paths::toUtf8(fs::path(windir != nullptr ? windir : "C:\\Windows") / "Fonts" / "segoeuib.ttf");
    const char* segoePath = segoe.c_str();
    ot::mu::call(ctx, [&] {
        font.reset(fz_new_base14_font(ctx, "Helvetica-Bold"));
        backup.reset(fz_new_font_from_file(ctx, nullptr, segoePath, 0, 0));
    });
    for (const auto& s : catalog["stamps"]) {
        const std::u32string caption = ot::utf8::decode(s["caption"].get<std::string>());
        const Rgb color = parseRgb(s["color"].get<std::string>());
        const bool arrow = s.value("shape", std::string("frame")) == "arrow";
        const bool dynamic = s.value("dynamic", false);
        std::vector<Glyph> glyphs(caption.size());
        Glyph* g = glyphs.data();
        const char32_t* chars = caption.data();
        const std::size_t count = caption.size();
        ot::mu::call(ctx, [&] {
            for (std::size_t i = 0; i < count; ++i) {
                g[i].font = font;
                g[i].gid = fz_encode_character(ctx, font, static_cast<int>(chars[i]));
                if (g[i].gid == 0 && chars[i] != U' ') {
                    g[i].font = backup;
                    g[i].gid = fz_encode_character(ctx, backup, static_cast<int>(chars[i]));
                }
                g[i].adv = fz_advance_glyph(ctx, g[i].font, g[i].gid, 0);
            }
        });
        float textW = 0;
        for (const Glyph& x : glyphs) {
            textW += x.adv;
        }
        float size = arrow ? 16.0f : (dynamic ? 19.0f : 22.0f);
        float w = 0;
        float h = 0;
        if (dynamic) {
            w = 200;
            h = 64;
            size = std::min(size, (w - 30) / textW);
        } else if (arrow) {
            h = 40;
            w = textW * size + 50;
        } else {
            h = 50;
            w = std::max(160.0f, textW * size + 36);
            if (w > 300) {
                size = (300 - 36) / textW;
                w = 300;
            }
        }
        const fs::path file = out / ot::paths::fromUtf8(s["file"].get<std::string>());
        fs::create_directories(file.parent_path());
        const std::string fileUtf8 = ot::paths::toUtf8(file);
        const char* filePath = fileUtf8.c_str();
        ot::mu::DocumentWriterHandle writer(ctx);
        ot::mu::call(ctx, [&] {
            writer.reset(fz_new_pdf_writer(ctx, filePath, "compress,garbage=deduplicate"));
            fz_device* dev = fz_begin_page(ctx, writer, fz_make_rect(0, 0, w, h));
            drawStamp(ctx, dev, w, h, arrow, dynamic, g, count, textW, size, color.c);
            fz_end_page(ctx, writer);
            fz_close_document_writer(ctx, writer);
        });
    }
}
