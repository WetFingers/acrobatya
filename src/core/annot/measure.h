// Измерения (план 5.5, п. 1; эталон 3.4 «Измерения»; comment-29, comment-30): масштаб и единицы, вычисления,
// пометки-измерения и масштаб документа.
//   * масштаб — «paperValue paperUnit на странице = realValue realUnit» (Acrobat «Масштаб и точность»: 1 см =
//     10 м), точность 0–4 знака; калибровка — по отрезку известной длины;
//   * расстояние — /Line /IT /LineDimension с выносными линиями (/LL /LLE /LLO) и подписью на линии (/Cap
//   true,
//     текст — /Contents: «12,5 см» формирует ui), периметр — /PolyLine /IT /PolyLineDimension, площадь —
//     /Polygon /IT /PolygonDimension; у каждой — свой словарь /Measure (ISO 32000 12.9: /Type /Measure
//     /Subtype /RL /R (1 cm = 10 m) /X [<< /U (m) /C единиц-на-пункт /D 10^точность >>] /D [...] /A [<< /U
//     (sq m) >>] /O [0 0]); единицы в файле — английские метки Acrobat (mm, cm, m, km, in, ft, yd, mi, pt);
//   * масштаб документа — видовые экраны страницы /VP (/BBox в пунктах PDF, /Measure /RL): точку покрывают
//     несколько — берётся последний в массиве (12.9.2); незнакомые единицы и /GEO — не масштаб;
//   * значение пометки читается из её геометрии и /Measure (так же у пометок Acrobat), подпись — /Contents;
//   * привязки — вершины и концы путей содержимого страницы (moveto/lineto/curveto, углы re; не управляющие
//     точки кривых) в координатах страницы; SnapIndex — ближайшая в радиусе по сетке.
// Геометрия — координаты страницы MuPDF (пункты, y вниз, с /Rotate и /CropBox), как annots.h: длины и площади
// от поворота не зависят. /UserUnit не учитывается (пункт = 1/72 дюйма).
#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/annot/annots.h"
#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::measure {

enum class Unit { Mm, Cm, M, Km, In, Ft, Yd, Mi, Pt };
inline constexpr int kUnitCount = 9;
inline constexpr std::array<Unit, kUnitCount> kUnits = {Unit::Mm, Unit::Cm, Unit::M,  Unit::Km, Unit::In,
                                                        Unit::Ft, Unit::Yd, Unit::Mi, Unit::Pt};

// Пунктов в одной единице длины (1 см = 72 / 2,54 пт).
double pointsPerUnit(Unit unit) noexcept;
// Метка единицы в файле (/U, /R): "mm", "cm", "m", "km", "in", "ft", "yd", "mi", "pt".
std::string_view unitLabel(Unit unit) noexcept;
// Единица по метке файла без учёта регистра и пробелов по краям ("in", "inch", "ft", "feet", "m"…); площадь
// "sq m" — единица длины m. Незнакомая — nullopt.
std::optional<Unit> unitFromLabel(std::string_view label) noexcept;

inline constexpr int kMaxPrecision = 4;

struct Scale {
    double paperValue = 1;
    Unit paperUnit = Unit::Cm;
    double realValue = 1;
    Unit realUnit = Unit::Cm;
    int precision = 2;  // знаков после запятой, 0–kMaxPrecision

    bool valid() const noexcept;
    // Единиц realUnit в одном пункте страницы.
    double unitsPerPoint() const noexcept;
    // /R: "1 cm = 10 m" (числа — без лишних нулей, точкой).
    std::string ratio() const;
    bool operator==(const Scale&) const = default;
};

// Калибровка: отрезок lengthPt пунктов на странице — known единиц unit; масштаб «1 paperUnit = N unit».
// lengthPt или known ≤ 0 — std::invalid_argument.
Scale calibrate(double lengthPt, double known, Unit unit, int precision, Unit paperUnit = Unit::Cm);

// "1 cm = 10 m" → масштаб (точность — precision); не разобрано — nullopt.
std::optional<Scale> parseRatio(std::string_view ratio, int precision = 2);

enum class Kind { Distance, Perimeter, Area };

// Line / PolyLine / Polygon.
annot::AnnotType typeOf(Kind kind) noexcept;
// /IT: "LineDimension", "PolyLineDimension", "PolygonDimension".
std::string_view intentOf(Kind kind) noexcept;
// Шаг истории: "Add distance", "Add perimeter", "Add area".
std::string createStepName(Kind kind);

// ---------------------------------------------------------------- геометрия (пункты страницы)

double length(PointF a, PointF b) noexcept;
// Длина ломаной; closed — с отрезком от последней точки к первой.
double pathLength(const std::vector<PointF>& points, bool closed = false) noexcept;
// Площадь многоугольника (по модулю, формула площади Гаусса).
double polygonArea(const std::vector<PointF>& points) noexcept;
// Угол от a к b в градусах 0–360 против часовой стрелки, как видит глаз (y страницы — вниз).
double angle(PointF a, PointF b) noexcept;
// Ограничение направления от from к to шагом stepDegrees (90 — орто, 45 — Shift): ближайшее направление,
// длина — проекция на него.
PointF constrain(PointF from, PointF to, double stepDegrees) noexcept;

// Значение в единицах масштаба: Distance — длина первых двух точек, Perimeter — длина ломаной, Area — площадь
// в квадратных единицах. Мало точек — 0.
double value(Kind kind, const std::vector<PointF>& points, const Scale& scale) noexcept;
// Число с precision знаками (округление от нуля) и десятичным разделителем decimalPoint: 3.5278 → "3,53";
// −0 — "0".
std::string formatNumber(double value, int precision, char decimalPoint = '.');

// ---------------------------------------------------------------- пометки-измерения

struct MeasureProps {
    Kind kind = Kind::Distance;
    std::vector<PointF> points;  // Distance — 2, Perimeter — ≥ 2, Area — ≥ 3 (координаты страницы)
    Scale scale;
    std::string caption;  // /Contents (Distance — подпись на линии /Cap)
    // Стиль: цвет, толщина, непрозрачность, автор, концы линии…; тип, геометрия и /IT — из kind и points.
    annot::AnnotProps style;
    float leaderLength = 0;     // /LL (Distance; 0 — без выносных линий)
    float leaderExtension = 0;  // /LLE
    float leaderOffset = 0;     // /LLO
    bool captionTop = false;    // /CP /Top (иначе в разрыве линии)
};

// Пометка-измерение на странице page одной операцией журнала (пусто — createStepName). Мало точек, нулевая
// длина, неверный масштаб — MuError(FZ_ERROR_ARGUMENT).
annot::AnnotRef create(Document& doc, int page, const MeasureProps& props, const std::string& operation = {});

// Измерение в файле: Line / PolyLine / Polygon с /IT …Dimension или со словарём /Measure.
struct Measurement {
    annot::AnnotRef ref;
    Kind kind = Kind::Distance;
    std::vector<PointF> points;  // координаты страницы
    double value = 0;            // в единицах unit; нет /Measure — 0 и hasValue false
    bool hasValue = false;
    std::string unit;          // метка /D (/A для площади) как в файле: "m", "sq m"
    std::string ratio;         // /R
    double unitsPerPoint = 0;  // /X /C × /D /C
    int precision = 2;
    std::string caption;  // /Contents
};

std::optional<Measurement> readMeasurement(Document& doc, const annot::AnnotRef& ref);
// Все измерения документа по страницам в порядке /Annots.
std::vector<Measurement> listMeasurements(Document& doc);

// ---------------------------------------------------------------- масштаб документа (/VP)

struct Viewport {
    RectF bbox;  // координаты страницы
    std::string name;
    Scale scale;
};

// Видовые экраны страницы с масштабом /RL знакомых единиц (порядок /VP).
std::vector<Viewport> viewports(Document& doc, int page);
// Масштаб в точке: последний экран /VP, который её покрывает; нет — nullopt.
std::optional<Scale> viewportScale(Document& doc, int page, PointF point);
std::optional<Scale> viewportScale(const std::vector<Viewport>& viewports, PointF point);

// ---------------------------------------------------------------- привязки

// Вершины и концы путей содержимого страницы (без пометок): концы moveto/lineto/curveto, углы re; совпавшие
// с точностью до 0,01 пт — одна. Больше kMaxSnapPoints — первые.
inline constexpr std::size_t kMaxSnapPoints = 200000;
std::vector<PointF> snapPoints(Document& doc, int page);

class SnapIndex {
public:
    SnapIndex() = default;
    explicit SnapIndex(std::vector<PointF> points, float cell = 16);

    // Ближайшая точка не дальше radius; нет — nullopt.
    std::optional<PointF> nearest(PointF p, float radius) const;
    std::size_t size() const noexcept { return points_.size(); }
    bool empty() const noexcept { return points_.empty(); }

private:
    static long long key(int cx, int cy) noexcept;

    std::vector<PointF> points_;
    float cell_ = 16;
    std::unordered_map<long long, std::vector<std::size_t>> grid_;
};

}  // namespace ot::measure
