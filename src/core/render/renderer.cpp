#include "core/render/renderer.h"

#include <stdexcept>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

namespace ot {

namespace {

fz_matrix toFz(const Matrix& m) { return {m.a, m.b, m.c, m.d, m.e, m.f}; }

// Уровень сглаживания контекста на время рендера; drop-подобное восстановление не бросает.
class AaScope {
public:
    AaScope(fz_context* ctx, int aa) noexcept : ctx_(ctx), saved_(fz_aa_level(ctx)), active_(aa >= 0) {
        if (active_) {
            fz_set_aa_level(ctx_, aa);
        }
    }
    ~AaScope() {
        if (active_) {
            fz_set_aa_level(ctx_, saved_);
        }
    }
    AaScope(const AaScope&) = delete;
    AaScope& operator=(const AaScope&) = delete;

private:
    fz_context* ctx_;
    int saved_;
    bool active_;
};

}  // namespace

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
    fz_context* ctx = doc_.context().get();
    const AaScope aa(ctx, req.aa);
    const fz_matrix ctm = toFz(req.matrix);
    const fz_rect scissor = fz_make_rect(static_cast<float>(area.x0), static_cast<float>(area.y0),
                                         static_cast<float>(area.x1), static_cast<float>(area.y1));
    mu::PageHandle page(ctx);
    mu::DisplayListHandle list(ctx);
    mu::PixmapHandle pix(ctx);
    mu::DeviceHandle dev(ctx);
    mu::call(ctx, [&] {
        page.reset(fz_load_page(ctx, doc_.handle(), req.page));
        list.reset(req.annotations ? fz_new_display_list_from_page(ctx, page)
                                   : fz_new_display_list_from_page_contents(ctx, page));
        fz_colorspace* cs =
            format == PixelFormat::Bgra32Premultiplied ? fz_device_bgr(ctx) : fz_device_rgb(ctx);
        pix.reset(fz_new_pixmap_with_data(ctx, cs, area.width(), area.height(), nullptr,
                                          format == PixelFormat::Bgra32Premultiplied ? 1 : 0, stride,
                                          buffer));
        pix->x = area.x0;  // пиксель буфера (0, 0) — точка устройства (x0, y0)
        pix->y = area.y0;
        fz_clear_pixmap_with_value(ctx, pix, 0xff);
        dev.reset(fz_new_draw_device(ctx, fz_identity, pix));
        fz_run_display_list(ctx, list, dev, ctm, scissor, cookie);
        fz_close_device(ctx, dev);
    });
    if (cookie != nullptr && cookie->abort != 0) {
        throw MuError(FZ_ERROR_ABORT, "render aborted");
    }
}

Bitmap Renderer::renderPage(int page, float dpi, bool annotations, int aa) const {
    RenderRequest req;
    req.page = page;
    req.matrix = Matrix::dpi(dpi);
    req.annotations = annotations;
    req.aa = aa;
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
