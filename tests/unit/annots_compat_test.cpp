// Файлы для compat.annots (план 5.1, п. 5; эталон 3.4 «Ядро», 9.4): annots_compat.DISABLED_write пишет в
// $OTTISK_COMPAT_OUT/annots — matrix.pdf (annots-all.pdf с запиской, записанный инкрементом), created.pdf (15
// создаваемых типов на пустой странице, инкрементом), наш рендер страницы 144 dpi (*.ours.png) и regions.json
// — рамки аннотаций с внешним видом в пикселях; tools/compat_annots.py рендерит их pypdfium2 и сверяет imgcmp
// --cross в каждой рамке; 5.2 — stamps.pdf: свои штампы assets/stamps (векторный вид из display list,
// динамический со строками, повёрнутый /Matrix) и штамп-картинка, инкрементом. Отключён: запускает ctest
// compat.annots.generate.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/annot/stamps.h"
#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::annot::AnnotColor;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;

namespace {

constexpr float kDpi = 144;

AnnotProps compatProps(AnnotType type, int index) {
    AnnotProps p = ot::annot::toolDefaults(type);
    p.author = "compat";
    p.contents = "c-" + std::string(ot::annot::annotTypeName(type));
    // Сетка 3 × 5 ячеек 150 × 130 пт от (40, 60).
    const float x = 40 + static_cast<float>(index % 3) * 180;
    const float y = 60 + static_cast<float>(index / 3) * 150;
    const ot::RectF cell{x + 20, y + 20, x + 150, y + 110};
    switch (type) {
        case AnnotType::Text:
        case AnnotType::FileAttachment:
            p.rect = {cell.x0, cell.y0, cell.x0 + 24, cell.y0 + 24};
            break;
        case AnnotType::Caret:
            p.rect = {cell.x0, cell.y0, cell.x0 + 30, cell.y0 + 30};
            break;
        case AnnotType::FreeText:
            p.rect = cell;
            p.contents = "Привет, мир";
            p.fontSize = 18;
            p.borderWidth = 1;
            p.color = AnnotColor::rgb(0, 0, 0);
            break;
        case AnnotType::Line:
            p.lineStart = {cell.x0, cell.y1};
            p.lineEnd = {cell.x1, cell.y0};
            p.lineEndStyle = ot::annot::LineEnding::ClosedArrow;
            p.borderWidth = 2;
            break;
        case AnnotType::Polygon:
        case AnnotType::PolyLine:
            p.vertices = {{cell.x0, cell.y1}, {(cell.x0 + cell.x1) / 2, cell.y0}, {cell.x1, cell.y1}};
            p.borderWidth = 2;
            break;
        case AnnotType::Highlight:
        case AnnotType::Underline:
        case AnnotType::Squiggly:
        case AnnotType::StrikeOut:
            p.quads = {{{cell.x0, cell.y0 + 30},
                        {cell.x1, cell.y0 + 30},
                        {cell.x0, cell.y0 + 60},
                        {cell.x1, cell.y0 + 60}}};
            break;
        case AnnotType::Ink:
            p.ink = {
                {{cell.x0, cell.y1}, {cell.x0 + 40, cell.y0}, {cell.x0 + 80, cell.y1}, {cell.x1, cell.y0}}};
            p.borderWidth = 3;
            break;
        case AnnotType::Stamp:
            p.rect = cell;
            p.icon = "Approved";
            break;
        default:
            p.rect = cell;
            p.borderWidth = 2;
            break;
    }
    if (type == AnnotType::FileAttachment) {
        p.fileName = "note.txt";
        p.fileData = "attachment";
    }
    return p;
}

// Наш рендер и рамки аннотаций с внешним видом (комментарии; без скрытых).
nlohmann::json describe(const fs::path& out, const std::string& name) {
    auto doc = ot::Document::openOrThrow(out / (name + ".pdf"));
    const ot::Bitmap bitmap = doc->renderPage(0, {.dpi = kDpi});
    ot::savePng(ot::MuContext::global(), bitmap, out / (name + ".ours.png"));
    nlohmann::json regions = nlohmann::json::array();
    nlohmann::json skipped = nlohmann::json::array();
    const float k = kDpi / 72.0f;
    for (const ot::annot::AnnotInfo& a : ot::annot::listAnnots(*doc, 0)) {
        if (!ot::annot::isComment(a.type) || !a.hasAppearance || a.hiddenOnScreen()) {
            continue;
        }
        // Чужой значок NoZoom, у которого /Rect больше /BBox вида: MuPDF рисует его в размере /BBox, pdfium и
        // Acrobat вписывают в /Rect — сверять нечего (наши значки создаются с /Rect = /BBox).
        const auto props = ot::annot::readProps(*doc, a.ref);
        if ((a.flags & ot::annot::kAnnotNoZoom) != 0 && props &&
            std::abs(props->rect.width() - a.rect.width()) > 0.5f) {
            skipped.push_back(
                {{"subtype", a.subtype}, {"nm", a.ref.nm}, {"reason", "nozoom-rect-larger-than-bbox"}});
            continue;
        }
        const int x0 = std::max(0, static_cast<int>(std::floor(a.rect.x0 * k)) - 2);
        const int y0 = std::max(0, static_cast<int>(std::floor(a.rect.y0 * k)) - 2);
        const int x1 = std::min(bitmap.width, static_cast<int>(std::ceil(a.rect.x1 * k)) + 2);
        const int y1 = std::min(bitmap.height, static_cast<int>(std::ceil(a.rect.y1 * k)) + 2);
        regions.push_back({{"subtype", a.subtype}, {"nm", a.ref.nm}, {"box", {x0, y0, x1, y1}}});
    }
    return {{"pdf", name + ".pdf"}, {"png", name + ".ours.png"}, {"page", 0},
            {"dpi", kDpi},          {"regions", regions},        {"skipped", skipped}};
}

}  // namespace

TEST(annots_compat, DISABLED_write) {
    const char* root = std::getenv("OTTISK_COMPAT_OUT");
    if (root == nullptr || *root == '\0') {
        GTEST_SKIP() << "OTTISK_COMPAT_OUT не задан";
    }
    const fs::path out = ot::paths::fromUtf8(root) / "annots";
    fs::remove_all(out);
    fs::create_directories(out);

    // Матрица 28 подтипов + записка, инкрементом.
    const fs::path matrix = out / "matrix.pdf";
    fs::copy_file(ot::test::sourceDir() / "tests" / "fixtures" / "annots" / "annots-all.pdf", matrix);
    {
        auto doc = ot::Document::openOrThrow(matrix);
        AnnotProps note = ot::annot::toolDefaults(AnnotType::Text);
        note.rect = {20, 20, 44, 44};
        note.contents = "compat";
        ot::annot::create(*doc, 0, note);
        const ot::SaveReport r = ot::Saver::save(*doc, matrix, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(r.ok()) << r.message;
    }
    // 15 создаваемых типов на пустой странице, инкрементом.
    const fs::path created = out / "created.pdf";
    ot::paths::writeFile(created, ot::test::makePdf(ot::MuContext::global(), 1));
    {
        auto doc = ot::Document::openOrThrow(created);
        const AnnotType types[] = {AnnotType::Text,     AnnotType::FreeText,  AnnotType::Line,
                                   AnnotType::Square,   AnnotType::Circle,    AnnotType::Polygon,
                                   AnnotType::PolyLine, AnnotType::Highlight, AnnotType::Underline,
                                   AnnotType::Squiggly, AnnotType::StrikeOut, AnnotType::Stamp,
                                   AnnotType::Caret,    AnnotType::Ink,       AnnotType::FileAttachment};
        int i = 0;
        for (const AnnotType t : types) {
            ot::annot::create(*doc, 0, compatProps(t, i++));
        }
        const ot::SaveReport r = ot::Saver::save(*doc, created, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(r.ok()) << r.message;
    }
    // Свои штампы (5.2), инкрементом.
    const fs::path stamps = out / "stamps.pdf";
    ot::paths::writeFile(stamps, ot::test::makePdf(ot::MuContext::global(), 1));
    {
        auto doc = ot::Document::openOrThrow(stamps);
        const fs::path assets = ot::test::sourceDir() / "assets" / "stamps";
        auto stampProps = [](ot::RectF rect) {
            AnnotProps p = ot::annot::toolDefaults(AnnotType::Stamp);
            p.author = "compat";
            p.rect = rect;
            return p;
        };
        ot::annot::StampSource approved{
            ot::paths::readFile(assets / "standard" / "approved.pdf"), 0, "#Approved", {}};
        ot::annot::createStamp(*doc, 0, approved, {}, stampProps({60, 60, 260, 122}));
        ot::annot::StampSource dynamic{
            ot::paths::readFile(assets / "dynamic" / "approved.pdf"),
            0,
            "#DApproved",
            {{"{name}", 100, 45.5f, 8, 1, 0x2E7D32}, {"{date} {time}", 100, 54.5f, 8, 1, 0x2E7D32}}};
        ot::annot::StampValues values;
        values.name = "Мария Иванова";
        values.date = "25.10.2026";
        values.time = "14:30";
        ot::annot::createStamp(*doc, 0, dynamic, values, stampProps({300, 60, 500, 124}));
        ot::annot::StampSource draft{ot::paths::readFile(assets / "standard" / "draft.pdf"), 0, "#Draft", {}};
        const ot::annot::AnnotRef turned =
            ot::annot::createStamp(*doc, 0, draft, {}, stampProps({80, 220, 280, 282}));
        ot::annot::setStampRotation(*doc, turned, 30);
        const std::string png =
            ot::paths::readFile(ot::test::sourceDir() / "tests" / "fixtures" / "images" / "logo.png");
        const ot::annot::StampSize size = ot::annot::imageStampSize(png);
        ot::annot::createImageStamp(*doc, 0, png, stampProps({340, 220, 340 + size.width, 220 + size.height}),
                                    "Logo");
        const ot::SaveReport r = ot::Saver::save(*doc, stamps, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(r.ok()) << r.message;
    }
    const nlohmann::json manifest = {
        {"files", {describe(out, "matrix"), describe(out, "created"), describe(out, "stamps")}}};
    ot::paths::writeFile(out / "regions.json", manifest.dump(2));
    EXPECT_EQ(manifest["files"][1]["regions"].size(), 15u);
    EXPECT_EQ(manifest["files"][2]["regions"].size(), 4u);
}
