// unit.fillsign_placement — умное размещение Fill & Sign (план 5.3, п. 2; эталон 3.5 «Умное размещение»: поле
// ищется по подчёркиваниям и рамкам из COLLECT_VECTORS — текст на линию, кегль под высоту клетки; гребёнка —
// по символу в клетку; forms-07, comment-09) на плоской анкете tests/fixtures/forms/flat/anketa.pdf с
// ожиданиями flat.json генератора: линии, рамки, засечки и флажки находятся; щелчок над подчёркиванием (и
// чуть ниже него) ставит базовую линию на линию с кеглем ≤ высоты клетки; в рамке — текст от её левого края;
// гребёнки из смежных клеток и из засечек дают клетки с их шагом, а «01021990» — 8 полей FreeText по центру
// клеток одним шагом истории; отметки ✓ ✕ ● — ▭ — глифы Segoe UI Symbol (видны в растре), во флажке — по его
// центру; щелчок вдали от полей — без привязки.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/annot/annots.h"
#include "core/annot/fillsign.h"
#include "core/annot/freetext.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/render/bitmap.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
namespace fillsign = ot::fillsign;
using fillsign::FieldKind;
using fillsign::Placement;
using ot::Document;
using ot::PointF;
using ot::RectF;

namespace {

fs::path flatDir() { return ot::test::sourceDir() / "tests" / "fixtures" / "forms" / "flat"; }

const nlohmann::json& fields() {
    static const nlohmann::json all = [] {
        std::ifstream in(flatDir() / "flat.json", std::ios::binary);
        return nlohmann::json::parse(in)["files"]["anketa.pdf"]["fields"];
    }();
    return all;
}

const nlohmann::json& field(const std::string& name) {
    for (const auto& f : fields()) {
        if (f["name"].get<std::string>() == name) {
            return f;
        }
    }
    throw std::runtime_error("no field " + name);
}

RectF rectOf(const nlohmann::json& r) {
    return {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
}

fillsign::PageGeometry geometryOf(Document& doc) {
    ot::TextOptions options;
    options.collectVectors = true;
    const auto text = ot::TextPage::build(doc, 0, options);
    return fillsign::geometryOf(text->vectors(), text->lines());
}

const fillsign::PageGeometry& anketa() {
    static const fillsign::PageGeometry geometry = [] {
        auto doc = Document::openOrThrow(flatDir() / "anketa.pdf");
        return geometryOf(*doc);
    }();
    return geometry;
}

// Пиксели, которые аннотации изменили в рамке r (растр 72 dpi: пиксель = пункт).
struct Ink {
    int count = 0;
    RectF box = RectF::none();
};

Ink inkIn(Document& doc, const RectF& r) {
    const ot::Bitmap with = doc.renderPage(0, {.dpi = 72, .annotations = true});
    const ot::Bitmap without = doc.renderPage(0, {.dpi = 72, .annotations = false});
    Ink ink;
    for (int y = std::max(0, static_cast<int>(r.y0)); y < std::min(with.height, static_cast<int>(r.y1));
         ++y) {
        for (int x = std::max(0, static_cast<int>(r.x0)); x < std::min(with.width, static_cast<int>(r.x1));
             ++x) {
            for (int k = 0; k < with.components; ++k) {
                const auto i = static_cast<std::size_t>(x * with.components + k);
                if (with.row(y)[i] != without.row(y)[i]) {
                    ++ink.count;
                    const RectF px{static_cast<float>(x), static_cast<float>(y), static_cast<float>(x + 1),
                                   static_cast<float>(y + 1)};
                    ink.box = ink.box.valid() ? ot::rectUnion(ink.box, px) : px;
                    break;
                }
            }
        }
    }
    return ink;
}

}  // namespace

TEST(fillsign_placement, geometry_finds_lines_boxes_ticks_and_checks) {
    const fillsign::PageGeometry& g = anketa();
    for (const auto& f : fields()) {
        const RectF r = rectOf(f["rect"]);
        if (f["kind"] == "line") {
            const float base = f["base"].get<float>();
            const bool found =
                std::any_of(g.horizontal.begin(), g.horizontal.end(), [&](const fillsign::HLine& h) {
                    return std::abs(h.y - base) < 0.5f && std::abs(h.x0 - r.x0) < 1.5f &&
                           std::abs(h.x1 - r.x1) < 1.5f;
                });
            EXPECT_TRUE(found) << f["name"].get<std::string>();
        }
        if (f["kind"] == "check") {
            const bool found = std::any_of(g.checks.begin(), g.checks.end(), [&](const RectF& c) {
                return std::abs(c.x0 - r.x0) < 1.5f && std::abs(c.y0 - r.y0) < 1.5f &&
                       std::abs(c.x1 - r.x1) < 1.5f && std::abs(c.y1 - r.y1) < 1.5f;
            });
            EXPECT_TRUE(found) << f["name"].get<std::string>();
        }
    }
    EXPECT_EQ(g.checks.size(), 2u);
    // 13 засечек ИНН — вертикали высотой 10 у базовой линии.
    const auto& inn = field("ИНН");
    const float base = inn["base"].get<float>();
    const int ticks =
        static_cast<int>(std::count_if(g.vertical.begin(), g.vertical.end(), [&](const fillsign::VLine& v) {
            return std::abs(v.y1 - base) < 1.5f && std::abs(v.y1 - v.y0 - 10) < 2.0f;
        }));
    EXPECT_EQ(ticks, 13);
    EXPECT_FALSE(g.text.empty());
}

TEST(fillsign_placement, click_over_underline_puts_text_on_the_line) {
    OT_FEATURE("forms-07");
    for (const char* name : {"Фамилия", "Имя", "Отчество", "Подпись", "Дата"}) {
        const auto& f = field(name);
        const RectF r = rectOf(f["rect"]);
        const float base = f["base"].get<float>();
        for (const float dy : {-8.0f, -2.0f, 3.0f}) {  // над линией, у самой линии и чуть ниже
            const PointF click{r.x0 + 30, base + dy};
            const Placement p = fillsign::placeText(anketa(), click);
            ASSERT_EQ(p.kind, FieldKind::Line) << name << " dy " << dy;
            EXPECT_LT(p.baseline, base) << name;
            EXPECT_GT(p.baseline, base - 4) << name;
            EXPECT_GE(p.fontSize, fillsign::kMinFontSize) << name;
            EXPECT_LE(p.fontSize, p.cellHeight) << name;
            EXPECT_LE(p.fontSize, fillsign::kMaxFontSize) << name;
            EXPECT_NEAR(p.x, click.x, 0.5f) << name;  // подчёркивание — от точки щелчка
            EXPECT_NEAR(p.field.x1, r.x1, 1.5f) << name;
            EXPECT_NEAR(p.field.x0, r.x0, 1.5f) << name;
            EXPECT_NEAR(p.field.y1, base, 1.0f) << name;
        }
    }
    // «Подпись» и «Дата» в одной строке — у каждой своя линия.
    const Placement date = fillsign::placeText(anketa(), {430, field("Дата")["base"].get<float>() - 6});
    EXPECT_NEAR(date.field.x0, 400, 1.5f);
    EXPECT_NEAR(date.field.x1, 520, 1.5f);
}

TEST(fillsign_placement, click_in_box_starts_text_at_its_left_edge) {
    for (const char* name : {"Серия", "Номер"}) {
        const RectF r = rectOf(field(name)["rect"]);
        const Placement p = fillsign::placeText(anketa(), {(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2});
        ASSERT_EQ(p.kind, FieldKind::Box) << name;
        EXPECT_NEAR(p.field.x0, r.x0, 1.5f) << name;
        EXPECT_NEAR(p.field.x1, r.x1, 1.5f) << name;
        EXPECT_NEAR(p.field.y0, r.y0, 1.5f) << name;
        EXPECT_NEAR(p.field.y1, r.y1, 1.5f) << name;
        EXPECT_NEAR(p.x, r.x0 + 2, 1.5f) << name;
        EXPECT_LE(p.fontSize, r.height()) << name;
        EXPECT_GT(p.baseline, r.y0 + p.fontSize * 0.5f) << name;
        EXPECT_LT(p.baseline, r.y1) << name;
    }
}

TEST(fillsign_placement, combs_follow_their_step) {
    OT_FEATURE("forms-07");
    for (const char* name : {"Дата рождения", "ИНН"}) {
        const auto& f = field(name);
        const auto& cells = f["cells"];
        const RectF r = rectOf(f["rect"]);
        const Placement p = fillsign::placeText(anketa(), {r.x0 + 30, f["base"].get<float>() - 4});
        ASSERT_EQ(p.kind, FieldKind::Comb) << name;
        ASSERT_EQ(p.cells.size(), cells.size()) << name;
        for (std::size_t i = 0; i < cells.size(); ++i) {
            const RectF want = rectOf(cells[i]);
            EXPECT_NEAR(p.cells[i].x0, want.x0, 1.0f) << name << " " << i;
            EXPECT_NEAR(p.cells[i].x1, want.x1, 1.0f) << name << " " << i;
            EXPECT_NEAR(p.cells[i].y1, want.y1, 1.0f) << name << " " << i;
        }
        EXPECT_LE(p.fontSize, p.cellHeight) << name;
        EXPECT_GE(p.fontSize, fillsign::kMinFontSize) << name;
    }

    // «01021990» — 8 полей по центру клеток с шагом 18; лишнее отбрасывается.
    const auto& date = field("Дата рождения");
    const Placement p = fillsign::placeText(anketa(), {200, date["base"].get<float>() - 6});
    const std::vector<ot::annot::AnnotProps> items =
        fillsign::combProps(p, U"0102199077", ot::annot::AnnotColor::gray(0));
    ASSERT_EQ(items.size(), 8u);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const RectF cell = rectOf(date["cells"][i]);
        EXPECT_NEAR((items[i].rect.x0 + items[i].rect.x1) / 2, (cell.x0 + cell.x1) / 2, 0.5f) << i;
        EXPECT_EQ(items[i].contents.size(), 1u);
        EXPECT_EQ(items[i].fontName, fillsign::kCombFont);
        EXPECT_EQ(items[i].align, 1);
        EXPECT_FLOAT_EQ(items[i].borderWidth, 0);
        if (i > 0) {
            EXPECT_NEAR(items[i].rect.x0 - items[i - 1].rect.x0, 18.0f, 0.5f);
        }
    }
    EXPECT_EQ(items[0].contents, "0");
    EXPECT_EQ(items[7].contents, "0");
}

TEST(fillsign_placement, marks_go_into_check_boxes) {
    const auto& f = field("Согласен на обработку данных");
    const RectF box = rectOf(f["rect"]);
    const Placement p =
        fillsign::placeMark(anketa(), {(box.x0 + box.x1) / 2 + 2, (box.y0 + box.y1) / 2 - 1}, 12);
    ASSERT_EQ(p.kind, FieldKind::Check);
    EXPECT_NEAR(p.field.x0, box.x0, 1.5f);
    EXPECT_NEAR(p.field.y1, box.y1, 1.5f);
    EXPECT_LE(p.fontSize, box.height());
    const ot::annot::AnnotProps check =
        fillsign::markProps(fillsign::Mark::Check, p, ot::annot::AnnotColor::gray(0));
    EXPECT_EQ(check.contents, "✓");
    EXPECT_EQ(check.fontName, fillsign::kMarkFont);
    EXPECT_EQ(check.type, ot::annot::AnnotType::FreeText);
    EXPECT_EQ(fillsign::markText(fillsign::Mark::Cross), "✕");
    EXPECT_EQ(fillsign::markText(fillsign::Mark::Dot), "●");
    EXPECT_EQ(fillsign::markText(fillsign::Mark::Line), "—");
    EXPECT_EQ(fillsign::markText(fillsign::Mark::Box), "▭");
    // Вне флажка — кегль size центром в точке.
    const Placement free = fillsign::placeMark(anketa(), {450, 60}, 16);
    EXPECT_EQ(free.kind, FieldKind::None);
    EXPECT_FLOAT_EQ(free.fontSize, 16);
    EXPECT_NEAR((free.field.x0 + free.field.x1) / 2, 450, 0.5f);
}

TEST(fillsign_placement, far_from_fields_is_not_snapped) {
    const Placement p = fillsign::placeText(anketa(), {450, 60}, 11);
    EXPECT_EQ(p.kind, FieldKind::None);
    EXPECT_FLOAT_EQ(p.fontSize, 11);
    EXPECT_NEAR(p.x, 450, 0.5f);
    EXPECT_GT(p.baseline, 60);  // щелчок — середина строки
    EXPECT_LT(p.baseline, 66);
}

TEST(fillsign_placement, created_text_comb_and_marks_render_in_place) {
    OT_FEATURE("comment-09");
    ot::test::TempDir dir;
    const fs::path path = dir.path() / "anketa.pdf";
    fs::copy_file(flatDir() / "anketa.pdf", path);
    auto doc = Document::openOrThrow(path);
    const fillsign::PageGeometry g = geometryOf(*doc);
    const float pageWidth = 595;

    // Текст на подчёркивании «Фамилия»: чернила над линией, без рамки и заливки.
    const auto& surname = field("Фамилия");
    const float base = surname["base"].get<float>();
    const Placement p = fillsign::placeText(g, {200, base - 6});
    ot::annot::AnnotProps text = fillsign::textProps(p, "Иванов", ot::annot::AnnotColor::gray(0), pageWidth);
    EXPECT_EQ(text.type, ot::annot::AnnotType::FreeText);
    EXPECT_FLOAT_EQ(text.borderWidth, 0);
    EXPECT_TRUE(text.color.empty());
    EXPECT_EQ(text.intent, "FreeTextTypeWriter");
    EXPECT_FLOAT_EQ(text.fontSize, p.fontSize);
    EXPECT_LE(text.rect.x1, p.field.x1 + 0.5f);
    const ot::annot::AnnotRef ref = ot::annot::create(*doc, 0, text);
    ASSERT_TRUE(ref.valid());
    const Ink ink = inkIn(*doc, {180, base - 30, 520, base + 6});
    ASSERT_GT(ink.count, 30);
    EXPECT_LE(ink.box.y1, base + 0.5f);              // буквы стоят на линии…
    EXPECT_GE(ink.box.y1, base - 4);                 // …а не висят над ней
    EXPECT_GE(ink.box.y0, base - p.cellHeight - 1);  // и помещаются в клетку
    EXPECT_NEAR(ink.box.x0, 200 + 2, 2.5f);

    // Гребёнка одним шагом: undo снимает все 8 полей.
    const auto& dateField = field("Дата рождения");
    const Placement comb = fillsign::placeText(g, {190, dateField["base"].get<float>() - 6});
    ASSERT_EQ(comb.kind, FieldKind::Comb);
    const std::size_t before = ot::annot::listAnnots(*doc, 0).size();
    const auto refs =
        fillsign::createAll(*doc, 0, fillsign::combProps(comb, U"01021990", ot::annot::AnnotColor::gray(0)));
    EXPECT_EQ(refs.size(), 8u);
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), before + 8);
    for (std::size_t i = 0; i < 8; ++i) {
        const RectF cell = rectOf(dateField["cells"][i]);
        EXPECT_GT(inkIn(*doc, {cell.x0 + 1, cell.y0 + 1, cell.x1 - 1, cell.y1 - 1}).count, 10) << i;
    }
    doc->undoStack().undo();
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), before);
    doc->undoStack().redo();
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), before + 8);

    // Пять отметок — глифы шрифта отметок видны (не пустые квадраты и не пропуски).
    float x = 300;
    for (const fillsign::Mark mark : {fillsign::Mark::Check, fillsign::Mark::Cross, fillsign::Mark::Dot,
                                      fillsign::Mark::Line, fillsign::Mark::Box}) {
        const Placement at = fillsign::placeMark(g, {x, 60}, 16);
        const ot::annot::AnnotProps props =
            fillsign::markProps(mark, at, ot::annot::AnnotColor::rgb(0, 0, 1));
        const ot::annot::AnnotRef markRef = ot::annot::create(*doc, 0, props);
        // Без выноски: /CL нет (MuPDF 1.28.5 pdf_set_annot_rect пишет его любому FreeText — ядро снимает).
        EXPECT_TRUE(ot::annot::readProps(*doc, markRef)->callout.empty()) << fillsign::markText(mark);
        const Ink m = inkIn(*doc, {x - 12, 44, x + 12, 76});
        EXPECT_GT(m.count, 12) << fillsign::markText(mark);
        // Глиф — около точки щелчка.
        EXPECT_NEAR((m.box.x0 + m.box.x1) / 2, x, 3.0f) << fillsign::markText(mark);
        EXPECT_NEAR((m.box.y0 + m.box.y1) / 2, 60, 4.0f) << fillsign::markText(mark);
        x += 40;
    }
    // Во флажке — по центру квадрата.
    const RectF box = rectOf(field("Нужна справка")["rect"]);
    const Placement in = fillsign::placeMark(g, {(box.x0 + box.x1) / 2, (box.y0 + box.y1) / 2}, 16);
    ot::annot::create(*doc, 0,
                      fillsign::markProps(fillsign::Mark::Check, in, ot::annot::AnnotColor::gray(0)));
    const Ink c = inkIn(*doc, {box.x0 - 4, box.y0 - 4, box.x1 + 4, box.y1 + 4});
    EXPECT_GT(c.count, 8);
    EXPECT_NEAR((c.box.x0 + c.box.x1) / 2, (box.x0 + box.x1) / 2, 2.0f);
    EXPECT_NEAR((c.box.y0 + c.box.y1) / 2, (box.y0 + box.y1) / 2, 2.5f);
}
