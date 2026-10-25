#include "core/annot/measure.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_set>

#include "core/annot/annot_internal.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"

namespace ot::measure {

namespace detail = annot::detail;

namespace {

struct UnitInfo {
    Unit unit;
    const char* label;
    double points;
};

constexpr double kInch = 72.0;
constexpr double kCm = kInch / 2.54;
constexpr std::array<UnitInfo, kUnitCount> kUnitTable = {{
    {Unit::Mm, "mm", kCm / 10},
    {Unit::Cm, "cm", kCm},
    {Unit::M, "m", kCm * 100},
    {Unit::Km, "km", kCm * 100000},
    {Unit::In, "in", kInch},
    {Unit::Ft, "ft", kInch * 12},
    {Unit::Yd, "yd", kInch * 36},
    {Unit::Mi, "mi", kInch * 63360},
    {Unit::Pt, "pt", 1},
}};

const UnitInfo& info(Unit unit) noexcept {
    for (const UnitInfo& u : kUnitTable) {
        if (u.unit == unit) {
            return u;
        }
    }
    return kUnitTable[1];
}

std::string_view trim(std::string_view s) noexcept {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
        s.remove_suffix(1);
    }
    return s;
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

// Число без экспоненты и лишних нулей, точкой (6 знаков после запятой — хватает метке масштаба).
std::string plain(double v) {
    const double rounded = std::round(v * 1e6) / 1e6;
    char buf[64];
    const auto res =
        std::to_chars(buf, buf + sizeof(buf), rounded == 0 ? 0.0 : rounded, std::chars_format::fixed);
    return std::string(buf, res.ptr);
}

// Число в начале s (точка или запятая); остаток — в rest.
std::optional<double> leadingNumber(std::string_view s, std::string_view& rest) {
    s = trim(s);
    std::string digits;
    std::size_t i = 0;
    while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == ',')) {
        digits.push_back(s[i] == ',' ? '.' : s[i]);
        ++i;
    }
    if (digits.empty()) {
        return std::nullopt;
    }
    double v = 0;
    const auto res = std::from_chars(digits.data(), digits.data() + digits.size(), v);
    if (res.ec != std::errc() || res.ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    rest = s.substr(i);
    return v;
}

int precisionOf(double d) noexcept {
    if (!(d >= 1)) {
        return 2;
    }
    return std::clamp(static_cast<int>(std::lround(std::log10(d))), 0, kMaxPrecision);
}

double rad(double degrees) noexcept { return degrees * std::numbers::pi / 180; }

bool finitePoint(PointF p) noexcept { return std::isfinite(p.x) && std::isfinite(p.y); }

// ------------------------------------------------------------------------------- /Measure

// Числовой формат словаря /Measure: /U, /C, /D (у /F /D — степень десяти).
struct NumberFormat {
    std::string unit;
    double factor = 1;
    double denominator = 100;
};

struct MeasureRaw {
    bool rl = false;
    std::string ratio;
    std::vector<NumberFormat> x, d, a;
};

// Строка PDF (текстовая) в UTF-8; внутри mu::call — только pdf_to_text_string (указатель в объект).
std::string textOf(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

void readFormats(fz_context* ctx, pdf_obj* arr, std::vector<NumberFormat>& out) {
    const char* units[4] = {};
    double factors[4] = {};
    double dens[4] = {};
    int n = 0;
    mu::call(ctx, [&] {
        n = std::min(pdf_array_len(ctx, arr), 4);
        for (int i = 0; i < n; ++i) {
            pdf_obj* f = pdf_array_get(ctx, arr, i);
            units[i] = pdf_to_text_string(ctx, pdf_dict_get(ctx, f, PDF_NAME(U)));
            factors[i] = pdf_dict_get_real(ctx, f, PDF_NAME(C));
            pdf_obj* den = pdf_dict_get(ctx, f, PDF_NAME(D));
            dens[i] = pdf_is_number(ctx, den) ? pdf_to_real(ctx, den) : 100;
        }
    });
    for (int i = 0; i < n; ++i) {
        out.push_back({textOf(units[i]), factors[i], dens[i]});
    }
}

// Словарь /Measure (косвенный или прямой); не словарь — rl false.
MeasureRaw readMeasure(fz_context* ctx, pdf_obj* m) {
    MeasureRaw raw;
    int isDict = 0;
    int rl = 0;
    const char* ratio = nullptr;
    pdf_obj* x = nullptr;
    pdf_obj* d = nullptr;
    pdf_obj* a = nullptr;
    mu::call(ctx, [&] {
        isDict = pdf_is_dict(ctx, m);
        if (isDict) {
            pdf_obj* sub = pdf_dict_get(ctx, m, PDF_NAME(Subtype));
            rl = sub == nullptr || pdf_name_eq(ctx, sub, PDF_NAME(RL));
            ratio = pdf_to_text_string(ctx, pdf_dict_get(ctx, m, PDF_NAME(R)));
            x = pdf_dict_gets(ctx, m, "X");
            d = pdf_dict_get(ctx, m, PDF_NAME(D));
            a = pdf_dict_get(ctx, m, PDF_NAME(A));
        }
    });
    if (!isDict) {
        return raw;
    }
    raw.rl = rl != 0;
    raw.ratio = textOf(ratio);
    readFormats(ctx, x, raw.x);
    readFormats(ctx, d, raw.d);
    readFormats(ctx, a, raw.a);
    return raw;
}

// Масштаб словаря: /R, если он разобран и сходится с /X /C × /D /C; иначе «1 cm = N единиц /D».
std::optional<Scale> scaleOf(const MeasureRaw& raw) {
    if (!raw.rl || raw.x.empty() || !(raw.x.front().factor > 0)) {
        return std::nullopt;
    }
    const NumberFormat& dist = raw.d.empty() ? raw.x.front() : raw.d.front();
    const double upp = raw.x.front().factor * (raw.d.empty() ? 1 : raw.d.front().factor);
    const std::optional<Unit> unit = unitFromLabel(dist.unit.empty() ? raw.x.front().unit : dist.unit);
    if (!unit || !(upp > 0) || !std::isfinite(upp)) {
        return std::nullopt;
    }
    const int precision = precisionOf(dist.denominator);
    if (std::optional<Scale> parsed = parseRatio(raw.ratio, precision);
        parsed && parsed->realUnit == *unit && std::fabs(parsed->unitsPerPoint() - upp) <= upp * 1e-3) {
        return parsed;
    }
    Scale s;
    s.paperValue = 1;
    s.paperUnit = Unit::Cm;
    s.realUnit = *unit;
    s.realValue = std::round(upp * pointsPerUnit(Unit::Cm) * 1e6) / 1e6;
    s.precision = precision;
    return s;
}

// Новый словарь /Measure масштаба s (внутри mu::call; строки — снаружи).
struct MeasureStrings {
    std::string ratio, unit, area;
};

pdf_obj* newFormat(fz_context* ctx, pdf_document* pdf, const char* unit, double factor, int den) {
    pdf_obj* f = pdf_new_dict(ctx, pdf, 4);
    pdf_dict_put_name(ctx, f, PDF_NAME(Type), "NumberFormat");
    pdf_dict_put_text_string(ctx, f, PDF_NAME(U), unit);
    pdf_dict_put_real(ctx, f, PDF_NAME(C), factor);
    pdf_dict_put_int(ctx, f, PDF_NAME(D), den);
    return f;
}

void putMeasure(fz_context* ctx, pdf_document* pdf, pdf_obj* annot, const MeasureStrings& s, double upp,
                int den) {
    pdf_obj* m = pdf_new_dict(ctx, pdf, 7);
    pdf_dict_puts_drop(ctx, annot, "Measure", m);
    pdf_dict_put_name(ctx, m, PDF_NAME(Type), "Measure");
    pdf_dict_put(ctx, m, PDF_NAME(Subtype), PDF_NAME(RL));
    pdf_dict_put_text_string(ctx, m, PDF_NAME(R), s.ratio.c_str());
    pdf_obj* x = pdf_new_array(ctx, pdf, 1);
    pdf_dict_puts_drop(ctx, m, "X", x);
    pdf_array_push_drop(ctx, x, newFormat(ctx, pdf, s.unit.c_str(), upp, den));
    pdf_obj* d = pdf_dict_put_array(ctx, m, PDF_NAME(D), 1);
    pdf_array_push_drop(ctx, d, newFormat(ctx, pdf, s.unit.c_str(), 1, den));
    pdf_obj* a = pdf_dict_put_array(ctx, m, PDF_NAME(A), 1);
    pdf_array_push_drop(ctx, a, newFormat(ctx, pdf, s.area.c_str(), 1, den));
    pdf_obj* o = pdf_dict_put_array(ctx, m, PDF_NAME(O), 2);
    pdf_array_push_int(ctx, o, 0);
    pdf_array_push_int(ctx, o, 0);
}

bool geometryOk(Kind kind, const std::vector<PointF>& points) {
    if (!std::all_of(points.begin(), points.end(), finitePoint)) {
        return false;
    }
    switch (kind) {
        case Kind::Distance:
            return points.size() >= 2 && length(points[0], points[1]) > 0;
        case Kind::Perimeter:
            return points.size() >= 2 && pathLength(points) > 0;
        case Kind::Area:
            return points.size() >= 3 && polygonArea(points) > 0;
    }
    return false;
}

std::optional<Kind> kindOf(annot::AnnotType type, std::string_view intent, bool hasMeasure) {
    if (type == annot::AnnotType::Line && (intent == "LineDimension" || hasMeasure)) {
        return Kind::Distance;
    }
    if (type == annot::AnnotType::PolyLine && (intent == "PolyLineDimension" || hasMeasure)) {
        return Kind::Perimeter;
    }
    if (type == annot::AnnotType::Polygon && (intent == "PolygonDimension" || hasMeasure)) {
        return Kind::Area;
    }
    return std::nullopt;
}

std::optional<Measurement> measurementOf(Document& doc, detail::PageAnnots& pa,
                                         const annot::AnnotInfo& info) {
    if (info.type != annot::AnnotType::Line && info.type != annot::AnnotType::PolyLine &&
        info.type != annot::AnnotType::Polygon) {
        return std::nullopt;
    }
    pdf_obj* obj = pa.object(info.ref.objnum);
    if (obj == nullptr) {
        return std::nullopt;
    }
    fz_context* ctx = pa.ctx();
    pdf_obj* m = nullptr;
    const char* it = nullptr;
    mu::call(ctx, [&] {
        m = pdf_dict_gets(ctx, obj, "Measure");
        it = pdf_dict_get_name(ctx, obj, PDF_NAME(IT));
    });
    const MeasureRaw raw = readMeasure(ctx, m);
    const std::optional<Kind> kind = kindOf(info.type, textOf(it), raw.rl);
    if (!kind) {
        return std::nullopt;
    }
    const std::optional<annot::AnnotProps> props = annot::readProps(doc, info.ref);
    if (!props) {
        return std::nullopt;
    }
    Measurement out;
    out.ref = info.ref;
    out.kind = *kind;
    out.caption = props->contents;
    if (*kind == Kind::Distance) {
        out.points = {props->lineStart, props->lineEnd};
    } else {
        out.points = props->vertices;
    }
    if (raw.rl && !raw.x.empty()) {
        const double x = raw.x.front().factor;
        const NumberFormat& dist = raw.d.empty() ? raw.x.front() : raw.d.front();
        const double dc = raw.d.empty() ? 1 : raw.d.front().factor;
        out.ratio = raw.ratio;
        out.unitsPerPoint = x * dc;
        if (*kind == Kind::Area) {
            const double ac = raw.a.empty() ? 1 : raw.a.front().factor;
            out.unit = raw.a.empty() ? "sq " + raw.x.front().unit : raw.a.front().unit;
            out.precision = precisionOf(raw.a.empty() ? dist.denominator : raw.a.front().denominator);
            out.value = polygonArea(out.points) * x * x * ac;
        } else {
            out.unit = dist.unit;
            out.precision = precisionOf(dist.denominator);
            out.value = pathLength(out.points) * out.unitsPerPoint;
        }
        out.hasValue = std::isfinite(out.value);
    }
    return out;
}

// ------------------------------------------------------------------------------- привязки

struct SnapCollector {
    std::vector<PointF>* out = nullptr;
    std::unordered_set<long long>* seen = nullptr;
    fz_matrix ctm = fz_identity;
    bool failed = false;

    void add(float x, float y) noexcept {
        if (failed || out->size() >= kMaxSnapPoints) {
            return;
        }
        const fz_point p = fz_transform_point(fz_make_point(x, y), ctm);
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) {
            return;
        }
        const long long kx = std::llround(static_cast<double>(p.x) * 100);
        const long long ky = std::llround(static_cast<double>(p.y) * 100);
        const long long key = kx * 4000003LL + ky;
        try {
            if (seen->insert(key).second) {
                out->push_back({p.x, p.y});
            }
        } catch (...) {
            failed = true;  // исключение не должно пересечь кадры MuPDF
        }
    }
};

struct SnapDevice {
    fz_device super;
    SnapCollector* collector;
};

void walkMove(fz_context*, void* arg, float x, float y) { static_cast<SnapCollector*>(arg)->add(x, y); }
void walkLine(fz_context*, void* arg, float x, float y) { static_cast<SnapCollector*>(arg)->add(x, y); }
void walkCurve(fz_context*, void* arg, float, float, float, float, float x3, float y3) {
    static_cast<SnapCollector*>(arg)->add(x3, y3);
}
void walkClose(fz_context*, void*) {}
void walkQuad(fz_context*, void* arg, float, float, float x2, float y2) {
    static_cast<SnapCollector*>(arg)->add(x2, y2);
}
void walkCurveV(fz_context*, void* arg, float, float, float x3, float y3) {
    static_cast<SnapCollector*>(arg)->add(x3, y3);
}
void walkRect(fz_context*, void* arg, float x1, float y1, float x2, float y2) {
    auto* c = static_cast<SnapCollector*>(arg);
    c->add(x1, y1);
    c->add(x2, y1);
    c->add(x2, y2);
    c->add(x1, y2);
}

constexpr fz_path_walker kWalker = {walkMove, walkLine,   walkCurve,  walkClose,
                                    walkQuad, walkCurveV, walkCurveV, walkRect};

void collectPath(fz_context* ctx, fz_device* dev, const fz_path* path, fz_matrix ctm) {
    SnapCollector* c = reinterpret_cast<SnapDevice*>(dev)->collector;
    c->ctm = ctm;
    fz_walk_path(ctx, path, &kWalker, c);
}

void snapFill(fz_context* ctx, fz_device* dev, const fz_path* path, int, fz_matrix ctm, fz_colorspace*,
              const float*, float, fz_color_params) {
    collectPath(ctx, dev, path, ctm);
}

void snapStroke(fz_context* ctx, fz_device* dev, const fz_path* path, const fz_stroke_state*, fz_matrix ctm,
                fz_colorspace*, const float*, float, fz_color_params) {
    collectPath(ctx, dev, path, ctm);
}

}  // namespace

// ---------------------------------------------------------------------------------- единицы и масштаб

double pointsPerUnit(Unit unit) noexcept { return info(unit).points; }

std::string_view unitLabel(Unit unit) noexcept { return info(unit).label; }

std::optional<Unit> unitFromLabel(std::string_view label) noexcept {
    std::string s;
    try {
        s = lower(trim(label));
    } catch (...) {
        return std::nullopt;
    }
    if (s.rfind("sq ", 0) == 0) {
        s = std::string(trim(std::string_view(s).substr(3)));
    }
    for (const UnitInfo& u : kUnitTable) {
        if (s == u.label) {
            return u.unit;
        }
    }
    if (s == "inch" || s == "inches" || s == "\"") {
        return Unit::In;
    }
    if (s == "foot" || s == "feet" || s == "'") {
        return Unit::Ft;
    }
    if (s == "yard" || s == "yards") {
        return Unit::Yd;
    }
    if (s == "mile" || s == "miles") {
        return Unit::Mi;
    }
    if (s == "point" || s == "points") {
        return Unit::Pt;
    }
    return std::nullopt;
}

bool Scale::valid() const noexcept {
    return std::isfinite(paperValue) && std::isfinite(realValue) && paperValue > 0 && realValue > 0 &&
           precision >= 0 && precision <= kMaxPrecision;
}

double Scale::unitsPerPoint() const noexcept { return realValue / (paperValue * pointsPerUnit(paperUnit)); }

std::string Scale::ratio() const {
    return plain(paperValue) + " " + std::string(unitLabel(paperUnit)) + " = " + plain(realValue) + " " +
           std::string(unitLabel(realUnit));
}

Scale calibrate(double lengthPt, double known, Unit unit, int precision, Unit paperUnit) {
    if (!(lengthPt > 0) || !(known > 0) || !std::isfinite(lengthPt) || !std::isfinite(known)) {
        throw std::invalid_argument("calibration needs a positive length and value");
    }
    Scale s;
    s.paperValue = 1;
    s.paperUnit = paperUnit;
    s.realUnit = unit;
    s.realValue = std::round(known * pointsPerUnit(paperUnit) / lengthPt * 1e6) / 1e6;
    s.precision = std::clamp(precision, 0, kMaxPrecision);
    return s;
}

std::optional<Scale> parseRatio(std::string_view ratio, int precision) {
    const std::size_t eq = ratio.find('=');
    if (eq == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest;
    const std::optional<double> paper = leadingNumber(ratio.substr(0, eq), rest);
    const std::optional<Unit> paperUnit = paper ? unitFromLabel(rest) : std::nullopt;
    const std::optional<double> real = leadingNumber(ratio.substr(eq + 1), rest);
    const std::optional<Unit> realUnit = real ? unitFromLabel(rest) : std::nullopt;
    if (!paperUnit || !realUnit) {
        return std::nullopt;
    }
    Scale s;
    s.paperValue = *paper;
    s.paperUnit = *paperUnit;
    s.realValue = *real;
    s.realUnit = *realUnit;
    s.precision = std::clamp(precision, 0, kMaxPrecision);
    if (!s.valid()) {
        return std::nullopt;
    }
    return s;
}

annot::AnnotType typeOf(Kind kind) noexcept {
    switch (kind) {
        case Kind::Distance:
            return annot::AnnotType::Line;
        case Kind::Perimeter:
            return annot::AnnotType::PolyLine;
        case Kind::Area:
            return annot::AnnotType::Polygon;
    }
    return annot::AnnotType::Line;
}

std::string_view intentOf(Kind kind) noexcept {
    switch (kind) {
        case Kind::Distance:
            return "LineDimension";
        case Kind::Perimeter:
            return "PolyLineDimension";
        case Kind::Area:
            return "PolygonDimension";
    }
    return {};
}

std::string createStepName(Kind kind) {
    switch (kind) {
        case Kind::Distance:
            return "Add distance";
        case Kind::Perimeter:
            return "Add perimeter";
        case Kind::Area:
            return "Add area";
    }
    return "Add measurement";
}

// ---------------------------------------------------------------------------------- геометрия

double length(PointF a, PointF b) noexcept {
    return std::hypot(static_cast<double>(b.x) - a.x, static_cast<double>(b.y) - a.y);
}

double pathLength(const std::vector<PointF>& points, bool closed) noexcept {
    double total = 0;
    for (std::size_t i = 1; i < points.size(); ++i) {
        total += length(points[i - 1], points[i]);
    }
    if (closed && points.size() > 2) {
        total += length(points.back(), points.front());
    }
    return total;
}

double polygonArea(const std::vector<PointF>& points) noexcept {
    if (points.size() < 3) {
        return 0;
    }
    double twice = 0;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const PointF a = points[i];
        const PointF b = points[(i + 1) % points.size()];
        twice += static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y;
    }
    return std::fabs(twice) / 2;
}

double angle(PointF a, PointF b) noexcept {
    const double dx = static_cast<double>(b.x) - a.x;
    const double dy = static_cast<double>(b.y) - a.y;
    if (dx == 0 && dy == 0) {
        return 0;
    }
    double deg = std::atan2(-dy, dx) * 180 / std::numbers::pi;
    if (deg < 0) {
        deg += 360;
    }
    return deg >= 360 ? deg - 360 : deg;
}

PointF constrain(PointF from, PointF to, double stepDegrees) noexcept {
    const double dx = static_cast<double>(to.x) - from.x;
    const double dy = static_cast<double>(to.y) - from.y;
    if (!(stepDegrees > 0) || (dx == 0 && dy == 0)) {
        return to;
    }
    const double step = rad(stepDegrees);
    const double snapped = std::round(std::atan2(dy, dx) / step) * step;
    const double len = dx * std::cos(snapped) + dy * std::sin(snapped);
    return {static_cast<float>(from.x + len * std::cos(snapped)),
            static_cast<float>(from.y + len * std::sin(snapped))};
}

double value(Kind kind, const std::vector<PointF>& points, const Scale& scale) noexcept {
    const double u = scale.unitsPerPoint();
    switch (kind) {
        case Kind::Distance:
            return points.size() < 2 ? 0 : length(points[0], points[1]) * u;
        case Kind::Perimeter:
            return pathLength(points) * u;
        case Kind::Area:
            return polygonArea(points) * u * u;
    }
    return 0;
}

std::string formatNumber(double value, int precision, char decimalPoint) {
    precision = std::clamp(precision, 0, kMaxPrecision);
    if (!std::isfinite(value)) {
        return "-";
    }
    const double f = std::pow(10.0, precision);
    const double scaled = value * f;
    // Округление от нуля; 1e-7 — запас на двоичное представление (2,675 → 2,68).
    double r = std::round(scaled + (scaled >= 0 ? 1e-7 : -1e-7)) / f;
    if (r == 0) {
        r = 0;  // −0 → 0
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, r);
    std::string out(buf);
    if (decimalPoint != '.') {
        std::replace(out.begin(), out.end(), '.', decimalPoint);
    }
    return out;
}

// ---------------------------------------------------------------------------------- пометки

annot::AnnotRef create(Document& doc, int page, const MeasureProps& props, const std::string& operation) {
    detail::pdfOf(doc);
    if (page < 0 || page >= doc.pageCount()) {
        detail::badArgument("page " + std::to_string(page) + " is out of range");
    }
    if (!props.scale.valid()) {
        detail::badArgument("measurement scale is invalid");
    }
    if (!geometryOk(props.kind, props.points)) {
        detail::badArgument("measurement geometry is empty");
    }
    annot::AnnotProps p = props.style;
    p.type = typeOf(props.kind);
    p.intent = std::string(intentOf(props.kind));
    p.contents = props.caption;
    p.richContents.clear();
    if (p.color.empty()) {
        p.color = annot::AnnotColor::rgb(1, 0, 0);
    }
    if (props.kind == Kind::Distance) {
        p.lineStart = props.points[0];
        p.lineEnd = props.points[1];
        p.vertices.clear();
    } else {
        p.vertices = props.points;
    }
    MeasureStrings strings;
    strings.ratio = props.scale.ratio();
    strings.unit = std::string(unitLabel(props.scale.realUnit));
    strings.area = "sq " + strings.unit;
    const double upp = props.scale.unitsPerPoint();
    const int den = static_cast<int>(std::lround(std::pow(10.0, props.scale.precision)));

    detail::PageAnnots pa(doc, page);
    Journal::Operation op(doc, operation.empty() ? createStepName(props.kind) : operation);
    const annot::AnnotRef ref = detail::createIn(doc, pa, p, 0, nullptr);
    pdf_annot* a = pa.find(ref.objnum);
    if (a == nullptr) {
        detail::notFound(ref);
    }
    fz_context* ctx = pa.ctx();
    pdf_document* pdf = pa.pdf();
    const bool line = props.kind == Kind::Distance;
    const float ll = props.leaderLength;
    const float lle = props.leaderExtension;
    const float llo = props.leaderOffset;
    const bool top = props.captionTop;
    mu::call(ctx, [&] {
        pdf_obj* obj = pdf_annot_obj(ctx, a);
        putMeasure(ctx, pdf, obj, strings, upp, den);
        if (line) {
            if (ll != 0) {
                pdf_set_annot_line_leader(ctx, a, ll);
                pdf_set_annot_line_leader_extension(ctx, a, lle);
                pdf_set_annot_line_leader_offset(ctx, a, llo);
            }
            pdf_set_annot_line_caption(ctx, a, 1);
            if (top) {
                pdf_dict_put(ctx, obj, PDF_NAME(CP), PDF_NAME(Top));
            }
        }
    });
    detail::updateAppearance(doc, pa, ref.objnum, true);
    if (line) {
        // Подпись /Cap MuPDF пишет шрифтом base14 без дескриптора (кириллица — HelvCYR).
        detail::completeBase14Fonts(ctx, pdf, pa.object(ref.objnum));
    }
    op.invalidate(page);
    op.commit();
    return ref;
}

std::optional<Measurement> readMeasurement(Document& doc, const annot::AnnotRef& ref) {
    if (!ref.valid() || doc.pdf() == nullptr || ref.page >= doc.pageCount()) {
        return std::nullopt;
    }
    const std::optional<annot::AnnotInfo> info = annot::findAnnot(doc, ref);
    if (!info) {
        return std::nullopt;
    }
    detail::PageAnnots pa(doc, ref.page);
    return measurementOf(doc, pa, *info);
}

std::vector<Measurement> listMeasurements(Document& doc) {
    std::vector<Measurement> out;
    if (doc.pdf() == nullptr) {
        return out;
    }
    const int pages = doc.pageCount();
    for (int page = 0; page < pages; ++page) {
        const std::vector<annot::AnnotInfo> annots = annot::listAnnots(doc, page);
        if (std::none_of(annots.begin(), annots.end(), [](const annot::AnnotInfo& a) {
                return a.type == annot::AnnotType::Line || a.type == annot::AnnotType::PolyLine ||
                       a.type == annot::AnnotType::Polygon;
            })) {
            continue;
        }
        detail::PageAnnots pa(doc, page);
        for (const annot::AnnotInfo& info : annots) {
            if (std::optional<Measurement> m = measurementOf(doc, pa, info)) {
                out.push_back(std::move(*m));
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------- масштаб документа

std::vector<Viewport> viewports(Document& doc, int page) {
    std::vector<Viewport> out;
    if (doc.pdf() == nullptr) {
        return out;
    }
    detail::PageAnnots pa(doc, page);
    if (pa.page() == nullptr) {
        return out;
    }
    fz_context* ctx = pa.ctx();
    const fz_matrix ctm = pa.ctm();
    pdf_obj* vp = nullptr;
    int n = 0;
    mu::call(ctx, [&] {
        vp = pdf_dict_gets(ctx, pa.page()->obj, "VP");
        n = pdf_array_len(ctx, vp);
    });
    for (int i = 0; i < n; ++i) {
        pdf_obj* entry = nullptr;
        pdf_obj* m = nullptr;
        fz_rect box{};
        const char* name = nullptr;
        mu::call(ctx, [&] {
            entry = pdf_array_get(ctx, vp, i);
            box = pdf_dict_get_rect(ctx, entry, PDF_NAME(BBox));
            name = pdf_to_text_string(ctx, pdf_dict_get(ctx, entry, PDF_NAME(Name)));
            m = pdf_dict_gets(ctx, entry, "Measure");
        });
        const std::optional<Scale> scale = scaleOf(readMeasure(ctx, m));
        if (!scale || fz_is_empty_rect(box)) {
            continue;
        }
        const fz_rect r = fz_transform_rect(box, ctm);
        out.push_back({{r.x0, r.y0, r.x1, r.y1}, textOf(name), *scale});
    }
    return out;
}

std::optional<Scale> viewportScale(const std::vector<Viewport>& viewports, PointF point) {
    for (auto it = viewports.rbegin(); it != viewports.rend(); ++it) {
        const RectF& b = it->bbox;
        if (point.x >= b.x0 && point.x <= b.x1 && point.y >= b.y0 && point.y <= b.y1) {
            return it->scale;
        }
    }
    return std::nullopt;
}

std::optional<Scale> viewportScale(Document& doc, int page, PointF point) {
    return viewportScale(viewports(doc, page), point);
}

// ---------------------------------------------------------------------------------- привязки

std::vector<PointF> snapPoints(Document& doc, int page) {
    std::vector<PointF> points;
    if (page < 0 || page >= doc.pageCount()) {
        throw MuError(FZ_ERROR_ARGUMENT, "page " + std::to_string(page) + " is out of range");
    }
    std::unordered_set<long long> seen;
    SnapCollector collector;
    collector.out = &points;
    collector.seen = &seen;
    fz_context* ctx = doc.context().get();
    fz_document* fz = doc.handle();
    mu::PageHandle pageHandle(ctx);
    mu::DeviceHandle device(ctx);
    SnapCollector* c = &collector;
    mu::call(ctx, [&] {
        pageHandle.reset(fz_load_page(ctx, fz, page));
        SnapDevice* dev = fz_new_derived_device(ctx, SnapDevice);
        dev->collector = c;
        dev->super.fill_path = snapFill;
        dev->super.stroke_path = snapStroke;
        device.reset(&dev->super);
        fz_run_page_contents(ctx, pageHandle, device, fz_identity, nullptr);
        fz_close_device(ctx, device);
    });
    return points;
}

SnapIndex::SnapIndex(std::vector<PointF> points, float cell)
    : points_(std::move(points)), cell_(cell > 0 ? cell : 16) {
    for (std::size_t i = 0; i < points_.size(); ++i) {
        const int cx = static_cast<int>(std::floor(points_[i].x / cell_));
        const int cy = static_cast<int>(std::floor(points_[i].y / cell_));
        grid_[key(cx, cy)].push_back(i);
    }
}

long long SnapIndex::key(int cx, int cy) noexcept {
    return (static_cast<long long>(cx) << 32) ^ static_cast<long long>(static_cast<unsigned int>(cy));
}

std::optional<PointF> SnapIndex::nearest(PointF p, float radius) const {
    if (points_.empty() || !(radius >= 0) || !finitePoint(p)) {
        return std::nullopt;
    }
    const int x0 = static_cast<int>(std::floor((p.x - radius) / cell_));
    const int x1 = static_cast<int>(std::floor((p.x + radius) / cell_));
    const int y0 = static_cast<int>(std::floor((p.y - radius) / cell_));
    const int y1 = static_cast<int>(std::floor((p.y + radius) / cell_));
    double best = static_cast<double>(radius) * radius;
    std::optional<PointF> found;
    for (int cx = x0; cx <= x1; ++cx) {
        for (int cy = y0; cy <= y1; ++cy) {
            const auto it = grid_.find(key(cx, cy));
            if (it == grid_.end()) {
                continue;
            }
            for (const std::size_t i : it->second) {
                const double dx = static_cast<double>(points_[i].x) - p.x;
                const double dy = static_cast<double>(points_[i].y) - p.y;
                const double d2 = dx * dx + dy * dy;
                if (d2 <= best) {
                    best = d2;
                    found = points_[i];
                }
            }
        }
    }
    return found;
}

}  // namespace ot::measure
