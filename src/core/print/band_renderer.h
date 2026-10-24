// Растр печати полосами (план 2.6; эталон 3.3 «Механика»; view-37, view-42, view-43): страница листа
// (imposition.h) рисуется MuPDF при DPI печати горизонтальными полосами — fz_run_display_list со scissor
// полосы (renderDisplayList), и каждая полоса уходит получателю (print_job: QPainter::drawImage в точный
// QRectF устройства). Печать — всегда растром (ответ на вопрос 12 (10): «только как изображение и dpi»).
//   * Display list печати — usage "Print": слои по их текущему состоянию и /Usage /Print /PrintState
//     (скрытые не печатаются), пометки — только с флагом Print (pdf_run_annot_with_usage). «Комментарии и
//     формы»: «Документ» — содержимое и поля форм, «Документ и пометки» — ещё комментарии, «Только поля» —
//     одни виджеты (pdf_run_page_*_with_usage). Не PDF (XPS, EPUB, картинки) — fz_run_page, «только поля» —
//     пусто.
//   * Полоса вместе с копией, которую драйвер делает из QImage, — не больше 64 МБ: сама полоса ≤ 32 МиБ
//     (A4 при 600 dpi в RGB — 4 полосы), высота — поровну.
//   * «Серый» — растр в fz_device_gray (Gray8, 1 байт на пиксель: и памяти, и данных принтеру втрое меньше),
//     цвет — Rgb24; бумага белая.
// Поток: display list строит поток документа (DocumentSession), рисует — любой поток контекстом той же семьи
// (MuContext::forThisThread()); отмена — cookie->abort (MuError Aborted).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "core/print/imposition.h"
#include "core/render/renderer.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {
class Document;
class DisplayList;
}  // namespace ot

namespace ot::print {

inline constexpr std::size_t kMaxBandBytes = std::size_t{32} << 20;

// Display list страницы для печати (поток документа). cookie: abort = 1 прерывает (MuError Aborted).
std::shared_ptr<const DisplayList> buildPrintList(const Document& doc, int page, PrintContent content,
                                                  fz_cookie* cookie = nullptr);

struct BandOptions {
    float dpi = 300;
    bool grayscale = false;
    std::size_t maxBandBytes = kMaxBandBytes;
};

struct Band {
    IRect area;  // пиксели листа при dpi (от левого верхнего угла бумаги)
    PixelFormat format = PixelFormat::Rgb24;
    const std::uint8_t* pixels = nullptr;
    int stride = 0;
    int index = 0;  // номер полосы размещения и их число
    int count = 1;
};

// Область листа в пикселях при dpi: края — round (соседние ячейки n-up не налезают друг на друга).
IRect deviceRect(const RectF& sheetRect, float dpi) noexcept;

// Разрезать область на полосы ≤ maxBytes (строки по bytesPerPixel × ширина, выровнены по 4 байта):
// поровну, сверху вниз; пустая область — пусто; строка шире предела — по строке.
std::vector<IRect> splitBands(const IRect& area, int bytesPerPixel, std::size_t maxBytes);

// Нарисовать размещение листа полосами: list — display list страницы placement.page (buildPrintList), ctx —
// контекст семьи документа этого потока. sink получает каждую полосу (буфер живёт до возврата sink).
// Пустое место (page < 0) и пустой clip — ничего.
void renderPlacement(fz_context* ctx, const DisplayList& list, const Placement& placement,
                     const BandOptions& options, fz_cookie* cookie,
                     const std::function<void(const Band&)>& sink);

}  // namespace ot::print
