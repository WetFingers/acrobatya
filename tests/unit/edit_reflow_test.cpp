// unit.edit_reflow.* — перевёрстка абзаца (план 3.3, п. 2; эталон 3.9 «Модель»; edit-01, edit-03 — интервалы
// и масштаб): ожидания tests/fixtures/edit/reflow.json (посчитаны вручную на моноширинных символах: перенос
// по словам и по символам, четыре выравнивания, отступ и выступ первой строки, жёсткий разрыв, разные кегли в
// строке, пустой абзац, наложение на соседние блоки); абзацы фикстур генератора с исходными ширинами глифов
// верстаются в те же строки и позиции (с Tc, Tw, Tz, по ширине Tw и TJ); вставка слова в середину первой
// строки переносит хвост — строк на одну больше, ширина набора не превышена; межстрочный интервал задаётся;
// рост абзаца вниз даёт наложение на следующий блок.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/edit/content_model.h"
#include "core/edit/paragraph_editor.h"
#include "core/edit/reflow.h"
#include "core/edit/text_blocks.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::edit::Align;
using ot::edit::ContentModel;
using ot::edit::ParagraphEditor;
using ot::edit::ReflowChar;
using ot::edit::ReflowParams;
using ot::edit::ReflowResult;
using ot::edit::TextBlock;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

Align alignOf(const std::string& s) {
    if (s == "right") {
        return Align::Right;
    }
    if (s == "center") {
        return Align::Center;
    }
    if (s == "justify") {
        return Align::Justify;
    }
    return Align::Left;
}

std::vector<ReflowChar> monospace(const json& c) {
    const std::u32string text = ot::utf8::decode(c["text"].get<std::string>());
    std::map<int, float> sizes;
    if (c.contains("sizes")) {
        for (const auto& [k, v] : c["sizes"].items()) {
            sizes[std::stoi(k)] = v.get<float>();
        }
    }
    std::vector<ReflowChar> chars;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto it = sizes.find(static_cast<int>(i));
        const float size = it != sizes.end() ? it->second : 10.0f;
        ReflowChar r;
        r.ch = text[i];
        r.size = size;
        r.advance = ot::edit::isHardBreak(r.ch) ? 0 : size;
        r.ascent = 0.8f * size;
        r.descent = 0.2f * size;
        chars.push_back(r);
    }
    return chars;
}

ReflowParams paramsOf(const json& j) {
    ReflowParams p;
    p.left = j.value("left", 0.0f);
    p.width = j.value("width", 0.0f);
    p.firstIndent = j.value("firstIndent", 0.0f);
    p.firstBaseline = j.value("firstBaseline", 0.0f);
    p.lineSpacing = j.value("lineSpacing", 1.2f);
    p.align = alignOf(j.value("align", std::string("left")));
    p.emptySize = j.value("emptySize", 12.0f);
    if (j.contains("obstacles")) {
        for (const auto& r : j["obstacles"]) {
            p.obstacles.push_back(
                {r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()});
        }
    }
    return p;
}

struct Page {
    std::unique_ptr<Document> doc;
    ContentModel model;
    std::vector<TextBlock> blocks;
};

Page load(const std::string& name) {
    Page p;
    p.doc = Document::openOrThrow(editFixture(name));
    p.model = ContentModel::build(*p.doc, 0);
    p.blocks = ot::edit::blocksForPage(p.model);
    return p;
}

// Ширины символов по исходным глифам: у набранного символа и добавленного пробела — ширина такого же символа
// того же стиля из файла (тест берёт только буквы, которые в абзаце есть).
std::vector<ReflowChar> measured(const ParagraphEditor& e) {
    std::map<std::pair<int, char32_t>, float> known;
    for (const auto& c : e.chars()) {
        if (!c.code.empty() && c.group == 1) {
            known.emplace(std::make_pair(c.style, c.ch), c.w0);
        }
    }
    std::vector<ReflowChar> out;
    for (const auto& c : e.chars()) {
        const auto& st = e.styles()[static_cast<std::size_t>(c.style)];
        float w0 = c.w0;
        if (c.code.empty()) {
            const auto it = known.find({c.style, c.ch});
            EXPECT_NE(it, known.end()) << "нет ширины U+" << std::hex << static_cast<int>(c.ch);
            w0 = it != known.end() ? it->second : 0.5f;
        }
        ReflowChar r;
        r.ch = c.ch;
        r.advance = c.group == 0 ? 0 : ot::edit::naturalAdvance(c.ch, w0, st);
        r.width = c.group == 0 ? 0 : w0 * st.size * st.hscale;
        r.size = st.size;
        r.ascent = 0.8f * st.size;
        r.descent = 0.2f * st.size;
        out.push_back(r);
    }
    // Лигатура: ширина глифа — на первом символе, продолжения — нулевые.
    return out;
}

std::vector<ot::RectF> otherBlocks(const ParagraphEditor& e, const std::vector<TextBlock>& blocks,
                                   const TextBlock& self) {
    std::vector<ot::RectF> rects;
    for (const auto& b : blocks) {
        if (b.spans != self.spans) {
            rects.push_back(b.bbox);
        }
    }
    return ot::edit::toLayoutRects(e, rects);
}

}  // namespace

TEST(edit_reflow, json_cases) {
    OT_FEATURE("edit-01");
    const json meta = json::parse(ot::paths::readFile(editFixture("reflow.json")));
    for (const json& c : meta["cases"]) {
        SCOPED_TRACE(c["name"].get<std::string>());
        const ReflowResult r = ot::edit::reflow(monospace(c), paramsOf(c["params"]));
        const auto& lines = r.layout.lines;
        ASSERT_EQ(lines.size(), c["lines"].size());
        for (std::size_t i = 0; i < lines.size(); ++i) {
            SCOPED_TRACE(i);
            const json& e = c["lines"][i];
            EXPECT_EQ(lines[i].begin, e["begin"].get<int>());
            EXPECT_EQ(lines[i].end, e["end"].get<int>());
            EXPECT_NEAR(lines[i].x0, e["x0"].get<float>(), 1e-3);
            EXPECT_NEAR(lines[i].x1, e["x1"].get<float>(), 1e-3);
            EXPECT_NEAR(lines[i].baseline, e["baseline"].get<float>(), 1e-3);
            EXPECT_NEAR(lines[i].wordSpace, e.value("wordSpace", 0.0f), 1e-3);
            EXPECT_EQ(lines[i].hardBreak, e.value("hardBreak", false));
            if (e.contains("ascent")) {
                EXPECT_NEAR(lines[i].ascent, e["ascent"].get<float>(), 1e-3);
                EXPECT_NEAR(lines[i].descent, e["descent"].get<float>(), 1e-3);
            }
        }
        const std::size_t n = ot::utf8::decode(c["text"].get<std::string>()).size();
        ASSERT_EQ(r.layout.x.size(), n);
        ASSERT_EQ(r.layout.advance.size(), n);
        if (c.contains("x")) {
            for (std::size_t i = 0; i < n; ++i) {
                EXPECT_NEAR(r.layout.x[i], c["x"][i].get<float>(), 1e-3) << i;
            }
        }
        if (c.contains("advance")) {
            for (std::size_t i = 0; i < n; ++i) {
                EXPECT_NEAR(r.layout.advance[i], c["advance"][i].get<float>(), 1e-3) << i;
            }
        }
        if (c.contains("bounds")) {
            EXPECT_NEAR(r.layout.bounds.x0, c["bounds"][0].get<float>(), 1e-3);
            EXPECT_NEAR(r.layout.bounds.y0, c["bounds"][1].get<float>(), 1e-3);
            EXPECT_NEAR(r.layout.bounds.x1, c["bounds"][2].get<float>(), 1e-3);
            EXPECT_NEAR(r.layout.bounds.y1, c["bounds"][3].get<float>(), 1e-3);
        }
        EXPECT_EQ(r.overlaps, c.value("overlaps", std::vector<int>{}));
        // Ни одна строка не шире набора (кроме слова из одного символа).
        const ReflowParams p = paramsOf(c["params"]);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const float left = p.left + (i == 0 ? p.firstIndent : 0.0f);
            EXPECT_LE(lines[i].x1 - left, p.width - (i == 0 ? p.firstIndent : 0.0f) + 1e-3) << i;
        }
    }
}

// Нетронутый абзац с исходными ширинами — те же строки и позиции, что в файле (генератор набирает так же:
// жадный перенос, естественные ширины, Tc, Tw, Tz; по ширине — Tw или сдвиги TJ).
TEST(edit_reflow, fixtures_reproduce_layout) {
    OT_FEATURE("edit-03");
    int blocks = 0;
    for (const char* name :
         {"latin.pdf", "cyr-cid.pdf", "base14.pdf", "align-left.pdf", "align-right.pdf", "align-center.pdf",
          "align-justify.pdf", "columns-2.pdf", "columns-3.pdf", "tz-tc-tw.pdf"}) {
        SCOPED_TRACE(name);
        const Page p = load(name);
        for (std::size_t bi = 0; bi < p.blocks.size(); ++bi) {
            const TextBlock& b = p.blocks[bi];
            if (!b.editable()) {
                continue;
            }
            SCOPED_TRACE(b.text);
            const ParagraphEditor e = ParagraphEditor::fromBlock(p.model, b, p.blocks);
            const ReflowResult r = ot::edit::reflow(measured(e), ot::edit::reflowParams(e));
            const ot::edit::TextLayout& was = e.layout();
            ASSERT_EQ(r.layout.lines.size(), was.lines.size());
            for (std::size_t i = 0; i < was.lines.size(); ++i) {
                EXPECT_EQ(r.layout.lines[i].begin, was.lines[i].begin) << i;
                EXPECT_EQ(r.layout.lines[i].end, was.lines[i].end) << i;
                EXPECT_NEAR(r.layout.lines[i].baseline, was.lines[i].baseline, 0.02) << i;
                EXPECT_NEAR(r.layout.lines[i].x0, was.lines[i].x0, 0.05) << i;
                EXPECT_NEAR(r.layout.lines[i].x1, was.lines[i].x1, 0.05) << i;
            }
            for (int i = 0; i < e.size(); ++i) {
                if (!e.chars()[static_cast<std::size_t>(i)].code.empty()) {
                    EXPECT_NEAR(r.layout.x[static_cast<std::size_t>(i)], was.x[static_cast<std::size_t>(i)],
                                0.05)
                        << i;
                }
            }
            EXPECT_TRUE(r.overlaps.empty());
            ++blocks;
        }
    }
    EXPECT_GE(blocks, 20);
}

TEST(edit_reflow, insert_word_wraps_tail) {
    OT_FEATURE("edit-01");
    for (const char* name : {"latin.pdf", "cyr-cid.pdf", "align-justify.pdf", "columns-2.pdf"}) {
        SCOPED_TRACE(name);
        const Page p = load(name);
        const TextBlock& b = p.blocks.front();
        ParagraphEditor e = ParagraphEditor::fromBlock(p.model, b, p.blocks);
        const ReflowParams params = ot::edit::reflowParams(e);
        const int lines = e.lineCount();
        const auto first = e.layout().lines.front();
        // Слово из середины первой строки (его буквы есть в шрифте) вставляется перед собой по одному, пока
        // последней строке не перестанет хватать места: строк становится ровно на одну больше (слово короче
        // строки — за раз не больше одной).
        const ParagraphEditor original = e;
        const ot::edit::TextRange word = e.wordAt(first.begin + (first.end - first.begin) / 2);
        ASSERT_FALSE(word.empty());
        const std::u32string piece = e.text(word) + U" ";
        const int at = word.begin;
        int inserted = 0;
        ReflowResult r;
        for (int guard = 0; guard < 200; ++guard) {
            e.insert(at, piece);
            inserted += static_cast<int>(piece.size());
            r = ot::edit::reflow(measured(e), params);
            ASSERT_LE(static_cast<int>(r.layout.lines.size()), lines + 1);
            if (static_cast<int>(r.layout.lines.size()) == lines + 1) {
                break;
            }
        }
        EXPECT_EQ(static_cast<int>(r.layout.lines.size()), lines + 1);
        // Хвост первой строки ушёл вниз: первая строка кончается раньше, чем исходная со вставкой.
        EXPECT_LT(r.layout.lines.front().end, first.end + inserted);
        for (std::size_t i = 0; i < r.layout.lines.size(); ++i) {
            const float left = params.left + (i == 0 ? params.firstIndent : 0.0f);
            EXPECT_LE(r.layout.lines[i].x1, params.left + params.width + 0.01f) << i;
            EXPECT_GE(r.layout.lines[i].x0, left - 0.01f) << i;
        }
        // До вставки — как было.
        for (int i = 0; i < at; ++i) {
            EXPECT_NEAR(r.layout.x[static_cast<std::size_t>(i)],
                        original.layout().x[static_cast<std::size_t>(i)],
                        e.alignment() == Align::Justify ? 50.0 : 0.05)
                << i;
        }
        e.setLayout(r.layout);
        EXPECT_TRUE(e.layoutValid());
        EXPECT_EQ(e.lineCount(), lines + 1);
    }
}

TEST(edit_reflow, line_spacing) {
    OT_FEATURE("edit-03");
    const Page p = load("latin.pdf");
    ParagraphEditor e = ParagraphEditor::fromBlock(p.model, p.blocks.front(), p.blocks);
    ASSERT_EQ(e.lineCount(), 2);
    const float size = e.styleAt(0).size;
    EXPECT_NEAR(ot::edit::reflow(measured(e), ot::edit::reflowParams(e)).layout.lines[1].baseline -
                    e.layout().lines[0].baseline,
                e.geometry().leading, 0.02);
    e.setLineSpacing(2.0f);
    const ReflowResult r = ot::edit::reflow(measured(e), ot::edit::reflowParams(e));
    EXPECT_NEAR(r.layout.lines[0].baseline, e.geometry().firstBaseline, 1e-4);  // первая на месте
    EXPECT_NEAR(r.layout.lines[1].baseline - r.layout.lines[0].baseline, 2.0f * size, 1e-3);
    // Выравнивание по центру и вправо — сдвиг строк, перенос тот же.
    e.setAlignment(Align::Right);
    const ReflowResult right = ot::edit::reflow(measured(e), ot::edit::reflowParams(e));
    const ReflowParams params = ot::edit::reflowParams(e);
    for (const auto& l : right.layout.lines) {
        EXPECT_NEAR(l.x1, params.left + params.width, 0.01);
    }
}

TEST(edit_reflow, growth_overlaps_next_block) {
    const Page p = load("latin.pdf");
    const TextBlock& b = p.blocks.front();
    ParagraphEditor e = ParagraphEditor::fromBlock(p.model, b, p.blocks);
    const auto obstacles = otherBlocks(e, p.blocks, b);
    ASSERT_EQ(obstacles.size(), p.blocks.size() - 1);
    EXPECT_TRUE(ot::edit::reflow(measured(e), ot::edit::reflowParams(e, obstacles)).overlaps.empty());
    // Абзац вырос на две строки — третья строка встала на место следующего блока.
    const std::u32string tail = e.text({e.layout().lines[1].begin, e.size()});
    int lines = e.lineCount();
    while (lines < 4) {
        e.insert(e.size(), U" " + tail);
        lines =
            static_cast<int>(ot::edit::reflow(measured(e), ot::edit::reflowParams(e)).layout.lines.size());
    }
    const ReflowResult r = ot::edit::reflow(measured(e), ot::edit::reflowParams(e, obstacles));
    ASSERT_FALSE(r.overlaps.empty());
    EXPECT_EQ(r.overlaps.front(), 0);  // следующий блок — первый из остальных
    // Первая строка не сдвинулась (абзац растёт вниз).
    EXPECT_NEAR(r.layout.lines[0].baseline, e.geometry().firstBaseline, 1e-4);
}
