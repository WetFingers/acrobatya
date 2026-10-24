#include "core/render/recolor_device.h"

#include <algorithm>

namespace ot {

namespace {

// Устройство MuPDF — C-структура: fz_new_device_of_size обнуляет память, поля ниже — только скаляры.
struct RecolorDevice {
    fz_device super;
    fz_device* target;
    Recolor rc;
    int maskDepth;  // > 0 — рисуется содержимое мягкой маски: цвета не трогать
};

RecolorDevice* self(fz_device* dev) { return reinterpret_cast<RecolorDevice*>(dev); }

// Заменить цвет, если нужно: true — out (RGB) готов для target.
bool replaced(fz_context* ctx, RecolorDevice* d, bool text, fz_colorspace* cs, const float* color,
              fz_color_params cp, float out[3]) {
    if (d->maskDepth > 0 || d->rc.mode != RecolorMode::Replace || cs == nullptr ||
        (!text && !d->rc.lineArt)) {
        return false;
    }
    float rgb[3] = {0, 0, 0};
    fz_convert_color(ctx, cs, color, fz_device_rgb(ctx), rgb, nullptr, cp);
    recolorRgb(d->rc, rgb, out);
    return true;
}

void closeDevice(fz_context* ctx, fz_device* dev) { fz_close_device(ctx, self(dev)->target); }

void fillPath(fz_context* ctx, fz_device* dev, const fz_path* path, int evenOdd, fz_matrix ctm,
              fz_colorspace* cs, const float* color, float alpha, fz_color_params cp) {
    RecolorDevice* d = self(dev);
    float rgb[3];
    if (replaced(ctx, d, false, cs, color, cp, rgb)) {
        fz_fill_path(ctx, d->target, path, evenOdd, ctm, fz_device_rgb(ctx), rgb, alpha, cp);
    } else {
        fz_fill_path(ctx, d->target, path, evenOdd, ctm, cs, color, alpha, cp);
    }
}

void strokePath(fz_context* ctx, fz_device* dev, const fz_path* path, const fz_stroke_state* stroke,
                fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha, fz_color_params cp) {
    RecolorDevice* d = self(dev);
    float rgb[3];
    if (replaced(ctx, d, false, cs, color, cp, rgb)) {
        fz_stroke_path(ctx, d->target, path, stroke, ctm, fz_device_rgb(ctx), rgb, alpha, cp);
    } else {
        fz_stroke_path(ctx, d->target, path, stroke, ctm, cs, color, alpha, cp);
    }
}

void clipPath(fz_context* ctx, fz_device* dev, const fz_path* path, int evenOdd, fz_matrix ctm,
              fz_rect scissor) {
    fz_clip_path(ctx, self(dev)->target, path, evenOdd, ctm, scissor);
}

void clipStrokePath(fz_context* ctx, fz_device* dev, const fz_path* path, const fz_stroke_state* stroke,
                    fz_matrix ctm, fz_rect scissor) {
    fz_clip_stroke_path(ctx, self(dev)->target, path, stroke, ctm, scissor);
}

void fillText(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix ctm, fz_colorspace* cs,
              const float* color, float alpha, fz_color_params cp) {
    RecolorDevice* d = self(dev);
    float rgb[3];
    if (replaced(ctx, d, true, cs, color, cp, rgb)) {
        fz_fill_text(ctx, d->target, text, ctm, fz_device_rgb(ctx), rgb, alpha, cp);
    } else {
        fz_fill_text(ctx, d->target, text, ctm, cs, color, alpha, cp);
    }
}

void strokeText(fz_context* ctx, fz_device* dev, const fz_text* text, const fz_stroke_state* stroke,
                fz_matrix ctm, fz_colorspace* cs, const float* color, float alpha, fz_color_params cp) {
    RecolorDevice* d = self(dev);
    float rgb[3];
    if (replaced(ctx, d, true, cs, color, cp, rgb)) {
        fz_stroke_text(ctx, d->target, text, stroke, ctm, fz_device_rgb(ctx), rgb, alpha, cp);
    } else {
        fz_stroke_text(ctx, d->target, text, stroke, ctm, cs, color, alpha, cp);
    }
}

void clipText(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix ctm, fz_rect scissor) {
    fz_clip_text(ctx, self(dev)->target, text, ctm, scissor);
}

void clipStrokeText(fz_context* ctx, fz_device* dev, const fz_text* text, const fz_stroke_state* stroke,
                    fz_matrix ctm, fz_rect scissor) {
    fz_clip_stroke_text(ctx, self(dev)->target, text, stroke, ctm, scissor);
}

void ignoreText(fz_context* ctx, fz_device* dev, const fz_text* text, fz_matrix ctm) {
    fz_ignore_text(ctx, self(dev)->target, text, ctm);
}

void fillShade(fz_context* ctx, fz_device* dev, fz_shade* shade, fz_matrix ctm, float alpha,
               fz_color_params cp) {
    fz_fill_shade(ctx, self(dev)->target, shade, ctm, alpha, cp);
}

void fillImage(fz_context* ctx, fz_device* dev, fz_image* image, fz_matrix ctm, float alpha,
               fz_color_params cp) {
    fz_fill_image(ctx, self(dev)->target, image, ctm, alpha, cp);
}

// Маска-картинка рисуется цветом (глифы Type3, штампы) — как текст.
void fillImageMask(fz_context* ctx, fz_device* dev, fz_image* image, fz_matrix ctm, fz_colorspace* cs,
                   const float* color, float alpha, fz_color_params cp) {
    RecolorDevice* d = self(dev);
    float rgb[3];
    if (replaced(ctx, d, true, cs, color, cp, rgb)) {
        fz_fill_image_mask(ctx, d->target, image, ctm, fz_device_rgb(ctx), rgb, alpha, cp);
    } else {
        fz_fill_image_mask(ctx, d->target, image, ctm, cs, color, alpha, cp);
    }
}

void clipImageMask(fz_context* ctx, fz_device* dev, fz_image* image, fz_matrix ctm, fz_rect scissor) {
    fz_clip_image_mask(ctx, self(dev)->target, image, ctm, scissor);
}

void popClip(fz_context* ctx, fz_device* dev) { fz_pop_clip(ctx, self(dev)->target); }

void beginMask(fz_context* ctx, fz_device* dev, fz_rect area, int luminosity, fz_colorspace* cs,
               const float* bc, fz_color_params cp) {
    RecolorDevice* d = self(dev);
    fz_begin_mask(ctx, d->target, area, luminosity, cs, bc, cp);
    ++d->maskDepth;
}

void endMask(fz_context* ctx, fz_device* dev, fz_function* fn) {
    RecolorDevice* d = self(dev);
    d->maskDepth = std::max(0, d->maskDepth - 1);
    fz_end_mask_tr(ctx, d->target, fn);
}

void beginGroup(fz_context* ctx, fz_device* dev, fz_rect area, fz_colorspace* cs, int isolated, int knockout,
                int blendmode, float alpha) {
    fz_begin_group(ctx, self(dev)->target, area, cs, isolated, knockout, blendmode, alpha);
}

void endGroup(fz_context* ctx, fz_device* dev) { fz_end_group(ctx, self(dev)->target); }

int beginTile(fz_context* ctx, fz_device* dev, fz_rect area, fz_rect view, float xstep, float ystep,
              fz_matrix ctm, int id, int docId) {
    return fz_begin_tile_tid(ctx, self(dev)->target, area, view, xstep, ystep, ctm, id, docId);
}

void endTile(fz_context* ctx, fz_device* dev) { fz_end_tile(ctx, self(dev)->target); }

void renderFlags(fz_context* ctx, fz_device* dev, int set, int clear) {
    fz_render_flags(ctx, self(dev)->target, set, clear);
}

void setDefaultColorspaces(fz_context* ctx, fz_device* dev, fz_default_colorspaces* cs) {
    fz_set_default_colorspaces(ctx, self(dev)->target, cs);
}

void beginLayer(fz_context* ctx, fz_device* dev, const char* name) {
    fz_begin_layer(ctx, self(dev)->target, name);
}

void endLayer(fz_context* ctx, fz_device* dev) { fz_end_layer(ctx, self(dev)->target); }

void beginStructure(fz_context* ctx, fz_device* dev, fz_structure standard, const char* raw, int idx) {
    fz_begin_structure(ctx, self(dev)->target, standard, raw, idx);
}

void endStructure(fz_context* ctx, fz_device* dev) { fz_end_structure(ctx, self(dev)->target); }

void beginMetatext(fz_context* ctx, fz_device* dev, fz_metatext meta, const char* text) {
    fz_begin_metatext(ctx, self(dev)->target, meta, text);
}

void endMetatext(fz_context* ctx, fz_device* dev) { fz_end_metatext(ctx, self(dev)->target); }

}  // namespace

fz_device* newRecolorDevice(fz_context* ctx, fz_device* target, const Recolor& rc) {
    auto* d = reinterpret_cast<RecolorDevice*>(fz_new_device_of_size(ctx, sizeof(RecolorDevice)));
    d->target = target;
    d->rc = rc;
    d->maskDepth = 0;
    fz_device& s = d->super;
    s.close_device = closeDevice;
    s.fill_path = fillPath;
    s.stroke_path = strokePath;
    s.clip_path = clipPath;
    s.clip_stroke_path = clipStrokePath;
    s.fill_text = fillText;
    s.stroke_text = strokeText;
    s.clip_text = clipText;
    s.clip_stroke_text = clipStrokeText;
    s.ignore_text = ignoreText;
    s.fill_shade = fillShade;
    s.fill_image = fillImage;
    s.fill_image_mask = fillImageMask;
    s.clip_image_mask = clipImageMask;
    s.pop_clip = popClip;
    s.begin_mask = beginMask;
    s.end_mask = endMask;
    s.begin_group = beginGroup;
    s.end_group = endGroup;
    s.begin_tile = beginTile;
    s.end_tile = endTile;
    s.render_flags = renderFlags;
    s.set_default_colorspaces = setDefaultColorspaces;
    s.begin_layer = beginLayer;
    s.end_layer = endLayer;
    s.begin_structure = beginStructure;
    s.end_structure = endStructure;
    s.begin_metatext = beginMetatext;
    s.end_metatext = endMetatext;
    return &d->super;
}

}  // namespace ot
