#include "core/render/renderer.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/display_cache.h"
#include "core/render/recolor_device.h"

namespace ot {

namespace {

fz_matrix toFz(const Matrix& m) { return {m.a, m.b, m.c, m.d, m.e, m.f}; }

// Сглаживание и тонкие линии контекста на время рендера; восстановление не бросает.
class StyleScope {
public:
    StyleScope(fz_context* ctx, const RenderStyle& style) noexcept
        : ctx_(ctx),
          text_(fz_text_aa_level(ctx)),
          graphics_(fz_graphics_aa_level(ctx)),
          minLine_(fz_graphics_min_line_width(ctx)),
          style_(style) {
        if (style.textAa >= 0) {
            fz_set_text_aa_level(ctx, style.textAa);
        }
        if (style.graphicsAa >= 0) {
            fz_set_graphics_aa_level(ctx, style.graphicsAa);
        }
        if (style.thinLines) {
            fz_set_graphics_min_line_width(ctx, kThinLinePx);
        }
    }
    ~StyleScope() {
        if (style_.textAa >= 0) {
            fz_set_text_aa_level(ctx_, text_);
        }
        if (style_.graphicsAa >= 0) {
            fz_set_graphics_aa_level(ctx_, graphics_);
        }
        if (style_.thinLines) {
            fz_set_graphics_min_line_width(ctx_, minLine_);
        }
    }
    StyleScope(const StyleScope&) = delete;
    StyleScope& operator=(const StyleScope&) = delete;

private:
    static constexpr float kThinLinePx = 1.0f;
    fz_context* ctx_;
    int text_;
    int graphics_;
    float minLine_;
    const RenderStyle& style_;
};

// Разделения для симуляции overprint (пустой набор — как mutool draw -M 1): растр с ними рисует в CMYK
// (или в OutputIntent) и переводит обратно.
class SeparationsScope {
public:
    explicit SeparationsScope(fz_context* ctx) noexcept : ctx_(ctx) {}
    ~SeparationsScope() { fz_drop_separations(ctx_, seps_); }
    SeparationsScope(const SeparationsScope&) = delete;
    SeparationsScope& operator=(const SeparationsScope&) = delete;
    fz_separations* get() const noexcept { return seps_; }
    void reset(fz_separations* seps) noexcept { seps_ = seps; }

private:
    fz_context* ctx_;
    fz_separations* seps_ = nullptr;
};

// «Инвертировать всё»: каналы цвета → альфа − канал (премультиплицированный BGRA) или 255 − канал (RGB).
void invertPixels(std::uint8_t* buffer, int width, int height, int stride, PixelFormat format) noexcept {
    const bool alpha = format == PixelFormat::Bgra32Premultiplied;
    const int n = alpha ? 4 : 3;
    for (int y = 0; y < height; ++y) {
        std::uint8_t* p = buffer + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < width; ++x, p += n) {
            const std::uint8_t a = alpha ? p[3] : 255;
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<std::uint8_t>(a - std::min(p[c], a));
            }
        }
    }
}

}  // namespace

void renderDisplayList(fz_context* ctx, const DisplayList& list, const Matrix& matrix, const IRect& area,
                       std::uint8_t* buffer, int stride, PixelFormat format, fz_cookie* cookie,
                       const RenderStyle& style) {
    const int bytesPerPixel = format == PixelFormat::Bgra32Premultiplied ? 4 : 3;
    if (area.empty() || buffer == nullptr || stride < area.width() * bytesPerPixel) {
        throw std::invalid_argument("renderDisplayList: empty area, null buffer or short stride");
    }
    const StyleScope styleScope(ctx, style);
    const fz_matrix ctm = toFz(matrix);
    const fz_rect scissor = fz_make_rect(static_cast<float>(area.x0), static_cast<float>(area.y0),
                                         static_cast<float>(area.x1), static_cast<float>(area.y1));
    const bool replace = style.recolor.mode == RecolorMode::Replace;
    SeparationsScope seps(ctx);
    mu::PixmapHandle pix(ctx);
    mu::DeviceHandle dev(ctx);
    mu::DeviceHandle wrap(ctx);
    mu::call(ctx, [&] {
        if (style.overprint && list.usesOverprint()) {
            seps.reset(fz_new_separations(ctx, 0));
        }
        fz_colorspace* cs =
            format == PixelFormat::Bgra32Premultiplied ? fz_device_bgr(ctx) : fz_device_rgb(ctx);
        pix.reset(fz_new_pixmap_with_data(ctx, cs, area.width(), area.height(), seps.get(),
                                          format == PixelFormat::Bgra32Premultiplied ? 1 : 0, stride,
                                          buffer));
        pix->x = area.x0;  // пиксель буфера (0, 0) — точка устройства (x0, y0)
        pix->y = area.y0;
        if (replace) {
            float paper[3] = {static_cast<float>((style.recolor.background >> 16) & 0xFF) / 255.0f,
                              static_cast<float>((style.recolor.background >> 8) & 0xFF) / 255.0f,
                              static_cast<float>(style.recolor.background & 0xFF) / 255.0f};
            fz_fill_pixmap_with_color(ctx, pix, fz_device_rgb(ctx), paper, fz_default_color_params);
        } else {
            fz_clear_pixmap_with_value(ctx, pix, 0xff);
        }
        dev.reset(fz_new_draw_device(ctx, fz_identity, pix));
        if (!style.smoothImages) {
            fz_enable_device_hints(ctx, dev, FZ_DONT_INTERPOLATE_IMAGES);
        }
        fz_device* run = dev.get();
        if (replace) {
            wrap.reset(newRecolorDevice(ctx, dev.get(), style.recolor));
            run = wrap.get();
        }
        fz_run_display_list(ctx, list.get(), run, ctm, scissor, cookie);
        if (cookie == nullptr || cookie->abort == 0) {
            fz_close_device(ctx, run);  // обёртка закрывает и draw device
        } else {
            // Отмена посреди списка: на стеке устройства остаются clip/group — close бросил бы «items left on
            // stack»; стек освобождает drop, а пустой close_device убирает предупреждение «dropping
            // unclosed».
            dev->close_device = nullptr;
            if (wrap.get() != nullptr) {
                wrap->close_device = nullptr;
            }
        }
    });
    if (cookie != nullptr && cookie->abort != 0) {
        throw MuError(FZ_ERROR_ABORT, "render aborted");
    }
    if (style.recolor.mode == RecolorMode::InvertAll) {
        invertPixels(buffer, area.width(), area.height(), stride, format);
    }
}

IRect Renderer::deviceBounds(int page, const Matrix& matrix) const {
    fz_context* ctx = doc_.context().get();
    mu::PageHandle p(ctx);
    fz_irect box{};
    const fz_matrix ctm = toFz(matrix);
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc_.handle(), page));
        box = fz_round_rect(fz_transform_rect(fz_bound_page(ctx, p), ctm));
    });
    return {box.x0, box.y0, box.x1, box.y1};
}

void Renderer::render(const RenderRequest& req, std::uint8_t* buffer, int stride, PixelFormat format,
                      fz_cookie* cookie) const {
    const IRect area = req.scissor.empty() ? deviceBounds(req.page, req.matrix) : req.scissor;
    const int bytesPerPixel = format == PixelFormat::Bgra32Premultiplied ? 4 : 3;
    if (area.empty() || buffer == nullptr || stride < area.width() * bytesPerPixel) {
        throw std::invalid_argument("Renderer::render: empty area, null buffer or short stride");
    }
    // Список строится с тем же cookie: отмена прерывает и интерпретацию страницы.
    const auto list = DisplayList::build(doc_, req.page, req.annotations, cookie);
    renderDisplayList(doc_.context().get(), *list, req.matrix, area, buffer, stride, format, cookie,
                      req.style);
}

Bitmap Renderer::renderPage(int page, float dpi, bool annotations, int aa) const {
    RenderRequest req;
    req.page = page;
    req.matrix = Matrix::dpi(dpi);
    req.annotations = annotations;
    req.style.textAa = aa;
    req.style.graphicsAa = aa;
    req.scissor = deviceBounds(page, req.matrix);
    Bitmap out;
    if (req.scissor.empty()) {
        return out;  // страница нулевого размера — пустой растр, как у fz_new_pixmap_from_page
    }
    out.width = req.scissor.width();
    out.height = req.scissor.height();
    out.components = 3;
    out.stride = out.width * 3;
    out.pixels.resize(static_cast<size_t>(out.stride) * static_cast<size_t>(out.height));
    render(req, out.pixels.data(), out.stride, PixelFormat::Rgb24);
    return out;
}

}  // namespace ot
