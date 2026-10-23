// Масштаб вида и раскладка страниц (эталон 4.2, HiDPI — 6.1; view-04 — расчёт). Масштаб zoom — логических
// пикселей на пункт: матрица тайлов — scale(zoom · dpr) (tiles.h). Проценты интерфейса — относительно
// «реального размера» монитора: 100 % = dpi / 72 логических пикселей на пункт (dpi — разрешение экрана в
// логических пикселях на дюйм: системное 96 · масштаб или по физическому DPI монитора — выбирает вызывающий).
// Раскладка знает только размеры страниц: пустые (0 × 0, NaN) — как Letter у MuPDF (612 × 792 pt), больше
// 200 × 200 дюймов (14 400 pt, предел PDF) — уменьшаются с сохранением пропорций.
#pragma once

#include "core/util/geometry.h"

namespace ot {

enum class FitMode {
    ActualSize,  // реальный размер: 100 % по DPI монитора
    Page,        // страница целиком
    Width,       // по ширине
    Height,      // по высоте
    Visible,     // по ширине видимого содержимого (bbox контента)
};

// Область окна просмотра в логических пикселях (без полей между страницами) и разрешение экрана.
struct Viewport {
    float width = 0;
    float height = 0;
    float dpi = 96.0f;  // логических пикселей на дюйм
};

inline constexpr float kMinZoomPercent = 1.0f;     // как у Acrobat: 1 %
inline constexpr float kMaxZoomPercent = 6400.0f;  // … 6400 %
inline constexpr float kMaxPagePoints = 14400.0f;  // 200 дюймов
inline constexpr float kDefaultPageWidth = 612.0f;
inline constexpr float kDefaultPageHeight = 792.0f;

// Масштаб для режима: pageRect — рамка страницы в пунктах (с учётом /Rotate), rotation — поворот вида;
// content — bbox содержимого (для Visible; пустой — как Width). Результат ограничен 1 %…6400 %.
float fitZoom(const RectF& pageRect, const Viewport& viewport, FitMode mode, int rotation = 0,
              const RectF& content = {});

// Проценты интерфейса ↔ масштаб при разрешении экрана dpi.
float zoomToPercent(float zoom, float dpi = 96.0f) noexcept;
float percentToZoom(float percent, float dpi = 96.0f) noexcept;
// Ограничить масштаб 1 %…6400 %.
float clampZoom(float zoom, float dpi = 96.0f) noexcept;

struct LayoutSize {
    float width = kDefaultPageWidth;  // пункты
    float height = kDefaultPageHeight;
    float scale = 1.0f;      // раскладка / настоящий размер (меньше 1 — страница уменьшена до 14 400 pt)
    bool defaulted = false;  // размер был пустым или нечисловым — взят Letter
    bool clamped = false;    // больше 14 400 pt — уменьшено
};

// Размер страницы для раскладки (см. выше); сторона не меньше 1 pt.
LayoutSize layoutPageSize(float width, float height) noexcept;

}  // namespace ot
