// unit.stext_vectors.* — векторы и структура текстового слоя (план 1.4): без опций векторов нет; с
// TextPage::Options{collectVectors} подчёркивания-отрезки, рамка и заливка vectors.pdf видны поштучно (число
// — по text.json генератора), отрезок лежит под своей подписью; без collectStructure узлы — только анализ
// MuPDF (SEGMENT, списки), с ним — дерево тегов файла Word, блоки знают свой узел; tableHunt и dehyphenate не
// ломают извлечение.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::TextOptions;
using ot::TextPage;

namespace fs = std::filesystem;

namespace {

fs::path textFixture(const char* name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "text" / name;
}

nlohmann::json readJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return nlohmann::json::parse(in);
}

}  // namespace

TEST(stext_vectors, not_collected_by_default) {
    auto doc = Document::openOrThrow(textFixture("vectors.pdf"));
    const auto text = TextPage::build(*doc, 0);
    EXPECT_TRUE(text->vectors().empty());
    for (const auto& b : text->blocks()) {
        EXPECT_NE(b.kind, ot::TextBlockKind::Vector);
    }
    EXPECT_TRUE(text->hasText());
}

TEST(stext_vectors, underlines_frames_and_fills) {
    OT_FEATURE("comment-02");
    const auto meta = readJson(textFixture("text.json"))["files"]["vectors.pdf"];
    const auto& expected = meta["vectors"];
    auto doc = Document::openOrThrow(textFixture("vectors.pdf"));
    TextOptions options;
    options.collectVectors = true;
    const auto text = TextPage::build(*doc, 0, options);

    const float lw = expected["underline_width"].get<float>();
    int segments = 0, strokedRects = 0, filledRects = 0;
    std::vector<ot::RectF> lines;
    for (const auto& v : text->vectors()) {
        ASSERT_TRUE(v.rectangle);
        // MuPDF расширяет рамку отрезка на толщину линии с каждой стороны (полтолщины обводки и столько же
        // запаса): подчёркивание 160 → 400 толщиной lw — рамка (240 + 2·lw) × 2·lw.
        if (v.stroked && v.bbox.height() <= 2 * lw + 0.01f) {
            ++segments;
            lines.push_back(v.bbox);
            EXPECT_NEAR(v.bbox.width(), 240.0f + 2 * lw, 0.01f);
        } else if (v.stroked) {
            ++strokedRects;
        } else {
            ++filledRects;
            EXPECT_EQ(v.argb, 0xFF3366CCu);  // 0.2 0.4 0.8 rg
        }
    }
    EXPECT_EQ(segments, expected["underlines"].get<int>());
    EXPECT_EQ(strokedRects, expected["stroked_rects"].get<int>());
    EXPECT_EQ(filledRects, expected["filled_rects"].get<int>());
    EXPECT_EQ(static_cast<int>(text->vectors().size()), segments + strokedRects + filledRects);

    // Каждый векторный блок указывает на свой элемент; отрезок — на 2 пт ниже базовой линии подписи.
    int vectorBlocks = 0;
    for (const auto& b : text->blocks()) {
        if (b.kind == ot::TextBlockKind::Vector) {
            ASSERT_GE(b.vector, 0);
            ++vectorBlocks;
        }
    }
    EXPECT_EQ(vectorBlocks, static_cast<int>(text->vectors().size()));
    int matched = 0;
    for (const auto& l : meta["lines"]) {
        if (l["name"] != "label") {
            continue;
        }
        const float y = l["baseline"].get<float>() + 2.0f;
        for (const auto& r : lines) {
            if (std::abs((r.y0 + r.y1) / 2 - y) < 0.05f) {
                ++matched;
            }
        }
    }
    EXPECT_EQ(matched, expected["underlines"].get<int>());
}

TEST(stext_vectors, structure_tree_of_tagged_file) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests/fixtures/cyr/word-truetype.pdf");
    // Без collectStructure узлы — только анализ MuPDF (разбиение SEGMENT — Div «Split», списки
    // PARAGRAPH_BREAK — LI), абзацев-тегов файла нет.
    const auto plain = TextPage::build(*doc, 0);
    for (const auto& node : plain->structure()) {
        EXPECT_NE(node.type, "P");
    }

    TextOptions options;
    options.collectStructure = true;
    const auto text = TextPage::build(*doc, 0, options);
    ASSERT_FALSE(text->structure().empty());
    int paragraphs = 0;
    for (std::size_t i = 0; i < text->structure().size(); ++i) {
        const auto& node = text->structure()[i];
        EXPECT_FALSE(node.type.empty());
        if (node.parent >= 0) {
            const auto& kids = text->structure()[node.parent].children;
            EXPECT_NE(std::find(kids.begin(), kids.end(), static_cast<int>(i)), kids.end());
        }
        paragraphs += node.type == "P" ? 1 : 0;
    }
    EXPECT_GT(paragraphs, 0);
    int inStructure = 0;
    for (const auto& b : text->blocks()) {
        if (b.kind == ot::TextBlockKind::Text && b.structNode >= 0) {
            ++inStructure;
        }
    }
    EXPECT_GT(inStructure, 0);
    // Текст тот же, меняется только обход.
    EXPECT_EQ(text->chars().size(), plain->chars().size());
}

TEST(stext_vectors, table_hunt_and_dehyphenate_keep_text) {
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto plain = TextPage::build(*doc, 0);
    TextOptions options;
    options.tableHunt = true;
    options.dehyphenate = true;
    const auto text = TextPage::build(*doc, 0, options);
    EXPECT_EQ(text->words().size(), plain->words().size());
    EXPECT_NE(text->text().find("Первая строка для выделения."), std::string::npos);
}
