// unit.shapes.* — фигуры (план 5.2, п. 2; эталон 3.4 «Инструмент»: Фигуры — прямоугольник, эллипс, линия,
// стрелка, многоугольник, ломаная, облако /BE; comment-07): каждая фигура пишет свои ключи — /L и /LE (линия,
// стрелка /IT /LineArrow), /Vertices (многоугольник, ломаная с /LE), /IC (заливка), /BS /D (пунктир), /BE /S
// /C с /I (облако), /CA — и переживает инкрементальную запись и переоткрытие; облако рисуется дугами, и его
// вид шире рамки «дизайна» (/RD); правка вершин и концов (AnnotPatch::vertices, lineStart/lineEnd,
// lineEndStyle) — новые ключи и вид, шаг истории один.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::PointF;
using ot::annot::AnnotColor;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::annot::LineEnding;

namespace {

// Ключи словаря аннотации — текстом (pdf_sprint_obj) и вид /AP /N.
struct Keys {
    std::string l, le, vertices, ic, bs, be, it, ca, rd;
    std::string ap;
    fz_rect rect{};
    fz_rect bbox{};
};

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

Keys keysOf(Document& doc, int objnum) {
    Keys k;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::mu::call(ctx, [&] {
        pdf_obj* a = pdf_load_object(ctx, pdf, objnum);
        k.l = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(L)));
        k.le = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(LE)));
        k.vertices = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(Vertices)));
        k.ic = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(IC)));
        k.bs = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(BS)));
        k.be = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(BE)));
        k.it = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(IT)));
        k.ca = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(CA)));
        k.rd = printed(ctx, pdf_dict_get(ctx, a, PDF_NAME(RD)));
        k.rect = pdf_dict_get_rect(ctx, a, PDF_NAME(Rect));
        pdf_obj* n = pdf_dict_getp(ctx, a, "AP/N");
        k.bbox = pdf_dict_get_rect(ctx, n, PDF_NAME(BBox));
        if (pdf_is_stream(ctx, n)) {
            fz_buffer* buf = pdf_load_stream(ctx, n);
            unsigned char* data = nullptr;
            const size_t len = fz_buffer_storage(ctx, buf, &data);
            k.ap.assign(reinterpret_cast<const char*>(data), len);
            fz_drop_buffer(ctx, buf);
        }
        pdf_drop_obj(ctx, a);
    });
    return k;
}

AnnotProps shape(AnnotType type) {
    AnnotProps p = ot::annot::toolDefaults(type);
    p.author = "Tester";
    p.contents = std::string(ot::annot::annotTypeName(type));
    return p;
}

// Все фигуры инструмента «Фигуры» — по одной.
struct Made {
    std::string name;
    AnnotProps props;
};

std::vector<Made> allShapes() {
    std::vector<Made> out;
    AnnotProps line = shape(AnnotType::Line);
    line.lineStart = {60, 80};
    line.lineEnd = {220, 120};
    out.push_back({"line", line});
    AnnotProps arrow = shape(AnnotType::Line);
    arrow.lineStart = {60, 160};
    arrow.lineEnd = {220, 200};
    arrow.lineEndStyle = LineEnding::ClosedArrow;
    arrow.intent = "LineArrow";
    arrow.interiorColor = AnnotColor::rgb(1, 0, 0);
    out.push_back({"arrow", arrow});
    AnnotProps rect = shape(AnnotType::Square);
    rect.rect = {260, 60, 400, 140};
    rect.interiorColor = AnnotColor::rgb(1, 1, 0.6f);
    rect.dash = {4, 2};
    rect.borderWidth = 2;
    rect.opacity = 0.7f;
    out.push_back({"rectangle", rect});
    AnnotProps oval = shape(AnnotType::Circle);
    oval.rect = {420, 60, 540, 140};
    out.push_back({"oval", oval});
    AnnotProps polygon = shape(AnnotType::Polygon);
    polygon.vertices = {{60, 260}, {160, 240}, {200, 320}, {90, 340}};
    polygon.interiorColor = AnnotColor::rgb(0.8f, 0.9f, 1);
    out.push_back({"polygon", polygon});
    AnnotProps polyline = shape(AnnotType::PolyLine);
    polyline.vertices = {{260, 260}, {320, 320}, {380, 250}, {440, 330}};
    polyline.lineEndStyle = LineEnding::OpenArrow;
    out.push_back({"polyline", polyline});
    AnnotProps cloud = shape(AnnotType::Polygon);
    cloud.vertices = {{60, 420}, {240, 400}, {260, 520}, {80, 540}};
    cloud.cloudy = true;
    cloud.cloudIntensity = 2;
    cloud.intent = "PolygonCloud";
    out.push_back({"cloud", cloud});
    AnnotProps cloudRect = shape(AnnotType::Square);
    cloudRect.rect = {320, 420, 500, 520};
    cloudRect.cloudy = true;
    cloudRect.cloudIntensity = 1;
    out.push_back({"cloud-rectangle", cloudRect});
    return out;
}

}  // namespace

TEST(shapes, keys_survive_incremental_save_and_reopen) {
    OT_FEATURE("comment-07");
    ot::test::TempDir dir;
    const fs::path path = dir.path() / "shapes.pdf";
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), 1));
    const std::vector<Made> shapes = allShapes();
    std::vector<std::string> nms;
    {
        auto doc = Document::openOrThrow(path);
        for (const Made& m : shapes) {
            nms.push_back(ot::annot::create(*doc, 0, m.props).nm);
        }
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    auto doc = Document::openOrThrow(path);
    const auto list = ot::annot::listAnnots(*doc, 0);
    for (std::size_t i = 0; i < shapes.size(); ++i) {
        SCOPED_TRACE(shapes[i].name);
        const auto it =
            std::find_if(list.begin(), list.end(), [&](const auto& a) { return a.ref.nm == nms[i]; });
        ASSERT_NE(it, list.end());
        const Keys k = keysOf(*doc, it->ref.objnum);
        const auto p = ot::annot::readProps(*doc, it->ref);
        ASSERT_TRUE(p);
        EXPECT_FALSE(k.ap.empty());
        const std::string& name = shapes[i].name;
        if (name == "line" || name == "arrow") {
            EXPECT_FALSE(k.l.empty());
            EXPECT_NEAR(p->lineStart.x, shapes[i].props.lineStart.x, 0.1);
            EXPECT_NEAR(p->lineEnd.y, shapes[i].props.lineEnd.y, 0.1);
        }
        if (name == "arrow") {
            EXPECT_NE(k.le.find("/ClosedArrow"), std::string::npos) << k.le;
            EXPECT_EQ(p->lineEndStyle, LineEnding::ClosedArrow);
            EXPECT_EQ(p->intent, "LineArrow");
        }
        if (name == "rectangle") {
            EXPECT_NE(k.ic.find(".6"), std::string::npos) << k.ic;
            EXPECT_NE(k.bs.find("/D"), std::string::npos) << k.bs;
            EXPECT_NE(k.bs.find("/W 2"), std::string::npos) << k.bs;
            EXPECT_NE(k.ca.find(".7"), std::string::npos) << k.ca;
            ASSERT_EQ(p->dash.size(), 2u);
            EXPECT_EQ(p->borderStyle, ot::annot::BorderStyle::Dashed);
            EXPECT_EQ(p->interiorColor.n, 3);
        }
        if (name == "polygon" || name == "polyline" || name == "cloud") {
            EXPECT_FALSE(k.vertices.empty());
            EXPECT_EQ(p->vertices.size(), shapes[i].props.vertices.size());
        }
        if (name == "polyline") {
            EXPECT_NE(k.le.find("/OpenArrow"), std::string::npos) << k.le;
            EXPECT_EQ(p->lineEndStyle, LineEnding::OpenArrow);
        }
        if (name == "cloud" || name == "cloud-rectangle") {
            EXPECT_NE(k.be.find("/S/C"), std::string::npos) << k.be;
            EXPECT_TRUE(p->cloudy);
            EXPECT_NEAR(p->cloudIntensity, shapes[i].props.cloudIntensity, 0.01);
            EXPECT_NE(k.ap.find(" c\n"), std::string::npos);  // дуги облака
        }
    }
}

// Облако: вид шире рамки «дизайна» — /RD отодвигает /Rect на радиус дуг, /BBox вида — по /Rect.
TEST(shapes, cloud_appearance_is_wider_than_design_rect) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps p = shape(AnnotType::Square);
    p.rect = {100, 100, 300, 200};
    p.cloudy = true;
    p.cloudIntensity = 2;
    const AnnotRef ref = ot::annot::create(*doc, 0, p);
    const Keys k = keysOf(*doc, ref.objnum);
    EXPECT_FALSE(k.rd.empty());
    EXPECT_GT(k.bbox.x1 - k.bbox.x0, 200 + 4);
    EXPECT_GT(k.bbox.y1 - k.bbox.y0, 100 + 4);
    // Без облака — /BBox в рамке.
    AnnotProps plain = p;
    plain.cloudy = false;
    plain.rect = {100, 300, 300, 400};
    const Keys q = keysOf(*doc, ot::annot::create(*doc, 0, plain).objnum);
    EXPECT_LT(q.bbox.x1 - q.bbox.x0, 200 + 4);
    // Рендер: дуги выходят за рамку дизайна.
    const ot::Bitmap with = doc->renderPage(0, {.dpi = 72, .annotations = true});
    int outside = 0;
    for (int x = 90; x < 100; ++x) {
        for (int y = 110; y < 190; ++y) {
            for (int c = 0; c < with.components; ++c) {
                if (with.row(y)[static_cast<std::size_t>(x * with.components + c)] < 200) {
                    ++outside;  // красная линия: зелёный и синий тёмные
                    break;
                }
            }
        }
    }
    EXPECT_GT(outside, 0);
}

// Правка вершин и концов — новые ключи и вид одним шагом.
TEST(shapes, edit_vertices_and_endings) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps line = shape(AnnotType::Line);
    line.lineStart = {60, 80};
    line.lineEnd = {220, 120};
    const AnnotRef l = ot::annot::create(*doc, 0, line);
    const int steps = doc->undoStack().size();
    ot::annot::AnnotPatch patch;
    patch.lineStart = PointF{80, 100};
    patch.lineEnd = PointF{300, 100};
    patch.lineEndStyle = LineEnding::OpenArrow;
    ot::annot::update(*doc, l, patch, "Move vertex");
    EXPECT_EQ(doc->undoStack().size(), steps + 1);
    auto p = ot::annot::readProps(*doc, l);
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->lineEnd.x, 300, 0.1);
    EXPECT_EQ(p->lineEndStyle, LineEnding::OpenArrow);
    EXPECT_EQ(p->lineStartStyle, LineEnding::None);
    EXPECT_GE(p->rect.x1, 299);  // /Rect — по новой линии

    AnnotProps poly = shape(AnnotType::Polygon);
    poly.vertices = {{60, 260}, {160, 240}, {200, 320}};
    const AnnotRef g = ot::annot::create(*doc, 0, poly);
    ot::annot::AnnotPatch vp;
    vp.vertices = std::vector<PointF>{{60, 260}, {160, 240}, {400, 500}};
    ot::annot::update(*doc, g, vp, "Move vertex");
    p = ot::annot::readProps(*doc, g);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->vertices.size(), 3u);
    EXPECT_NEAR(p->vertices[2].x, 400, 0.1);
    EXPECT_GE(p->rect.y1, 499);
    doc->undoStack().undo();
    p = ot::annot::readProps(*doc, g);
    EXPECT_NEAR(p->vertices[2].x, 200, 0.1);
}
