#include "core/render/bitmap.h"

#include <stdexcept>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/paths.h"

namespace ot {

namespace {

Bitmap fromPixmap(fz_context* ctx, fz_pixmap* pix) {
    Bitmap b;
    b.width = fz_pixmap_width(ctx, pix);
    b.height = fz_pixmap_height(ctx, pix);
    b.components = fz_pixmap_components(ctx, pix);
    b.stride = b.width * b.components;
    b.pixels.resize(static_cast<size_t>(b.stride) * static_cast<size_t>(b.height));
    const unsigned char* src = fz_pixmap_samples(ctx, pix);
    const auto srcStride = static_cast<size_t>(fz_pixmap_stride(ctx, pix));
    for (int y = 0; y < b.height; ++y) {
        std::memcpy(b.row(y), src + srcStride * static_cast<size_t>(y), static_cast<size_t>(b.stride));
    }
    return b;
}

}  // namespace

Bitmap loadPng(MuContext& mu, const std::filesystem::path& path) {
    const std::string bytes = paths::readFile(path);
    fz_context* ctx = mu.get();
    fz_buffer* buf = nullptr;
    fz_image* img = nullptr;
    fz_pixmap* raw = nullptr;
    fz_pixmap* rgb = nullptr;
    Bitmap out;
    try {
        mu::call(ctx, [&] {
            buf = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(bytes.data()),
                                                 bytes.size());
            img = fz_new_image_from_buffer(ctx, buf);
            raw = fz_get_pixmap_from_image(ctx, img, nullptr, nullptr, nullptr, nullptr);
            if (fz_pixmap_colorspace(ctx, raw) != fz_device_rgb(ctx) || fz_pixmap_alpha(ctx, raw)) {
                rgb = fz_convert_pixmap(ctx, raw, fz_device_rgb(ctx), nullptr, nullptr,
                                        fz_default_color_params, 0);
            }
        });
        out = fromPixmap(ctx, rgb ? rgb : raw);
    } catch (...) {
        fz_drop_pixmap(ctx, rgb);
        fz_drop_pixmap(ctx, raw);
        fz_drop_image(ctx, img);
        fz_drop_buffer(ctx, buf);
        throw;
    }
    fz_drop_pixmap(ctx, rgb);
    fz_drop_pixmap(ctx, raw);
    fz_drop_image(ctx, img);
    fz_drop_buffer(ctx, buf);
    return out;
}

std::string encodePng(MuContext& mu, const Bitmap& bitmap) {
    if (bitmap.empty()) {
        throw std::invalid_argument("encodePng: empty bitmap");
    }
    fz_context* ctx = mu.get();
    fz_pixmap* pix = nullptr;
    fz_buffer* png = nullptr;
    std::string out;
    try {
        mu::call(ctx, [&] {
            fz_colorspace* cs = bitmap.components == 1 ? fz_device_gray(ctx) : fz_device_rgb(ctx);
            pix = fz_new_pixmap(ctx, cs, bitmap.width, bitmap.height, nullptr, 0);
        });
        unsigned char* dst = fz_pixmap_samples(ctx, pix);
        const auto dstStride = static_cast<size_t>(fz_pixmap_stride(ctx, pix));
        for (int y = 0; y < bitmap.height; ++y) {
            std::memcpy(dst + dstStride * static_cast<size_t>(y), bitmap.row(y),
                        static_cast<size_t>(bitmap.width * bitmap.components));
        }
        mu::call(ctx, [&] { png = fz_new_buffer_from_pixmap_as_png(ctx, pix, fz_default_color_params); });
        unsigned char* data = nullptr;
        const size_t len = fz_buffer_storage(ctx, png, &data);
        out.assign(reinterpret_cast<const char*>(data), len);
    } catch (...) {
        fz_drop_buffer(ctx, png);
        fz_drop_pixmap(ctx, pix);
        throw;
    }
    fz_drop_buffer(ctx, png);
    fz_drop_pixmap(ctx, pix);
    return out;
}

void savePng(MuContext& mu, const Bitmap& bitmap, const std::filesystem::path& path) {
    paths::writeFile(path, encodePng(mu, bitmap));
}

}  // namespace ot
