// Renderer — растр страницы в буфер вызывающего без копий (эталон 4.2): QImage(Format_ARGB32_Premultiplied)
// или Bitmap получают пиксели прямо от fz_new_pixmap_with_data → fz_clear_pixmap_with_value(0xff) →
// fz_new_draw_device → fz_run_display_list(…, scissor, cookie) → fz_close_device до fz_drop_device.
// Одна матрица на рендер, хит-тест и выделение: scale(zoom·dpr) × rotate(view) × translate(…).
// Renderer строит display list на вызов (с cookie — прерываемо); кеш списков — DisplayCache, тайлы — tiles.h,
// рендер готового списка в любом потоке своим контекстом — renderDisplayList (RenderPool).
// Поток Renderer: как у документа — один поток одновременно (контекст документа).
#pragma once

#include <cstdint>

#include "core/render/bitmap.h"
#include "core/util/geometry.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class Document;
class DisplayList;

// Замена цветов при чтении (эталон 3.1, view-30) — устройство перекраски появится в сессии 2.5b.
enum class Recolor { None };

enum class PixelFormat {
    Bgra32Premultiplied,  // 4 байта B, G, R, A — память QImage::Format_ARGB32_Premultiplied
    Rgb24,                // 3 байта R, G, B без альфы — эталоны рендера, PNG
};

struct RenderRequest {
    int page = 0;
    Matrix matrix;  // пункты страницы → пиксели устройства (Matrix::dpi(150), × rotate, × translate…)
    IRect scissor;  // область устройства = буфер (тайл); пустая — вся страница (deviceBounds)
    int aa = -1;    // сглаживание 0–8 бит; −1 — как у контекста документа
    Recolor recolor = Recolor::None;
    bool annotations = true;  // false — только содержимое страницы (печать «без пометок»)
};

class Renderer {
public:
    explicit Renderer(const Document& doc) noexcept : doc_(doc) {}

    // Рамка страницы в пикселях устройства при матрице (fz_round_rect, как fz_new_pixmap_from_page).
    IRect deviceBounds(int page, const Matrix& matrix) const;

    // Нарисовать область req.scissor (пустая — deviceBounds) в buffer: ширина × высота области,
    // stride байт на строку (≥ ширина × 4 для BGRA, × 3 для RGB). Фон белый, непрозрачный.
    // cookie (может быть nullptr): abort = 1 из другого потока прерывает рендер (MuError,
    // ErrorKind::Aborted). Ошибки MuPDF — MuError.
    void render(const RenderRequest& req, std::uint8_t* buffer, int stride,
                PixelFormat format = PixelFormat::Bgra32Premultiplied, fz_cookie* cookie = nullptr) const;

    // Вся страница при dpi в RGB-растр (эталоны, PNG, печать этапа 0).
    Bitmap renderPage(int page, float dpi, bool annotations = true, int aa = -1) const;

private:
    const Document& doc_;
};

// Нарисовать готовый display list в buffer контекстом ctx (поток рендера — свой клон: MuContext::
// forThisThread()): область area устройства при matrix, stride байт на строку, фон белый непрозрачный.
// cookie->abort = 1 из другого потока прерывает рендер (MuError, ErrorKind::Aborted); aa — как у Renderer.
void renderDisplayList(fz_context* ctx, const DisplayList& list, const Matrix& matrix, const IRect& area,
                       std::uint8_t* buffer, int stride,
                       PixelFormat format = PixelFormat::Bgra32Premultiplied, fz_cookie* cookie = nullptr,
                       int aa = -1);

}  // namespace ot
