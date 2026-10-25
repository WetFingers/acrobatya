// unit.measure — измерения ядра (план 5.5, п. 1; эталон 3.4 «Измерения»; comment-29, comment-30): масштаб «1
// см = 10 м» и единицы, расстояние, периметр и площадь в единицах масштаба, форматирование с запятой,
// калибровка по отрезку, угол и орто/45°; пометки-измерения — Line /LineDimension с выносными линиями и
// подписью /Cap, PolyLine /PolyLineDimension, Polygon /PolygonDimension со своим /Measure — переживают
// инкрементальную запись и читаются обратно со значением; масштаб документа из /VP фикстуры
// tests/fixtures/annots/measure-viewport.pdf (ожидания measure.json генератора tools/gen/make_measure.py) —
// по точке, последний экран; готовые пометки Acrobat читаются со значениями; привязка — к вершинам и концам
// путей в радиусе, но не к углам рамки диагонали, не к управляющим точкам кривой и не вне радиуса.
//
// План («Сначала тесты»): «расстояние двух точек при 1 см = 10 м даёт 3,54 м для 100 pt» — арифметика плана
// не сходится: 100 пт = 3,5278 см, при 1 см = 10 м это 35,28 м, а 3,53 м — при 1 см = 1 м. Проверяются оба
// верных значения (решение — в журнале сессии 5.5).
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/annot/annots.h"
#include "core/annot/measure.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
namespace measure = ot::measure;
using measure::Kind;
using measure::Scale;
using measure::Unit;
using ot::Document;
using ot::PointF;

namespace {

constexpr double kCm = 72.0 / 2.54;

fs::path fixture() {
    return ot::test::sourceDir() / "tests" / "fixtures" / "annots" / "measure-viewport.pdf";
}

const nlohmann::json& expected() {
    static const nlohmann::json all = [] {
        std::ifstream in(ot::test::sourceDir() / "tests" / "fixtures" / "annots" / "measure.json",
                         std::ios::binary);
        return nlohmann::json::parse(in)["files"]["measure-viewport.pdf"];
    }();
    return all;
}

PointF pointOf(const nlohmann::json& p) { return {p[0].get<float>(), p[1].get<float>()}; }

Scale scale(double paper, Unit paperUnit, double real, Unit realUnit, int precision = 2) {
    Scale s;
    s.paperValue = paper;
    s.paperUnit = paperUnit;
    s.realValue = real;
    s.realUnit = realUnit;
    s.precision = precision;
    return s;
}

std::string printed(fz_context* ctx, pdf_obj* o) {
    if (o == nullptr) {
        return {};
    }
    size_t len = 0;
    char* s = pdf_sprint_obj(ctx, nullptr, 0, &len, o, 1, 0);
    std::string out(s, len);
    fz_free(ctx, s);
    return out;
}

// Ключ словаря аннотации текстом.
std::string keyOf(Document& doc, int objnum, const char* path) {
    std::string out;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    pdf_obj* value = nullptr;
    ot::mu::call(ctx, [&] {
        pdf_obj* a = pdf_load_object(ctx, pdf, objnum);
        value = pdf_keep_obj(ctx, pdf_dict_getp(ctx, a, path));
        pdf_drop_obj(ctx, a);
    });
    out = printed(ctx, value);
    pdf_drop_obj(ctx, value);
    return out;
}

bool hasAppearance(Document& doc, int objnum) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int stream = 0;
    ot::mu::call(ctx, [&] {
        pdf_obj* a = pdf_load_object(ctx, pdf, objnum);
        stream = pdf_is_stream(ctx, pdf_dict_getp(ctx, a, "AP/N"));
        pdf_drop_obj(ctx, a);
    });
    return stream != 0;
}

}  // namespace

TEST(measure, units_and_labels) {
    OT_FEATURE("comment-30");
    EXPECT_NEAR(measure::pointsPerUnit(Unit::Cm), kCm, 1e-9);
    EXPECT_NEAR(measure::pointsPerUnit(Unit::In), 72, 1e-9);
    EXPECT_NEAR(measure::pointsPerUnit(Unit::Ft), 864, 1e-9);
    EXPECT_NEAR(measure::pointsPerUnit(Unit::Mi), 72.0 * 63360, 1e-6);
    EXPECT_NEAR(measure::pointsPerUnit(Unit::Km), kCm * 100000, 1e-3);
    EXPECT_EQ(measure::pointsPerUnit(Unit::Pt), 1);
    for (const Unit u : measure::kUnits) {
        const auto back = measure::unitFromLabel(measure::unitLabel(u));
        ASSERT_TRUE(back) << measure::unitLabel(u);
        EXPECT_EQ(*back, u);
    }
    EXPECT_EQ(measure::unitFromLabel(" Inch "), Unit::In);
    EXPECT_EQ(measure::unitFromLabel("feet"), Unit::Ft);
    EXPECT_EQ(measure::unitFromLabel("sq m"), Unit::M);
    EXPECT_FALSE(measure::unitFromLabel("furlong"));
}

TEST(measure, distance_with_scale) {
    OT_FEATURE("comment-29");
    OT_FEATURE("comment-30");
    const Scale tenMeters = scale(1, Unit::Cm, 10, Unit::M);
    ASSERT_TRUE(tenMeters.valid());
    EXPECT_EQ(tenMeters.ratio(), "1 cm = 10 m");
    // 100 пт = 3,5278 см → 35,28 м при 1 см = 10 м.
    const double d = measure::value(Kind::Distance, {{100, 100}, {200, 100}}, tenMeters);
    EXPECT_NEAR(d, 35.2778, 1e-3);
    EXPECT_EQ(measure::formatNumber(d, 2, ','), "35,28");
    // 1 см = 1 м → 3,53 м (диагональ 60 × 80 = 100 пт).
    const Scale oneMeter = scale(1, Unit::Cm, 1, Unit::M);
    const double diag = measure::value(Kind::Distance, {{0, 0}, {60, 80}}, oneMeter);
    EXPECT_NEAR(diag, 3.5278, 1e-3);
    EXPECT_EQ(measure::formatNumber(diag, 2, ','), "3,53");
    EXPECT_EQ(measure::formatNumber(diag, 0, ','), "4");
    EXPECT_EQ(measure::formatNumber(diag, 4, '.'), "3.5278");
    EXPECT_EQ(measure::formatNumber(-0.001, 2, ','), "0,00");
    EXPECT_EQ(measure::formatNumber(2.5, 0, ','), "3");
    // Масштаб 1 : 1 в дюймах: 72 пт = 1 in.
    EXPECT_NEAR(measure::value(Kind::Distance, {{0, 0}, {72, 0}}, scale(1, Unit::In, 1, Unit::In)), 1, 1e-9);
    EXPECT_FALSE(scale(0, Unit::Cm, 1, Unit::M).valid());
    EXPECT_FALSE(scale(1, Unit::Cm, -1, Unit::M).valid());
}

TEST(measure, perimeter_of_triangle) {
    OT_FEATURE("comment-29");
    // Египетский треугольник 300-400-500 пт, путь замкнут щелчком в первую вершину.
    const std::vector<PointF> path = {{0, 0}, {300, 0}, {0, 400}, {0, 0}};
    EXPECT_NEAR(measure::pathLength(path), 1200, 1e-6);
    EXPECT_NEAR(measure::pathLength({{0, 0}, {300, 0}, {0, 400}}, true), 1200, 1e-6);
    const Scale inches = scale(1, Unit::In, 1, Unit::In, 3);
    EXPECT_NEAR(measure::value(Kind::Perimeter, path, inches), 1200.0 / 72, 1e-9);
    EXPECT_EQ(measure::formatNumber(measure::value(Kind::Perimeter, path, inches), 3, ','), "16,667");
    EXPECT_EQ(measure::value(Kind::Perimeter, {{0, 0}}, inches), 0);
}

TEST(measure, area_of_rectangle_in_square_meters) {
    OT_FEATURE("comment-29");
    // 10 × 5 см на странице при 1 см = 1 м — 50 м².
    const float w = static_cast<float>(10 * kCm);
    const float h = static_cast<float>(5 * kCm);
    const std::vector<PointF> rect = {{100, 100}, {100 + w, 100}, {100 + w, 100 + h}, {100, 100 + h}};
    EXPECT_NEAR(measure::polygonArea(rect), w * h, 1e-2);
    EXPECT_NEAR(measure::value(Kind::Area, rect, scale(1, Unit::Cm, 1, Unit::M)), 50, 1e-3);
    // Обход в другую сторону — та же площадь.
    const std::vector<PointF> reversed(rect.rbegin(), rect.rend());
    EXPECT_NEAR(measure::value(Kind::Area, reversed, scale(1, Unit::Cm, 1, Unit::M)), 50, 1e-3);
    // 1 см = 2 м — площадь вчетверо больше.
    EXPECT_NEAR(measure::value(Kind::Area, rect, scale(1, Unit::Cm, 2, Unit::M)), 200, 1e-2);
}

TEST(measure, calibrate_by_known_segment) {
    OT_FEATURE("comment-30");
    // Отрезок 5 см на странице — 25 м: 1 см = 5 м.
    const Scale s = measure::calibrate(5 * kCm, 25, Unit::M, 1);
    EXPECT_EQ(s.paperUnit, Unit::Cm);
    EXPECT_NEAR(s.paperValue, 1, 1e-9);
    EXPECT_EQ(s.realUnit, Unit::M);
    EXPECT_NEAR(s.realValue, 5, 1e-6);
    EXPECT_EQ(s.precision, 1);
    EXPECT_EQ(s.ratio(), "1 cm = 5 m");
    EXPECT_THROW(measure::calibrate(0, 25, Unit::M, 1), std::invalid_argument);
    const auto parsed = measure::parseRatio("1 in = 10 ft", 1);
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->paperUnit, Unit::In);
    EXPECT_EQ(parsed->realUnit, Unit::Ft);
    EXPECT_NEAR(parsed->unitsPerPoint(), 10.0 / 72, 1e-9);
    EXPECT_FALSE(measure::parseRatio("one inch = ten feet"));
}

TEST(measure, angle_and_constraints) {
    EXPECT_NEAR(measure::angle({0, 0}, {10, 0}), 0, 1e-9);
    EXPECT_NEAR(measure::angle({0, 0}, {10, -10}), 45, 1e-9);  // y вниз: вверх-вправо — 45°
    EXPECT_NEAR(measure::angle({0, 0}, {0, 10}), 270, 1e-9);
    const PointF ortho = measure::constrain({0, 0}, {100, 20}, 90);
    EXPECT_NEAR(ortho.x, 100, 1e-3);
    EXPECT_NEAR(ortho.y, 0, 1e-3);
    const PointF vertical = measure::constrain({0, 0}, {10, 90}, 90);
    EXPECT_NEAR(vertical.x, 0, 1e-3);
    EXPECT_NEAR(vertical.y, 90, 1e-3);
    const PointF diag = measure::constrain({0, 0}, {100, 90}, 45);
    EXPECT_NEAR(diag.x, diag.y, 1e-3);
    EXPECT_NEAR(diag.x, 95, 1e-3);  // проекция на 45°
}

TEST(measure, distance_annotation_survives_save) {
    OT_FEATURE("comment-29");
    OT_FEATURE("comment-30");
    ot::test::TempDir dir;
    const fs::path path = dir.path() / "measure.pdf";
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), 1));
    std::string nm;
    {
        auto doc = Document::openOrThrow(path);
        measure::MeasureProps props;
        props.kind = Kind::Distance;
        props.points = {{100, 100}, {100 + static_cast<float>(1.25 * kCm), 100}};
        props.scale = scale(1, Unit::Cm, 10, Unit::Cm, 1);
        props.caption = "12,5 см";
        props.style.color = ot::annot::AnnotColor::rgb(1, 0, 0);
        props.style.author = "Tester";
        props.leaderLength = 6;
        props.leaderExtension = 3;
        props.leaderOffset = 1;
        const int before = doc->undoStack().size();
        const auto ref = measure::create(*doc, 0, props);
        EXPECT_EQ(doc->undoStack().size(), before + 1);
        nm = ref.nm;
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    auto doc = Document::openOrThrow(path);
    const auto all = measure::listMeasurements(*doc);
    ASSERT_EQ(all.size(), 1u);
    const measure::Measurement& m = all.front();
    EXPECT_EQ(m.ref.nm, nm);
    EXPECT_EQ(m.kind, Kind::Distance);
    ASSERT_TRUE(m.hasValue);
    EXPECT_NEAR(m.value, 12.5, 1e-3);
    EXPECT_EQ(m.unit, "cm");
    EXPECT_EQ(m.ratio, "1 cm = 10 cm");
    EXPECT_EQ(m.precision, 1);
    EXPECT_EQ(m.caption, "12,5 см");
    const int obj = m.ref.objnum;
    EXPECT_EQ(keyOf(*doc, obj, "Subtype"), "/Line");
    EXPECT_EQ(keyOf(*doc, obj, "IT"), "/LineDimension");
    EXPECT_EQ(keyOf(*doc, obj, "Cap"), "true");
    EXPECT_EQ(keyOf(*doc, obj, "LL"), "6");
    EXPECT_EQ(keyOf(*doc, obj, "LLE"), "3");
    EXPECT_EQ(keyOf(*doc, obj, "LLO"), "1");
    EXPECT_EQ(keyOf(*doc, obj, "Measure/Type"), "/Measure");
    EXPECT_EQ(keyOf(*doc, obj, "Measure/Subtype"), "/RL");
    EXPECT_NE(keyOf(*doc, obj, "Measure/X").find("/C"), std::string::npos);
    EXPECT_NE(keyOf(*doc, obj, "Measure/D").find("/D 10"), std::string::npos);
    EXPECT_TRUE(hasAppearance(*doc, obj));
}

TEST(measure, perimeter_and_area_annotations) {
    OT_FEATURE("comment-29");
    auto doc = Document::createBlank(1);
    const Scale s = scale(1, Unit::Cm, 1, Unit::M);
    const float w = static_cast<float>(4 * kCm);
    const float h = static_cast<float>(3 * kCm);
    measure::MeasureProps perim;
    perim.kind = Kind::Perimeter;
    perim.points = {{100, 100}, {100 + w, 100}, {100 + w, 100 + h}};
    perim.scale = s;
    perim.caption = "7,00 m";
    const auto pref = measure::create(*doc, 0, perim);
    measure::MeasureProps area;
    area.kind = Kind::Area;
    area.points = {{100, 300}, {100 + w, 300}, {100 + w, 300 + h}, {100, 300 + h}};
    area.scale = s;
    area.caption = "12,00 m2";
    const auto aref = measure::create(*doc, 0, area);
    EXPECT_EQ(keyOf(*doc, pref.objnum, "Subtype"), "/PolyLine");
    EXPECT_EQ(keyOf(*doc, pref.objnum, "IT"), "/PolyLineDimension");
    EXPECT_EQ(keyOf(*doc, aref.objnum, "Subtype"), "/Polygon");
    EXPECT_EQ(keyOf(*doc, aref.objnum, "IT"), "/PolygonDimension");
    EXPECT_NE(keyOf(*doc, aref.objnum, "Measure/A").find("(sq m)"), std::string::npos);
    const auto p = measure::readMeasurement(*doc, pref);
    ASSERT_TRUE(p);
    EXPECT_EQ(p->kind, Kind::Perimeter);
    EXPECT_NEAR(p->value, 7, 1e-3);
    EXPECT_EQ(p->unit, "m");
    const auto a = measure::readMeasurement(*doc, aref);
    ASSERT_TRUE(a);
    EXPECT_EQ(a->kind, Kind::Area);
    EXPECT_NEAR(a->value, 12, 1e-3);
    EXPECT_EQ(a->unit, "sq m");
    EXPECT_EQ(a->caption, "12,00 m2");
    // Мало точек и плохой масштаб — ошибка без шага.
    measure::MeasureProps bad;
    bad.kind = Kind::Area;
    bad.points = {{0, 0}, {10, 10}};
    bad.scale = s;
    EXPECT_THROW(measure::create(*doc, 0, bad), ot::MuError);
    bad.kind = Kind::Distance;
    bad.scale.realValue = 0;
    EXPECT_THROW(measure::create(*doc, 0, bad), ot::MuError);
    // Обычная линия — не измерение.
    ot::annot::AnnotProps line;
    line.type = ot::annot::AnnotType::Line;
    line.lineStart = {10, 10};
    line.lineEnd = {50, 50};
    const auto plain = ot::annot::create(*doc, 0, line);
    EXPECT_FALSE(measure::readMeasurement(*doc, plain));
    EXPECT_EQ(measure::listMeasurements(*doc).size(), 2u);
}

TEST(measure, viewport_scale_from_fixture) {
    OT_FEATURE("comment-30");
    auto doc = Document::openOrThrow(fixture());
    const auto vps = measure::viewports(*doc, 0);
    ASSERT_EQ(vps.size(), expected()["viewports"].size());
    for (std::size_t i = 0; i < vps.size(); ++i) {
        const auto& e = expected()["viewports"][i];
        SCOPED_TRACE(e["name"].get<std::string>());
        EXPECT_EQ(vps[i].name, e["name"].get<std::string>());
        EXPECT_NEAR(vps[i].bbox.x0, e["bbox"][0].get<float>(), 0.01);
        EXPECT_NEAR(vps[i].bbox.y0, e["bbox"][1].get<float>(), 0.01);
        EXPECT_NEAR(vps[i].bbox.x1, e["bbox"][2].get<float>(), 0.01);
        EXPECT_NEAR(vps[i].bbox.y1, e["bbox"][3].get<float>(), 0.01);
        EXPECT_EQ(vps[i].scale.ratio(), e["ratio"].get<std::string>());
        EXPECT_EQ(measure::unitLabel(vps[i].scale.realUnit), e["unit"].get<std::string>());
        EXPECT_EQ(vps[i].scale.precision, e["precision"].get<int>());
        EXPECT_NEAR(vps[i].scale.unitsPerPoint(), e["units_per_point"].get<double>(), 1e-6);
        const auto at = measure::viewportScale(*doc, 0, pointOf(e["inside"]));
        ASSERT_TRUE(at);
        EXPECT_EQ(measure::unitLabel(at->realUnit), e["unit"].get<std::string>());
    }
    EXPECT_FALSE(measure::viewportScale(*doc, 0, pointOf(expected()["outside"])));
    // Документ без /VP — масштаба нет.
    auto blank = Document::createBlank(1);
    EXPECT_TRUE(measure::viewports(*blank, 0).empty());
    EXPECT_FALSE(measure::viewportScale(*blank, 0, {100, 100}));
}

TEST(measure, acrobat_measurements_in_fixture) {
    OT_FEATURE("comment-29");
    auto doc = Document::openOrThrow(fixture());
    const auto all = measure::listMeasurements(*doc);
    ASSERT_EQ(all.size(), expected()["measurements"].size());
    for (std::size_t i = 0; i < all.size(); ++i) {
        const auto& e = expected()["measurements"][i];
        SCOPED_TRACE(e["nm"].get<std::string>());
        EXPECT_EQ(all[i].ref.nm, e["nm"].get<std::string>());
        EXPECT_EQ(all[i].kind, e["kind"].get<std::string>() == "area" ? Kind::Area : Kind::Distance);
        ASSERT_TRUE(all[i].hasValue);
        EXPECT_NEAR(all[i].value, e["value"].get<double>(), 1e-3);
        EXPECT_EQ(all[i].unit, e["unit"].get<std::string>());
        EXPECT_EQ(all[i].caption, e["caption"].get<std::string>());
        EXPECT_EQ(all[i].precision, 2);
    }
}

TEST(measure, snaps_to_vertices_within_radius) {
    OT_FEATURE("comment-29");
    auto doc = Document::openOrThrow(fixture());
    const std::vector<PointF> points = measure::snapPoints(*doc, 0);
    ASSERT_FALSE(points.empty());
    const measure::SnapIndex index(points);
    EXPECT_EQ(index.size(), points.size());
    for (const auto& v : expected()["vertices"]) {
        const PointF p = pointOf(v);
        SCOPED_TRACE(std::to_string(p.x) + "," + std::to_string(p.y));
        // В радиусе 8 — вершина.
        const auto near = index.nearest({p.x + 3, p.y - 2}, 8);
        ASSERT_TRUE(near);
        EXPECT_NEAR(near->x, p.x, 0.05);
        EXPECT_NEAR(near->y, p.y, 0.05);
    }
    // Углы рамки диагонали и управляющие точки кривой — не вершины.
    for (const auto& v : expected()["not_vertices"]) {
        const PointF p = pointOf(v);
        SCOPED_TRACE(std::to_string(p.x) + "," + std::to_string(p.y));
        const auto near = index.nearest(p, 2);
        EXPECT_FALSE(near);
    }
    // Вне радиуса — ничего: угол наружной стены (80, 501.89) в 9 пт.
    const PointF corner = pointOf(expected()["vertices"][0]);
    EXPECT_FALSE(index.nearest({corner.x - 9, corner.y + 0.5f}, 8));
    EXPECT_TRUE(index.nearest({corner.x - 7.5f, corner.y}, 8));
    // Пустой индекс.
    EXPECT_FALSE(measure::SnapIndex().nearest({0, 0}, 100));
}
