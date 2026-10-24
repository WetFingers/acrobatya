// unit.text_blocks.* — строки, абзацы и блоки (план 3.1, п. 4; эталон 3.9 «Модель»): по ожиданиям генератора
// (tests/fixtures/edit/expected.json) — блоки своих фикстур совпадают по составу спанов, тексту, числу строк,
// границам (x0/x1, первая и последняя базовая линия ±0,5 pt), выравниванию и причине «только чтение»: абзацы
// из нескольких BT/ET, абзацы одного BT с отступом первой строки, две и три колонки (строки рядами), четыре
// выравнивания, Tc/Tw/Tz/Ts (индекс в той же строке), повёрнутый текст, XObject-форма; файлы Word — абзацы и
// ячейки таблицы находятся с выравниванием из задания и границами по PDFium (±2 pt); стили пробегов; страница
// с /Rotate 90 даёт те же абзацы.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/text_blocks.h"
#include "core/mu/call.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::edit::Align;
using ot::edit::blocksForPage;
using ot::edit::ContentModel;
using ot::edit::TextBlock;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

const json& expected() {
    static const json meta = json::parse(ot::paths::readFile(editFixture("expected.json")));
    return meta;
}

std::string squeeze(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!out.empty() && out.back() != ' ') {
                out.push_back(' ');
            }
        } else {
            out.push_back(c);
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

struct Page {
    std::unique_ptr<Document> doc;
    ContentModel model;
    std::vector<TextBlock> blocks;
};

Page load(const std::string& name, int page = 0) {
    Page p;
    p.doc = Document::openOrThrow(editFixture(name));
    p.model = ContentModel::build(*p.doc, page);
    p.blocks = blocksForPage(p.model);
    return p;
}

const TextBlock* blockWithSpan(const std::vector<TextBlock>& blocks, int span) {
    for (const auto& b : blocks) {
        if (std::find(b.spans.begin(), b.spans.end(), span) != b.spans.end()) {
            return &b;
        }
    }
    return nullptr;
}

// Блоки своих фикстур (у ожиданий есть состав спанов): 12 фикстур плана и ещё четыре.
const char* const kSynthetic[] = {
    "latin.pdf",      "cyr-cid.pdf",     "columns-2.pdf",    "columns-3.pdf",
    "align-left.pdf", "align-right.pdf", "align-center.pdf", "align-justify.pdf",
    "tz-tc-tw.pdf",   "rotated.pdf",     "xobject-form.pdf", "type1-diff.pdf",
    "base14.pdf",     "tj-kerning.pdf",  "two-streams.pdf",  "inline-image.pdf"};

}  // namespace

TEST(text_blocks, paragraphs_match_generator_expectations) {
    OT_FEATURE("edit-01");
    for (const char* name : kSynthetic) {
        SCOPED_TRACE(name);
        const Page p = load(name);
        const json& exp = expected()["fixtures"][name]["blocks"];
        EXPECT_EQ(p.blocks.size(), exp.size());
        for (const json& e : exp) {
            SCOPED_TRACE(e["text"].get<std::string>());
            const auto& spans = e["spans"];
            const TextBlock* b = blockWithSpan(p.blocks, spans[0].get<int>());
            ASSERT_NE(b, nullptr);
            std::vector<int> want;
            for (const json& s : spans) {
                want.push_back(s.get<int>());
            }
            std::vector<int> got = b->spans;
            std::sort(got.begin(), got.end());
            std::sort(want.begin(), want.end());
            EXPECT_EQ(got, want);
            EXPECT_EQ(b->text, e["text"].get<std::string>());
            EXPECT_STREQ(ot::edit::alignCode(b->align), e["align"].get<std::string>().c_str());
            EXPECT_STREQ(ot::edit::readOnlyCode(b->reason), e["readOnly"].get<std::string>().c_str());
            EXPECT_EQ(static_cast<int>(b->lines.size()), e["lines"].get<int>());
            if (e.contains("x0")) {
                float x0 = 1e9f;
                float x1 = -1e9f;
                for (const auto& l : b->lines) {
                    x0 = std::min(x0, l.start.x);
                    x1 = std::max(x1, l.end.x);
                }
                EXPECT_NEAR(x0, e["x0"].get<double>(), 0.5);
                EXPECT_NEAR(x1, e["x1"].get<double>(), 0.5);
                EXPECT_NEAR(b->lines.front().baseline, e["firstBaseline"].get<double>(), 0.5);
                EXPECT_NEAR(b->lines.back().baseline, e["lastBaseline"].get<double>(), 0.5);
                EXPECT_LE(b->bbox.x0, x0 + 0.01f);
                EXPECT_GE(b->bbox.x1, x1 - 0.01f);
                EXPECT_EQ(b->boundingRect().y0, b->bbox.y0);
            }
        }
    }
}

TEST(text_blocks, three_columns_are_three_blocks) {
    const Page p = load("columns-3.pdf");
    ASSERT_EQ(p.blocks.size(), 3u);
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(p.blocks[i].column, static_cast<int>(i));  // порядок чтения — колонки слева направо
        EXPECT_GE(p.blocks[i].lines.size(), 5u);
        if (i > 0) {
            EXPECT_LT(p.blocks[i - 1].bbox.x1, p.blocks[i].bbox.x0);
        }
    }
    const Page two = load("columns-2.pdf");
    ASSERT_EQ(two.blocks.size(), 4u);
    EXPECT_EQ(two.blocks[0].column, 0);
    EXPECT_EQ(two.blocks[1].column, 0);
    EXPECT_EQ(two.blocks[2].column, 1);
    EXPECT_EQ(two.blocks[3].column, 1);
}

TEST(text_blocks, alignment_on_four_fixtures) {
    const std::pair<const char*, Align> cases[] = {{"align-left.pdf", Align::Left},
                                                   {"align-right.pdf", Align::Right},
                                                   {"align-center.pdf", Align::Center},
                                                   {"align-justify.pdf", Align::Justify}};
    for (const auto& [name, align] : cases) {
        SCOPED_TRACE(name);
        const Page p = load(name);
        ASSERT_EQ(p.blocks.size(), 2u);
        for (const auto& b : p.blocks) {
            EXPECT_EQ(b.align, align);
            EXPECT_TRUE(b.editable());
            EXPECT_GT(b.leading, 0.0f);
        }
    }
    // По ширине: у Tw и у TJ-сдвигов растянуты все строки, кроме последней.
    const Page j = load("align-justify.pdf");
    for (const auto& b : j.blocks) {
        for (size_t i = 0; i + 1 < b.lines.size(); ++i) {
            EXPECT_GT(b.lines[i].stretch, 0.02f);
        }
        EXPECT_NEAR(b.lines.back().stretch, 0.0f, 0.01f);
    }
}

TEST(text_blocks, style_runs_and_indent) {
    const Page p = load("tz-tc-tw.pdf");
    const TextBlock* b = nullptr;
    for (const auto& blk : p.blocks) {
        if (blk.text == "E = mc2 and H2O in one line") {
            b = &blk;
        }
    }
    ASSERT_NE(b, nullptr);
    ASSERT_EQ(b->lines.size(), 1u);
    ASSERT_EQ(b->runs.size(), 5u);  // 12, 8 (Ts 5), 12, 8 (Ts −3), 12
    EXPECT_FLOAT_EQ(b->runs[1].size, 8.0f);
    EXPECT_FLOAT_EQ(b->runs[1].rise, 5.0f);
    EXPECT_FLOAT_EQ(b->runs[3].rise, -3.0f);
    EXPECT_EQ(b->runs[1].text, "2");

    const Page c = load("cyr-cid.pdf");
    ASSERT_EQ(c.blocks.size(), 2u);
    for (const auto& blk : c.blocks) {
        EXPECT_NEAR(blk.firstIndent, 24.0f, 0.5f);
        EXPECT_NEAR(blk.leading, 15.0f, 0.1f);
        ASSERT_EQ(blk.runs.size(), 1u);
        EXPECT_FLOAT_EQ(blk.runs[0].size, 12.0f);
    }
    const Page two = load("two-streams.pdf");
    ASSERT_EQ(two.blocks.size(), 1u);
    ASSERT_EQ(two.blocks[0].runs.size(), 1u);
    EXPECT_EQ(two.blocks[0].runs[0].fill, 0xFF000099u);
}

TEST(text_blocks, word_fixtures) {
    {
        // Абзацы Word: каждый — свой блок с выравниванием из задания и границами PDFium.
        const Page p = load("word-paragraphs.pdf");
        const json& exp = expected()["fixtures"]["word-paragraphs.pdf"]["blocks"];
        EXPECT_EQ(p.blocks.size(), exp.size());
        for (const json& e : exp) {
            const std::string want = squeeze(e["text"].get<std::string>());
            SCOPED_TRACE(want);
            const TextBlock* b = nullptr;
            for (const auto& blk : p.blocks) {
                if (squeeze(blk.text) == want) {
                    b = &blk;
                }
            }
            ASSERT_NE(b, nullptr);
            EXPECT_STREQ(ot::edit::alignCode(b->align), e["align"].get<std::string>().c_str());
            EXPECT_TRUE(b->editable());
            ASSERT_TRUE(e.contains("x0"));
            const double tol = e["tolerance"].get<double>();
            EXPECT_NEAR(b->bbox.x0, e["x0"].get<double>(), tol);
            EXPECT_NEAR(b->bbox.x1, e["x1"].get<double>(), tol);
        }
    }
    {
        // Таблица: ячейки одной строки — разные блоки (большой разрыв по X).
        const Page p = load("word-table.pdf");
        const json& exp = expected()["fixtures"]["word-table.pdf"]["blocks"];
        EXPECT_EQ(p.blocks.size(), exp.size());
        for (const json& e : exp) {
            const std::string want = squeeze(e["text"].get<std::string>());
            const bool found = std::any_of(p.blocks.begin(), p.blocks.end(),
                                           [&](const TextBlock& b) { return squeeze(b.text) == want; });
            EXPECT_TRUE(found) << want;
        }
    }
    {
        // Списки: каждый пункт — свой блок (маркер или номер в начале строки начинает абзац).
        const Page p = load("word-lists.pdf");
        const json& exp = expected()["fixtures"]["word-lists.pdf"]["blocks"];
        EXPECT_EQ(p.blocks.size(), exp.size());
        for (const json& e : exp) {
            const std::string want = squeeze(e["text"].get<std::string>());
            const bool found = std::any_of(p.blocks.begin(), p.blocks.end(), [&](const TextBlock& b) {
                const std::string got = squeeze(b.text);
                return got.size() >= want.size() &&
                       got.compare(got.size() - want.size(), want.size(), want) == 0;
            });
            EXPECT_TRUE(found) << want;
        }
    }
    {
        // Колонки Word: блоки — части абзацев задания; левые края — две колонки сверху и три снизу (64
        // общий).
        const Page p = load("word-columns.pdf");
        const json& fx = expected()["fixtures"]["word-columns.pdf"];
        std::set<int> columns;
        for (const auto& b : p.blocks) {
            columns.insert(static_cast<int>(std::lround(b.bbox.x0 / 4.0f)));
            const std::string got = squeeze(b.text);
            const bool part = std::any_of(
                fx["paragraphTexts"].begin(), fx["paragraphTexts"].end(),
                [&](const json& t) { return squeeze(t.get<std::string>()).find(got) != std::string::npos; });
            EXPECT_TRUE(part) << got;
        }
        EXPECT_GE(columns.size(), 4u);
    }
    {
        const Page p = load("word-rotated.pdf");
        const json& fx = expected()["fixtures"]["word-rotated.pdf"];
        int rotated = 0;
        for (const auto& b : p.blocks) {
            if (!b.horizontal) {
                ++rotated;
                EXPECT_EQ(b.reason, ot::edit::ReadOnly::Rotated);
                EXPECT_EQ(squeeze(b.text), fx["rotatedText"].get<std::string>());
            } else {
                EXPECT_TRUE(b.editable());
            }
        }
        EXPECT_EQ(rotated, 1);
    }
}

TEST(text_blocks, rotated_page_keeps_paragraphs) {
    // Та же раскладка на странице с /Rotate 90: абзацы в системе координат текста — горизонтальные и
    // правятся.
    auto doc = Document::openOrThrow(editFixture("latin.pdf"));
    const ContentModel plain = ContentModel::build(*doc, 0);
    const auto before = blocksForPage(plain);
    const std::string bytes = doc->savePdfToMemory();
    auto rotated = Document::openOrThrowMemory(bytes);
    {
        fz_context* ctx = rotated->context().get();
        ot::Journal::Operation op(*rotated, "Rotate");
        ot::mu::call(ctx, [&] {
            pdf_obj* page = pdf_lookup_page_obj(ctx, rotated->pdf(), 0);
            pdf_dict_put_int(ctx, page, PDF_NAME(Rotate), 90);
        });
        op.commit();
    }
    const ContentModel model = ContentModel::build(*rotated, 0);
    const auto after = blocksForPage(model);
    ASSERT_EQ(after.size(), before.size());
    for (size_t i = 0; i < after.size(); ++i) {
        EXPECT_EQ(after[i].text, before[i].text);
        EXPECT_TRUE(after[i].horizontal);
        EXPECT_TRUE(after[i].editable());
        EXPECT_EQ(after[i].lines.size(), before[i].lines.size());
    }
}

// Помощник: блоки файла OTTISK_DUMP_FILE (стр. OTTISK_DUMP_PAGE) — для разбора раскладки на чужих файлах.
TEST(text_blocks, DISABLED_dump) {
    const char* file = std::getenv("OTTISK_DUMP_FILE");
    if (file == nullptr) {
        GTEST_SKIP();
    }
    const char* pageEnv = std::getenv("OTTISK_DUMP_PAGE");
    auto doc = Document::openOrThrow(ot::paths::fromUtf8(file));
    const ContentModel model = ContentModel::build(*doc, pageEnv != nullptr ? std::atoi(pageEnv) : 0);
    for (const auto& b : blocksForPage(model)) {
        std::printf("[%s%s col %d] lines %zu bbox %.1f %.1f %.1f %.1f size %.1f lead %.1f: %s\n",
                    ot::edit::alignCode(b.align),
                    b.editable() ? "" : (std::string(" ro:") + ot::edit::readOnlyCode(b.reason)).c_str(),
                    b.column, b.lines.size(), b.bbox.x0, b.bbox.y0, b.bbox.x1, b.bbox.y1, b.fontSize,
                    b.leading, b.text.c_str());
        for (const auto& l : b.lines) {
            std::printf("    x %.1f..%.1f y %.1f size %.1f stretch %.3f spans %zu\n", l.x0, l.x1, l.y, l.size,
                        l.stretch, l.spans.size());
        }
    }
}
