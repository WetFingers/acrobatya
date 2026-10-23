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
    // «Ещё ничего» для накопления объединением (как fz_empty_rect): x0 > x1.
    static constexpr RectF none() noexcept { return {1, 1, -1, -1}; }
    bool valid() const noexcept { return x0 <= x1 && y0 <= y1; }
};

struct PointF {
    float x = 0;
    float y = 0;
    bool operator==(const PointF&) const = default;
};

// Четырёхугольник как fz_quad: углы ul, ur, ll, lr (у повёрнутого текста — не осевой прямоугольник).
struct QuadF {
    PointF ul, ur, ll, lr;

    RectF bounds() const noexcept {
        return {std::fmin(std::fmin(ul.x, ur.x), std::fmin(ll.x, lr.x)),
                std::fmin(std::fmin(ul.y, ur.y), std::fmin(ll.y, lr.y)),
                std::fmax(std::fmax(ul.x, ur.x), std::fmax(ll.x, lr.x)),
                std::fmax(std::fmax(ul.y, ur.y), std::fmax(ll.y, lr.y))};
    }
    // Точка внутри выпуклого четырёхугольника (обход ul → ur → lr → ll, граница — внутри); вырожденный
    // (нулевой высоты) не содержит ничего.
    bool contains(PointF p) const noexcept {
        const PointF v[4] = {ul, ur, lr, ll};
        int sign = 0;
        for (int i = 0; i < 4; ++i) {
            const PointF a = v[i], b = v[(i + 1) % 4];
            const float cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
            if (cross == 0) {
                continue;
            }
            const int s = cross > 0 ? 1 : -1;
            if (sign != 0 && s != sign) {
                return false;
            }
            sign = s;
        }
        return sign != 0;
    }
};

inline bool rectContains(const RectF& r, PointF p) noexcept {
    return p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1;
}

// Расстояние от точки до прямоугольника (0 — внутри или на границе).
inline float rectDistance(const RectF& r, PointF p) noexcept {
    const float dx = std::fmax(std::fmax(r.x0 - p.x, 0.0f), p.x - r.x1);
    const float dy = std::fmax(std::fmax(r.y0 - p.y, 0.0f), p.y - r.y1);
    return std::sqrt(dx * dx + dy * dy);
}

inline RectF rectUnion(const RectF& a, const RectF& b) noexcept {
    if (!a.valid()) {
        return b;
    }
    if (!b.valid()) {
        return a;
    }
    return {std::fmin(a.x0, b.x0), std::fmin(a.y0, b.y0), std::fmax(a.x1, b.x1), std::fmax(a.y1, b.y1)};
}

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
