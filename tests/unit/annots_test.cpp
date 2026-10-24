// unit.annots.* — ядро аннотаций 5.1 (core/annot/annots.h, defaults.h, ink_math.h; services/annot_ops.h):
// создание 14 типов → внешний вид MuPDF (pdf_annot_ap) → инкрементальная запись → переоткрытие даёт те же
// /Rect, /C, /Contents, /T, /NM, qpdf --check = 0; FreeText с кириллицей рисуется и извлекается; undo/redo
// трёх операций; ластик делит штрих; буфер переносит записку на другую страницу и в другой документ; флаги,
// пунктир, сдвиг и масштаб с геометрией, группа «Заменить текст», JSON, хит-тест по типу, Identity-автор.
#include <gtest/gtest.h>

#include <QObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/annot/ink_math.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::PointF;
using ot::QuadF;
using ot::RectF;
using ot::annot::AnnotColor;
using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;

namespace {

// Пустой документ в файле (инкрементальная запись нужна файлу на диске).
fs::path blankFile(const ot::test::TempDir& dir, const char* name, int pages = 2) {
    const fs::path path = dir.path() / name;
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), pages));
    return path;
}

QuadF quadOf(float x0, float y0, float x1, float y1) { return {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}; }

AnnotProps propsFor(AnnotType type) {
    AnnotProps p = ot::annot::toolDefaults(type);
    p.contents = "c-" + std::string(ot::annot::annotTypeName(type));
    p.author = "Tester";
    p.color = AnnotColor::rgb(0.2f, 0.4f, 0.8f);
    switch (type) {
        case AnnotType::Text:
            p.rect = {50, 50, 70, 70};
            break;
        case AnnotType::FreeText:
            p.rect = {100, 50, 250, 90};
            p.color = AnnotColor{};
            break;
        case AnnotType::Line:
            p.lineStart = {50, 120};
            p.lineEnd = {200, 140};
            p.lineEndStyle = ot::annot::LineEnding::OpenArrow;
            break;
        case AnnotType::Square:
            p.rect = {50, 160, 150, 220};
            p.dash = {3, 2};
            break;
        case AnnotType::Circle:
            p.rect = {170, 160, 270, 220};
            p.interiorColor = AnnotColor::rgb(1, 1, 0.8f);
            break;
        case AnnotType::Polygon:
            p.vertices = {{300, 160}, {360, 170}, {330, 220}};
            break;
        case AnnotType::PolyLine:
            p.vertices = {{380, 160}, {420, 200}, {460, 170}};
            break;
        case AnnotType::Highlight:
            p.quads = {quadOf(50, 250, 200, 265)};
            break;
        case AnnotType::Underline:
            p.quads = {quadOf(50, 270, 200, 285)};
            break;
        case AnnotType::Squiggly:
            p.quads = {quadOf(50, 290, 200, 305)};
            break;
        case AnnotType::StrikeOut:
            p.quads = {quadOf(50, 310, 200, 325)};
            break;
        case AnnotType::Stamp:
            p.rect = {300, 250, 450, 300};
            p.icon = "Approved";
            break;
        case AnnotType::Caret:
            p.rect = {50, 350, 62, 362};
            break;
        case AnnotType::Ink:
            p.ink = {{{50, 400}, {80, 420}, {110, 400}, {140, 420}}};
            break;
        case AnnotType::FileAttachment:
            p.rect = {300, 350, 320, 370};
            p.fileName = "a.txt";
            p.fileData = "hello";
            break;
        default:
            break;
    }
    return p;
}

const AnnotType kCreatable[] = {AnnotType::Text,     AnnotType::FreeText,  AnnotType::Line,
                                AnnotType::Square,   AnnotType::Circle,    AnnotType::Polygon,
                                AnnotType::PolyLine, AnnotType::Highlight, AnnotType::Underline,
                                AnnotType::Squiggly, AnnotType::StrikeOut, AnnotType::Stamp,
                                AnnotType::Caret,    AnnotType::Ink,       AnnotType::FileAttachment};

// /AP /N у аннотации objnum страницы page (pdf_annot_ap).
bool hasAppearance(Document& doc, int page, int objnum) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::mu::PageHandle handle(ctx);
    int found = 0;
    ot::mu::call(ctx, [&] {
        handle.reset(fz_load_page(ctx, reinterpret_cast<fz_document*>(pdf), page));
        pdf_page* p = pdf_page_from_fz_page(ctx, handle);
        for (pdf_annot* a = pdf_first_annot(ctx, p); a != nullptr; a = pdf_next_annot(ctx, a)) {
            if (pdf_to_num(ctx, pdf_annot_obj(ctx, a)) == objnum && pdf_annot_ap(ctx, a) != nullptr) {
                found = 1;
            }
        }
    });
    return found != 0;
}

std::vector<AnnotInfo> comments(Document& doc, int page) {
    std::vector<AnnotInfo> out;
    for (AnnotInfo& a : ot::annot::listAnnots(doc, page)) {
        if (ot::annot::isComment(a.type)) {
            out.push_back(std::move(a));
        }
    }
    return out;
}

std::optional<AnnotInfo> byNm(const std::vector<AnnotInfo>& list, const std::string& nm) {
    for (const AnnotInfo& a : list) {
        if (a.ref.nm == nm) {
            return a;
        }
    }
    return std::nullopt;
}

void expectRectNear(const RectF& a, const RectF& b, float eps = 0.05f) {
    EXPECT_NEAR(a.x0, b.x0, eps);
    EXPECT_NEAR(a.y0, b.y0, eps);
    EXPECT_NEAR(a.x1, b.x1, eps);
    EXPECT_NEAR(a.y1, b.y1, eps);
}

void expectColorNear(const AnnotColor& a, const AnnotColor& b) {
    ASSERT_EQ(a.n, b.n);
    for (int i = 0; i < a.n; ++i) {
        EXPECT_NEAR(a.c[static_cast<std::size_t>(i)], b.c[static_cast<std::size_t>(i)], 0.01f);
    }
}

// Пиксели, отличающиеся между растрами в рамке (пункты, dpi 72 — 1 px = 1 пт).
int differingPixels(const ot::Bitmap& a, const ot::Bitmap& b, const RectF& r) {
    int n = 0;
    const int x0 = std::max(0, static_cast<int>(r.x0));
    const int y0 = std::max(0, static_cast<int>(r.y0));
    const int x1 = std::min(a.width, static_cast<int>(std::ceil(r.x1)));
    const int y1 = std::min(a.height, static_cast<int>(std::ceil(r.y1)));
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            for (int k = 0; k < a.components; ++k) {
                const auto i = static_cast<std::size_t>(x * a.components + k);
                if (a.row(y)[i] != b.row(y)[i]) {
                    ++n;
                    break;
                }
            }
        }
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------- геометрия без MuPDF

TEST(annots, simplify_stroke_drops_collinear_points) {
    ot::annot::Stroke line;
    for (int i = 0; i <= 20; ++i) {
        line.push_back({static_cast<float>(i) * 5, 100.0f + (i % 2 == 0 ? 0.0f : 0.2f)});
    }
    const ot::annot::Stroke s = ot::annot::simplifyStroke(line);
    EXPECT_EQ(s.size(), 2u);
    EXPECT_EQ(s.front(), line.front());
    EXPECT_EQ(s.back(), line.back());
    // Угол остаётся.
    const ot::annot::Stroke corner =
        ot::annot::simplifyStroke({{0, 0}, {50, 0.1f}, {100, 0}, {100, 50}, {100, 100}});
    EXPECT_EQ(corner.size(), 3u);
}

TEST(annots, erase_strokes_splits_by_circle) {
    const std::vector<ot::annot::Stroke> strokes = {{{0, 0}, {100, 0}}, {{0, 50}, {100, 50}}};
    bool changed = false;
    const auto out = ot::annot::eraseStrokes(strokes, {{50, 0}}, 6, &changed);
    EXPECT_TRUE(changed);
    ASSERT_EQ(out.size(), 3u);  // первый — два куска, второй не задет
    EXPECT_NEAR(out[0].back().x, 44, 0.1);
    EXPECT_NEAR(out[1].front().x, 56, 0.1);
    EXPECT_EQ(out[2], strokes[1]);
    // Мимо — без изменений.
    const auto same = ot::annot::eraseStrokes(strokes, {{50, 25}}, 6, &changed);
    EXPECT_FALSE(changed);
    EXPECT_EQ(same, strokes);
}

TEST(annots, hit_distances) {
    EXPECT_NEAR(ot::annot::distanceToSegment({5, 3}, {0, 0}, {10, 0}), 3, 1e-4);
    EXPECT_TRUE(ot::annot::pointInPolygon({5, 5}, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
    EXPECT_FALSE(ot::annot::pointInPolygon({15, 5}, {{0, 0}, {10, 0}, {10, 10}, {0, 10}}));
    EXPECT_EQ(ot::annot::distanceToQuad({5, 5}, quadOf(0, 0, 10, 10)), 0.0f);
    EXPECT_NEAR(ot::annot::distanceToQuad({5, 14}, quadOf(0, 0, 10, 10)), 4, 1e-4);
}

// ---------------------------------------------------------------- создание и запись

// 14 создаваемых типов (+ вложение): внешний вид есть сразу, после инкрементальной записи и переоткрытия —
// те же /Rect, /C, /Contents, /T, /NM; qpdf --check = 0.
TEST(annots, create_each_type_survives_incremental_save) {
    OT_FEATURE("comment-08");
    ot::test::TempDir dir;
    const fs::path path = blankFile(dir, "create.pdf");
    std::vector<AnnotProps> created;
    {
        auto doc = Document::openOrThrow(path);
        for (const AnnotType type : kCreatable) {
            SCOPED_TRACE(std::string(ot::annot::annotTypeName(type)));
            ASSERT_TRUE(ot::annot::canCreate(type));
            const AnnotRef ref = ot::annot::create(*doc, 0, propsFor(type));
            ASSERT_TRUE(ref.valid());
            EXPECT_EQ(ref.nm.size(), 36u);
            EXPECT_EQ(doc->undoStack().undoName(), ot::annot::createStepName(type));
            EXPECT_TRUE(hasAppearance(*doc, 0, ref.objnum));
            const auto props = ot::annot::readProps(*doc, ref);
            ASSERT_TRUE(props.has_value());
            EXPECT_EQ(props->type, type);
            EXPECT_EQ(props->nm, ref.nm);
            EXPECT_EQ(props->author, "Tester");
            EXPECT_EQ(props->contents, propsFor(type).contents);
            EXPECT_EQ(props->subject, ot::annot::defaultSubject(type));
            EXPECT_GT(props->created, 0);
            EXPECT_FALSE(props->rect.empty());
            created.push_back(*props);
        }
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
        EXPECT_EQ(report.mode, ot::SaveMode::Incremental);
    }
    auto reopened = Document::openOrThrow(path);
    const std::vector<AnnotInfo> list = comments(*reopened, 0);
    EXPECT_EQ(list.size(), std::size(kCreatable));
    for (const AnnotProps& before : created) {
        SCOPED_TRACE(std::string(ot::annot::annotTypeName(before.type)));
        const auto info = byNm(list, before.nm);
        ASSERT_TRUE(info.has_value());
        const auto after = ot::annot::readProps(*reopened, info->ref);
        ASSERT_TRUE(after.has_value());
        expectRectNear(after->rect, before.rect);
        expectColorNear(after->color, before.color);
        EXPECT_EQ(after->contents, before.contents);
        EXPECT_EQ(after->author, before.author);
        EXPECT_EQ(after->nm, before.nm);
        EXPECT_TRUE(hasAppearance(*reopened, 0, info->ref.objnum));
    }
    const ot::QpdfTool qpdf;
    if (qpdf.available()) {
        const ot::ToolResult check = qpdf.check(path);
        EXPECT_EQ(check.exitCode, 0) << check.output;
    }
}

// Кириллица в FreeText: внешний вид MuPDF со шрифтом кириллицы — в рамке есть пиксели и текст извлекается.
TEST(annots, freetext_cyrillic_renders_and_extracts) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps p = ot::annot::toolDefaults(AnnotType::FreeText);
    p.rect = {60, 60, 260, 110};
    p.contents = "Привет, мир";
    p.fontSize = 18;
    const AnnotRef ref = ot::annot::create(*doc, 0, p);
    const ot::Bitmap with = doc->renderPage(0, {.dpi = 72, .annotations = true});
    const ot::Bitmap without = doc->renderPage(0, {.dpi = 72, .annotations = false});
    EXPECT_GE(differingPixels(with, without, p.rect), 50);

    fz_context* ctx = doc->context().get();
    ot::mu::PageHandle page(ctx);
    ot::mu::StextPageHandle text(ctx);
    ot::mu::BufferHandle buf(ctx);
    const char* extracted = nullptr;
    ot::mu::call(ctx, [&] {
        page.reset(fz_load_page(ctx, doc->handle(), 0));
        for (pdf_annot* a = pdf_first_annot(ctx, pdf_page_from_fz_page(ctx, page)); a != nullptr;
             a = pdf_next_annot(ctx, a)) {
            if (pdf_to_num(ctx, pdf_annot_obj(ctx, a)) == ref.objnum) {
                text.reset(pdf_new_stext_page_from_annot(ctx, a, nullptr));
            }
        }
        buf.reset(fz_new_buffer_from_stext_page(ctx, text));
        extracted = fz_string_from_buffer(ctx, buf);
    });
    ASSERT_NE(extracted, nullptr);
    EXPECT_NE(std::string(extracted).find("Привет"), std::string::npos) << extracted;
}

TEST(annots, undo_redo_three_operations) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    ot::annot::create(*doc, 0, propsFor(AnnotType::Text));
    ot::annot::create(*doc, 0, propsFor(AnnotType::Square));
    ot::annot::create(*doc, 0, propsFor(AnnotType::Ink));
    EXPECT_EQ(comments(*doc, 0).size(), 3u);
    EXPECT_EQ(doc->undoStack().size(), 3);
    for (int i = 0; i < 3; ++i) {
        doc->undoStack().undo();
    }
    EXPECT_EQ(comments(*doc, 0).size(), 0u);
    EXPECT_TRUE(ot::annot::listAnnots(*doc, 0).empty());  // и окна записки ушли
    for (int i = 0; i < 3; ++i) {
        doc->undoStack().redo();
    }
    EXPECT_EQ(comments(*doc, 0).size(), 3u);
}

// ---------------------------------------------------------------- ластик, сдвиг, масштаб

TEST(annots, eraser_splits_stroke_and_removes_empty) {
    OT_FEATURE("comment-06");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps p = propsFor(AnnotType::Ink);
    p.ink = {{}};
    for (int x = 50; x <= 250; x += 10) {
        p.ink[0].push_back({static_cast<float>(x), 300});
    }
    const AnnotRef ref = ot::annot::create(*doc, 0, p);
    EXPECT_FALSE(ot::annot::eraseInk(*doc, ref, {{150, 340}}, 6));  // мимо — шага нет
    EXPECT_EQ(doc->undoStack().size(), 1);
    EXPECT_TRUE(ot::annot::eraseInk(*doc, ref, {{150, 300}}, 6));
    const auto split = ot::annot::readProps(*doc, ref);
    ASSERT_TRUE(split.has_value());
    ASSERT_EQ(split->ink.size(), 2u);
    EXPECT_NEAR(split->ink[0].back().x, 144, 0.2);
    EXPECT_NEAR(split->ink[1].front().x, 156, 0.2);
    EXPECT_EQ(doc->undoStack().undoName(), "Erase ink");
    doc->undoStack().undo();
    EXPECT_EQ(ot::annot::readProps(*doc, ref)->ink.size(), 1u);
    // Стереть всё — аннотации нет.
    EXPECT_TRUE(ot::annot::eraseInk(*doc, ref, {{40, 300}, {260, 300}}, 8));
    EXPECT_FALSE(ot::annot::findAnnot(*doc, ref).has_value());
}

TEST(annots, move_keeps_size_and_shifts_geometry) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef ink = ot::annot::create(*doc, 0, propsFor(AnnotType::Ink));
    const AnnotInfo before = *ot::annot::findAnnot(*doc, ink);
    const AnnotProps props = *ot::annot::readProps(*doc, ink);
    const RectF moved{before.rect.x0 + 10, before.rect.y0 + 5, before.rect.x1 + 10, before.rect.y1 + 5};
    ot::annot::setRect(*doc, ink, moved);
    const AnnotProps after = *ot::annot::readProps(*doc, ink);
    ASSERT_EQ(after.ink.size(), 1u);
    for (std::size_t i = 0; i < after.ink[0].size(); ++i) {
        EXPECT_NEAR(after.ink[0][i].x, props.ink[0][i].x + 10, 0.01);
        EXPECT_NEAR(after.ink[0][i].y, props.ink[0][i].y + 5, 0.01);
    }
    expectRectNear(ot::annot::findAnnot(*doc, ink)->rect, moved, 0.05f);

    // translate нескольких — одна операция.
    const AnnotRef square = ot::annot::create(*doc, 0, propsFor(AnnotType::Square));
    const RectF squareRect = ot::annot::readProps(*doc, square)->rect;
    const int steps = doc->undoStack().size();
    ot::annot::translate(*doc, {ink, square}, -10, -5);
    EXPECT_EQ(doc->undoStack().size(), steps + 1);
    expectRectNear(ot::annot::readProps(*doc, square)->rect,
                   {squareRect.x0 - 10, squareRect.y0 - 5, squareRect.x1 - 10, squareRect.y1 - 5});
}

TEST(annots, resize_scales_geometry_and_resynthesizes) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef square = ot::annot::create(*doc, 0, propsFor(AnnotType::Square));
    const AnnotInfo info = *ot::annot::findAnnot(*doc, square);
    const RectF bigger{info.rect.x0, info.rect.y0, info.rect.x1 + 100, info.rect.y1 + 50};
    ot::annot::setRect(*doc, square, bigger);
    expectRectNear(ot::annot::findAnnot(*doc, square)->rect, bigger, 1.0f);
    EXPECT_TRUE(hasAppearance(*doc, 0, square.objnum));
}

// ---------------------------------------------------------------- свойства, флаги, пунктир, группы

TEST(annots, update_props_and_undo) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef ref = ot::annot::create(*doc, 0, propsFor(AnnotType::Text));
    ot::annot::AnnotPatch patch;
    patch.contents = "new text";
    patch.color = AnnotColor::rgb(0, 1, 0);
    patch.opacity = 0.5f;
    patch.icon = "Key";
    ot::annot::update(*doc, ref, patch);
    const AnnotProps p = *ot::annot::readProps(*doc, ref);
    EXPECT_EQ(p.contents, "new text");
    expectColorNear(p.color, AnnotColor::rgb(0, 1, 0));
    EXPECT_NEAR(p.opacity, 0.5f, 0.01f);
    EXPECT_EQ(p.icon, "Key");
    EXPECT_EQ(doc->undoStack().undoName(), "Set annotation properties");
    doc->undoStack().undo();
    EXPECT_EQ(ot::annot::readProps(*doc, ref)->contents, propsFor(AnnotType::Text).contents);
}

TEST(annots, flags_hidden_noview_print) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef ref = ot::annot::create(*doc, 0, propsFor(AnnotType::Square));
    const AnnotInfo info = *ot::annot::findAnnot(*doc, ref);
    EXPECT_NE(info.flags & ot::annot::kAnnotPrint, 0);
    const PointF c{(info.rect.x0 + info.rect.x1) / 2, info.rect.y0 + 0.5f};
    ASSERT_TRUE(ot::annot::annotAt(*doc, 0, c, 1).has_value());
    ot::annot::setFlags(*doc, {ref}, ot::annot::kAnnotHidden, ot::annot::kAnnotPrint);
    const AnnotInfo hidden = *ot::annot::findAnnot(*doc, ref);
    EXPECT_NE(hidden.flags & ot::annot::kAnnotHidden, 0);
    EXPECT_EQ(hidden.flags & ot::annot::kAnnotPrint, 0);
    EXPECT_FALSE(ot::annot::annotAt(*doc, 0, c, 1).has_value());
    ot::annot::setFlags(*doc, {ref}, ot::annot::kAnnotNoView | ot::annot::kAnnotPrint,
                        ot::annot::kAnnotHidden);
    EXPECT_TRUE(ot::annot::findAnnot(*doc, ref)->hiddenOnScreen());
    ot::annot::setFlags(*doc, {ref}, 0, ot::annot::kAnnotNoView);
    EXPECT_TRUE(ot::annot::annotAt(*doc, 0, c, 1).has_value());
}

TEST(annots, dash_pattern_and_cloud) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef ref = ot::annot::create(*doc, 0, propsFor(AnnotType::Square));
    AnnotProps p = *ot::annot::readProps(*doc, ref);
    EXPECT_EQ(p.borderStyle, ot::annot::BorderStyle::Dashed);
    ASSERT_EQ(p.dash.size(), 2u);
    EXPECT_FLOAT_EQ(p.dash[0], 3);
    EXPECT_FLOAT_EQ(p.dash[1], 2);
    ot::annot::AnnotPatch patch;
    patch.dash = std::vector<float>{};
    patch.cloudy = true;
    ot::annot::update(*doc, ref, patch);
    p = *ot::annot::readProps(*doc, ref);
    EXPECT_TRUE(p.dash.empty());
    EXPECT_EQ(p.borderStyle, ot::annot::BorderStyle::Solid);
    EXPECT_TRUE(p.cloudy);
}

// «Заменить текст» (эталон 3.4): Caret главная, StrikeOut — /IRT на неё и /RT /Group, одна операция.
TEST(annots, replace_text_group) {
    OT_FEATURE("comment-03");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps caret = propsFor(AnnotType::Caret);
    caret.contents = "new words";
    AnnotProps strike = propsFor(AnnotType::StrikeOut);
    strike.intent = "StrikeOutTextEdit";
    const auto refs = ot::annot::createGroup(*doc, 0, {caret, strike}, "Replace text");
    ASSERT_EQ(refs.size(), 2u);
    EXPECT_EQ(doc->undoStack().size(), 1);
    EXPECT_EQ(doc->undoStack().undoName(), "Replace text");
    const AnnotProps s = *ot::annot::readProps(*doc, refs[1]);
    EXPECT_EQ(s.inReplyTo, refs[0].objnum);
    EXPECT_EQ(s.replyType, "Group");
    EXPECT_EQ(s.intent, "StrikeOutTextEdit");
    const AnnotInfo info = *ot::annot::findAnnot(*doc, refs[1]);
    EXPECT_EQ(info.inReplyTo, refs[0].objnum);
}

// ---------------------------------------------------------------- хит-тест по типу

TEST(annots, hit_test_by_type) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps hl = propsFor(AnnotType::Highlight);
    hl.quads = {quadOf(50, 100, 200, 110), quadOf(50, 130, 120, 140)};
    const AnnotRef highlight = ot::annot::create(*doc, 0, hl);
    EXPECT_EQ(ot::annot::annotAt(*doc, 0, {100, 105})->ref, highlight);
    EXPECT_EQ(ot::annot::annotAt(*doc, 0, {100, 135})->ref, highlight);
    EXPECT_FALSE(ot::annot::annotAt(*doc, 0, {100, 120}).has_value());  // между строк
    EXPECT_FALSE(ot::annot::annotAt(*doc, 0, {180, 135}).has_value());  // правее второй строки

    AnnotProps ink = propsFor(AnnotType::Ink);
    ink.ink = {{{300, 300}, {400, 400}}};
    ink.borderWidth = 2;
    const AnnotRef stroke = ot::annot::create(*doc, 0, ink);
    EXPECT_EQ(ot::annot::annotAt(*doc, 0, {350, 350})->ref, stroke);
    EXPECT_FALSE(ot::annot::annotAt(*doc, 0, {390, 310}).has_value());   // в рамке, но далеко от штриха
    EXPECT_EQ(ot::annot::annotAt(*doc, 0, {353, 350}, 3)->ref, stroke);  // в допуске

    AnnotProps line = propsFor(AnnotType::Line);
    line.lineStart = {300, 500};
    line.lineEnd = {450, 560};
    const AnnotRef l = ot::annot::create(*doc, 0, line);
    EXPECT_EQ(ot::annot::annotAt(*doc, 0, {375, 530})->ref, l);
    EXPECT_FALSE(ot::annot::annotAt(*doc, 0, {310, 555}).has_value());
}

// ---------------------------------------------------------------- буфер обмена

TEST(annots, clipboard_copies_note_to_other_page_and_document) {
    OT_FEATURE("comment-43");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 2));
    AnnotProps note = propsFor(AnnotType::Text);
    note.contents = "copy me";
    const AnnotRef src = ot::annot::create(*doc, 0, note);
    const AnnotProps srcProps = *ot::annot::readProps(*doc, src);
    ASSERT_TRUE(srcProps.hasPopup());

    const ot::annot::AnnotClip clip = ot::annot::copy(*doc, {src});
    ASSERT_EQ(clip.count, 1);
    ASSERT_FALSE(clip.pdf.empty());

    const auto pasted = ot::annot::paste(*doc, 1, clip);
    ASSERT_EQ(pasted.size(), 1u);
    EXPECT_EQ(pasted[0].page, 1);
    EXPECT_EQ(doc->undoStack().undoName(), "Paste annotations");
    const AnnotProps copyProps = *ot::annot::readProps(*doc, pasted[0]);
    EXPECT_EQ(copyProps.type, AnnotType::Text);
    EXPECT_EQ(copyProps.contents, "copy me");
    EXPECT_NE(copyProps.nm, srcProps.nm);
    EXPECT_EQ(copyProps.nm.size(), 36u);
    expectRectNear(copyProps.rect, srcProps.rect);
    EXPECT_TRUE(copyProps.hasPopup());
    EXPECT_TRUE(hasAppearance(*doc, 1, pasted[0].objnum));
    // Исходная — на месте.
    EXPECT_EQ(comments(*doc, 0).size(), 1u);

    // Другой документ.
    auto other = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const auto there = ot::annot::paste(*other, 0, clip, {5, 5});
    ASSERT_EQ(there.size(), 1u);
    const AnnotProps otherProps = *ot::annot::readProps(*other, there[0]);
    EXPECT_EQ(otherProps.contents, "copy me");
    expectRectNear(otherProps.rect,
                   {srcProps.rect.x0 + 5, srcProps.rect.y0 + 5, srcProps.rect.x1 + 5, srcProps.rect.y1 + 5});
    other->undoStack().undo();
    EXPECT_TRUE(comments(*other, 0).empty());

    // Пустой буфер — ошибка.
    EXPECT_THROW(ot::annot::paste(*doc, 0, ot::annot::AnnotClip{}), ot::MuError);
}

TEST(annots, clipboard_keeps_custom_appearance) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const AnnotRef stamp = ot::annot::create(*doc, 0, propsFor(AnnotType::Stamp));
    const AnnotRef ink = ot::annot::create(*doc, 0, propsFor(AnnotType::Ink));
    const ot::annot::AnnotClip clip = ot::annot::copy(*doc, {stamp, ink});
    ASSERT_EQ(clip.count, 2);
    auto other = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const auto refs = ot::annot::paste(*other, 0, clip);
    ASSERT_EQ(refs.size(), 2u);
    EXPECT_EQ(ot::annot::readProps(*other, refs[0])->type, AnnotType::Stamp);
    EXPECT_EQ(ot::annot::readProps(*other, refs[1])->ink.size(), 1u);
    const ot::Bitmap a = doc->renderPage(0, {.dpi = 72});
    const ot::Bitmap b = other->renderPage(0, {.dpi = 72});
    const AnnotInfo info = *ot::annot::findAnnot(*doc, stamp);
    EXPECT_EQ(differingPixels(a, b, info.rect), 0);
}

// ---------------------------------------------------------------- JSON и умолчания

TEST(annots, json_round_trip) {
    AnnotProps p = propsFor(AnnotType::Ink);
    p.nm = "id-1";
    p.subject = "Pencil";
    p.richContents = "<body><p>x</p></body>";
    p.interiorColor = AnnotColor::rgb(1, 0, 0);
    p.opacity = 0.4f;
    p.dash = {2, 1};
    p.borderStyle = ot::annot::BorderStyle::Dashed;
    p.quads = {quadOf(1, 2, 3, 4)};
    p.vertices = {{1, 1}, {2, 3}};
    p.lineStart = {1, 2};
    p.lineEnd = {3, 4};
    p.lineEndStyle = ot::annot::LineEnding::ClosedArrow;
    p.intent = "LineArrow";
    p.popupRect = {10, 10, 210, 110};
    p.open = true;
    p.inReplyTo = 7;
    p.replyType = "Group";
    p.created = 1700000000;
    p.modified = 1700000100;
    p.flags = ot::annot::kAnnotPrint | ot::annot::kAnnotLocked;
    p.callout = {{1, 1}, {5, 5}};
    p.align = 2;
    p.fontSize = 9;
    const AnnotProps q = ot::annot::propsFromJson(ot::annot::toJson(p));
    EXPECT_EQ(q.type, p.type);
    EXPECT_EQ(q.nm, p.nm);
    EXPECT_EQ(q.contents, p.contents);
    EXPECT_EQ(q.richContents, p.richContents);
    EXPECT_EQ(q.author, p.author);
    EXPECT_EQ(q.subject, p.subject);
    EXPECT_EQ(q.color, p.color);
    EXPECT_EQ(q.interiorColor, p.interiorColor);
    EXPECT_FLOAT_EQ(q.opacity, p.opacity);
    EXPECT_EQ(q.dash, p.dash);
    EXPECT_EQ(q.borderStyle, p.borderStyle);
    EXPECT_EQ(q.ink, p.ink);
    EXPECT_EQ(q.vertices, p.vertices);
    EXPECT_EQ(q.lineStart, p.lineStart);
    EXPECT_EQ(q.lineEndStyle, p.lineEndStyle);
    EXPECT_EQ(q.intent, p.intent);
    EXPECT_EQ(q.open, p.open);
    EXPECT_EQ(q.inReplyTo, p.inReplyTo);
    EXPECT_EQ(q.replyType, p.replyType);
    EXPECT_EQ(q.created, p.created);
    EXPECT_EQ(q.modified, p.modified);
    EXPECT_EQ(q.flags, p.flags);
    EXPECT_EQ(q.callout, p.callout);
    EXPECT_EQ(q.align, p.align);
    EXPECT_FLOAT_EQ(q.fontSize, p.fontSize);
    ASSERT_EQ(q.quads.size(), 1u);
    EXPECT_EQ(q.quads[0].lr, p.quads[0].lr);
    expectRectNear(q.popupRect, p.popupRect);
    EXPECT_THROW(ot::annot::propsFromJson("{not json"), std::invalid_argument);
}

TEST(annots, identity_author_and_tool_defaults) {
    OT_FEATURE("comment-24");
    EXPECT_FALSE(ot::annot::loginName().empty());
    ot::annot::Identity identity;
    identity.name = "Иван Петров";
    identity.alwaysLoginName = false;
    EXPECT_EQ(ot::annot::authorName(identity), "Иван Петров");
    identity.alwaysLoginName = true;
    EXPECT_EQ(ot::annot::authorName(identity), ot::annot::loginName());
    identity.alwaysLoginName = false;
    identity.name.clear();
    EXPECT_EQ(ot::annot::authorName(identity), ot::annot::loginName());

    EXPECT_EQ(ot::annot::toolDefaults(AnnotType::Text).icon, "Comment");
    EXPECT_EQ(ot::annot::toolDefaults(AnnotType::Highlight).color, AnnotColor::rgb(1, 1, 0));
    EXPECT_EQ(ot::annot::toolDefaults(AnnotType::StrikeOut).color, AnnotColor::rgb(1, 0, 0));
    EXPECT_EQ(ot::annot::defaultSubject(AnnotType::Ink), "Pencil");
    EXPECT_EQ(AnnotColor::fromRgb(0xff8000).toRgb(), 0xff8000u);

    // Автор из Identity попадает в /T.
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    AnnotProps p = propsFor(AnnotType::Text);
    identity.name = "Мария";
    p.author = ot::annot::authorName(identity);
    const AnnotRef ref = ot::annot::create(*doc, 0, p);
    EXPECT_EQ(ot::annot::findAnnot(*doc, ref)->author, "Мария");
}

TEST(annots, create_errors) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    EXPECT_FALSE(ot::annot::canCreate(AnnotType::Widget));
    EXPECT_THROW(ot::annot::create(*doc, 0, ot::annot::toolDefaults(AnnotType::Widget)), ot::MuError);
    AnnotProps empty = ot::annot::toolDefaults(AnnotType::Highlight);  // без квадов
    EXPECT_THROW(ot::annot::create(*doc, 0, empty), ot::MuError);
    EXPECT_THROW(ot::annot::create(*doc, 3, propsFor(AnnotType::Text)), ot::MuError);
    EXPECT_FALSE(doc->undoStack().canUndo());
}

// ---------------------------------------------------------------- через DocumentSession

TEST(annots, session_create_update_erase_paste) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(blankFile(dir, "session.pdf").wstring())));
    std::vector<int> pages;
    QObject::connect(&session, &ot::DocumentSession::pageChanged, &session,
                     [&](int p) { pages.push_back(p); });

    const AnnotRef note = ot::annot::create(session, 0, propsFor(AnnotType::Text)).result();
    ASSERT_TRUE(note.valid());
    EXPECT_EQ(session.undoState().undoName, QStringLiteral("Add note"));
    ASSERT_TRUE(ot::test::waitUntil([&] { return !pages.empty(); }));

    ot::annot::AnnotPatch patch;
    patch.contents = "edited";
    ot::annot::update(session, note, patch).waitForFinished();
    EXPECT_EQ(ot::annot::props(session, note).result()->contents, "edited");

    const AnnotRef ink = ot::annot::create(session, 0, propsFor(AnnotType::Ink)).result();
    EXPECT_TRUE(ot::annot::eraseInk(session, ink, {{80, 420}}, 6).result());
    EXPECT_EQ(session.undoState().undoName, QStringLiteral("Erase ink"));

    const ot::annot::AnnotClip clip = ot::annot::copy(session, {note}).result();
    const auto pasted = ot::annot::paste(session, 1, clip, {}).result();
    ASSERT_EQ(pasted.size(), 1u);
    EXPECT_EQ(ot::annot::props(session, pasted[0]).result()->contents, "edited");

    ot::annot::setFlags(session, {note}, ot::annot::kAnnotLocked, 0).waitForFinished();
    EXPECT_TRUE(ot::annot::props(session, note).result()->flags & ot::annot::kAnnotLocked);
    EXPECT_TRUE(session.undo().result());
    session.close();
}
