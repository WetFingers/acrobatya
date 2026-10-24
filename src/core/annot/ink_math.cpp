#include "core/annot/ink_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ot::annot {

namespace ink_math_detail {

float dist(PointF a, PointF b) noexcept { return std::hypot(a.x - b.x, a.y - b.y); }

void simplifyRange(const Stroke& pts, std::size_t first, std::size_t last, float epsilon,
                   std::vector<bool>& keep) {
    if (last <= first + 1) {
        return;
    }
    float worst = -1.0f;
    std::size_t index = first;
    for (std::size_t i = first + 1; i < last; ++i) {
        const float d = distanceToSegment(pts[i], pts[first], pts[last]);
        if (d > worst) {
            worst = d;
            index = i;
        }
    }
    if (worst > epsilon) {
        keep[index] = true;
        simplifyRange(pts, first, index, epsilon, keep);
        simplifyRange(pts, index, last, epsilon, keep);
    }
}

// Параметр t ∈ [0, 1] точки выхода отрезка a→b из круга (c, r), когда a внутри: ближайший корень > 0.
float exitParam(PointF a, PointF b, PointF c, float r) noexcept {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float fx = a.x - c.x;
    const float fy = a.y - c.y;
    const float qa = dx * dx + dy * dy;
    if (qa <= 0) {
        return 1.0f;
    }
    const float qb = 2 * (fx * dx + fy * dy);
    const float qc = fx * fx + fy * fy - r * r;
    const float disc = qb * qb - 4 * qa * qc;
    if (disc < 0) {
        return 1.0f;
    }
    const float t = (-qb + std::sqrt(disc)) / (2 * qa);
    return std::clamp(t, 0.0f, 1.0f);
}

PointF lerp(PointF a, PointF b, float t) noexcept { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

// Путь ластика уплотнён: шаг не больше radius / 2.
Stroke densify(const Stroke& path, float radius) {
    Stroke out;
    if (path.empty()) {
        return out;
    }
    const float step = std::max(radius / 2, 0.25f);
    out.push_back(path.front());
    for (std::size_t i = 1; i < path.size(); ++i) {
        const float d = dist(path[i - 1], path[i]);
        const int parts = std::max(1, static_cast<int>(std::ceil(d / step)));
        for (int k = 1; k <= parts; ++k) {
            out.push_back(lerp(path[i - 1], path[i], static_cast<float>(k) / static_cast<float>(parts)));
        }
    }
    return out;
}

// Штрих, уплотнённый так, чтобы граница круга ластика легла между точками не дальше step.
Stroke densifyStroke(const Stroke& stroke, float step) {
    Stroke out;
    if (stroke.empty()) {
        return out;
    }
    out.push_back(stroke.front());
    for (std::size_t i = 1; i < stroke.size(); ++i) {
        const float d = dist(stroke[i - 1], stroke[i]);
        const int parts = std::max(1, static_cast<int>(std::ceil(d / step)));
        for (int k = 1; k <= parts; ++k) {
            out.push_back(lerp(stroke[i - 1], stroke[i], static_cast<float>(k) / static_cast<float>(parts)));
        }
    }
    return out;
}

}  // namespace ink_math_detail

float distanceToSegment(PointF p, PointF a, PointF b) noexcept {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len2 = dx * dx + dy * dy;
    if (len2 <= 0) {
        return ink_math_detail::dist(p, a);
    }
    const float t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0f, 1.0f);
    return ink_math_detail::dist(p, {a.x + t * dx, a.y + t * dy});
}

float distanceToPolyline(PointF p, const Stroke& line, bool closed) noexcept {
    if (line.empty()) {
        return std::numeric_limits<float>::infinity();
    }
    if (line.size() == 1) {
        return ink_math_detail::dist(p, line.front());
    }
    float best = std::numeric_limits<float>::infinity();
    for (std::size_t i = 1; i < line.size(); ++i) {
        best = std::min(best, distanceToSegment(p, line[i - 1], line[i]));
    }
    if (closed && line.size() > 2) {
        best = std::min(best, distanceToSegment(p, line.back(), line.front()));
    }
    return best;
}

bool pointInPolygon(PointF p, const Stroke& polygon) noexcept {
    bool inside = false;
    const std::size_t n = polygon.size();
    if (n < 3) {
        return false;
    }
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const PointF a = polygon[i];
        const PointF b = polygon[j];
        if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)) {
            inside = !inside;
        }
    }
    return inside;
}

float distanceToQuad(PointF p, const QuadF& quad) noexcept {
    const Stroke polygon = {quad.ul, quad.ur, quad.lr, quad.ll};
    if (pointInPolygon(p, polygon)) {
        return 0.0f;
    }
    return distanceToPolyline(p, polygon, true);
}

Stroke simplifyStroke(const Stroke& points, float epsilon) {
    if (points.size() < 3) {
        return points;
    }
    std::vector<bool> keep(points.size(), false);
    keep.front() = true;
    keep.back() = true;
    ink_math_detail::simplifyRange(points, 0, points.size() - 1, epsilon, keep);
    Stroke out;
    for (std::size_t i = 0; i < points.size(); ++i) {
        if (keep[i]) {
            out.push_back(points[i]);
        }
    }
    return out;
}

std::vector<Stroke> eraseStrokes(const std::vector<Stroke>& strokes, const Stroke& path, float radius,
                                 bool* changed) {
    using namespace ink_math_detail;
    if (changed != nullptr) {
        *changed = false;
    }
    if (path.empty() || radius <= 0) {
        return strokes;
    }
    const Stroke centers = densify(path, radius);
    auto erased = [&](PointF p) {
        return std::any_of(centers.begin(), centers.end(), [&](PointF c) { return dist(p, c) <= radius; });
    };
    // Ближайший центр, чей круг содержит p.
    auto circleOf = [&](PointF p) -> PointF {
        PointF best = centers.front();
        float bestD = std::numeric_limits<float>::infinity();
        for (const PointF c : centers) {
            const float d = dist(p, c);
            if (d < bestD) {
                bestD = d;
                best = c;
            }
        }
        return best;
    };
    std::vector<Stroke> out;
    for (const Stroke& source : strokes) {
        const Stroke stroke = densifyStroke(source, std::max(radius / 4, 0.25f));
        const std::size_t before = out.size();
        bool touched = false;
        Stroke piece;
        auto flush = [&] {
            if (piece.size() >= 2) {
                out.push_back(simplifyStroke(piece, 0.01f));
            }
            piece.clear();
        };
        for (std::size_t i = 0; i < stroke.size(); ++i) {
            const PointF p = stroke[i];
            const bool gone = erased(p);
            if (gone) {
                touched = true;
                if (!piece.empty()) {
                    // Выход из круга: отрезок до границы.
                    const PointF prev = piece.back();
                    const PointF c = circleOf(p);
                    const float t = exitParam(p, prev, c, radius);
                    piece.push_back(lerp(p, prev, t));
                    flush();
                }
                continue;
            }
            if (piece.empty() && i > 0 && erased(stroke[i - 1])) {
                // Вход из круга: начать с границы.
                const PointF prev = stroke[i - 1];
                const float t = exitParam(prev, p, circleOf(prev), radius);
                piece.push_back(lerp(prev, p, t));
            }
            piece.push_back(p);
        }
        flush();
        if (!touched) {
            out.resize(before);
            out.push_back(source);  // не задет — исходные точки без уплотнения
        } else if (changed != nullptr) {
            *changed = true;
        }
    }
    return out;
}

}  // namespace ot::annot
