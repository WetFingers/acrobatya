// Геометрия пометок без MuPDF (план 5.1; эталон 3.4 «Рисовать», «Выделение и правка на странице»): упрощение
// штриха карандаша Дугласом–Пекером (ε = 0,5 пт — точки, которые пишутся в /InkList), ластик — разрезание
// штрихов по кругу вокруг пути курсора, расстояния для хит-теста по типу (штрих, ломаная, многоугольник,
// квады разметки). Координаты — пункты страницы (как AnnotInfo::rect); поток — любой.
#pragma once

#include <vector>

#include "core/util/geometry.h"

namespace ot::annot {

using Stroke = std::vector<PointF>;

inline constexpr float kInkSimplifyEpsilon = 0.5f;  // пт: допуск Дугласа–Пекера для /InkList

// Дуглас–Пекер: точки, отклонение которых от хорды не больше epsilon, выбрасываются; концы остаются. Меньше
// трёх точек — как есть.
Stroke simplifyStroke(const Stroke& points, float epsilon = kInkSimplifyEpsilon);

// Ластик: из штрихов выбрасываются части внутри кругов радиуса radius вокруг точек пути (путь между точками
// уплотняется — быстрый взмах не перескакивает штрих); штрих режется на куски, кусок из одной точки
// отбрасывается. Отрезок штриха, пересекающий круг, укорачивается до его границы. changed — что-то стёрто.
std::vector<Stroke> eraseStrokes(const std::vector<Stroke>& strokes, const Stroke& path, float radius,
                                 bool* changed = nullptr);

// Расстояние от точки до отрезка / ломаной (одна точка — до неё; пусто — бесконечность).
float distanceToSegment(PointF p, PointF a, PointF b) noexcept;
float distanceToPolyline(PointF p, const Stroke& line, bool closed = false) noexcept;
// Точка внутри многоугольника (правило чётности).
bool pointInPolygon(PointF p, const Stroke& polygon) noexcept;
// Расстояние до четырёхугольника (0 — внутри).
float distanceToQuad(PointF p, const QuadF& quad) noexcept;

}  // namespace ot::annot
