// Геометрия без Qt и без MuPDF: прямоугольники в пунктах (1/72 дюйма) или пикселях, аффинная матрица
// в соглашениях PDF/MuPDF (точка — строка [x y 1], concat(a, b) — сначала a, потом b, как fz_concat).
#pragma once

#include <cmath>

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

// Прямоугольник в пикселях устройства: [x0, x1) × [y0, y1).
struct IRect {
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;

    int width() const noexcept { return x1 - x0; }
    int height() const noexcept { return y1 - y0; }
    bool empty() const noexcept { return x1 <= x0 || y1 <= y0; }
    bool operator==(const IRect&) const = default;
};

struct Matrix {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;

    static Matrix identity() noexcept { return {}; }
    static Matrix scale(float sx, float sy) noexcept { return {sx, 0, 0, sy, 0, 0}; }
    static Matrix scale(float s) noexcept { return scale(s, s); }
    static Matrix translate(float tx, float ty) noexcept { return {1, 0, 0, 1, tx, ty}; }
    // Поворот на кратный 90° угол — точно, без погрешности sin/cos.
    static Matrix rotate(float degrees) noexcept {
        float deg = std::fmod(degrees, 360.0f);
        if (deg < 0) {
            deg += 360.0f;
        }
        if (deg == 0.0f) {
            return {};
        }
        if (deg == 90.0f) {
            return {0, 1, -1, 0, 0, 0};
        }
        if (deg == 180.0f) {
            return {-1, 0, 0, -1, 0, 0};
        }
        if (deg == 270.0f) {
            return {0, -1, 1, 0, 0, 0};
        }
        const float r = deg * 3.14159265358979f / 180.0f;
        const float s = std::sin(r), co = std::cos(r);
        return {co, s, -s, co, 0, 0};
    }
    // Масштаб dpi/72: пункты страницы → пиксели при dpi.
    static Matrix dpi(float dpi) noexcept { return scale(dpi / 72.0f); }
};

// Сначала l, потом r (как fz_concat(l, r)).
inline Matrix concat(const Matrix& l, const Matrix& r) noexcept {
    return {l.a * r.a + l.b * r.c, l.a * r.b + l.b * r.d,       l.c * r.a + l.d * r.c,
            l.c * r.b + l.d * r.d, l.e * r.a + l.f * r.c + r.e, l.e * r.b + l.f * r.d + r.f};
}

}  // namespace ot
