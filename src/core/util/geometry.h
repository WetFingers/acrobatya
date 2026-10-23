// Геометрия без Qt и без MuPDF: прямоугольник в пунктах (1/72 дюйма) или пикселях.
#pragma once

namespace ot {

struct RectF {
    float x0 = 0;
    float y0 = 0;
    float x1 = 0;
    float y1 = 0;

    float width() const noexcept { return x1 - x0; }
    float height() const noexcept { return y1 - y0; }
    bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
};

}  // namespace ot
