// unit.edit.fixtures — фикстуры редактора текста (план 3.1, п. 1; эталон 9.2):
// tests/fixtures/edit/expected.json читается, каждая фикстура открывается MuPDF с ожидаемым числом страниц и
// весит не больше 50 КБ, у спанов и блоков есть поля, по которым сверяют модель тесты content_model_test и
// text_blocks_test; на месте все фикстуры, которые эти тесты называют. Генераторы —
// tools/gen/make_edit_fixtures.ps1 (Word) и make_edit_fixtures.py.
#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

fs::path editDir() { return ot::test::sourceDir() / "tests" / "fixtures" / "edit"; }

// Фикстуры, которые называют тесты модели (план 3.1: 8 — разбор потока, 12 — блоки, 4 — выравнивание).
const char* const kModelFixtures[] = {
    "latin.pdf",   "cyr-cid.pdf",     "type1-diff.pdf",   "tj-kerning.pdf", "tz-tc-tw.pdf",
    "rotated.pdf", "two-streams.pdf", "xobject-form.pdf", "base14.pdf",     "inline-image.pdf"};
const char* const kBlockFixtures[] = {
    "latin.pdf",      "cyr-cid.pdf",     "columns-2.pdf",    "columns-3.pdf",
    "align-left.pdf", "align-right.pdf", "align-center.pdf", "align-justify.pdf",
    "tz-tc-tw.pdf",   "rotated.pdf",     "xobject-form.pdf", "word-paragraphs.pdf",
    "word-table.pdf", "word-lists.pdf",  "word-columns.pdf", "word-rotated.pdf"};

}  // namespace

TEST(edit, fixtures) {
    const fs::path dir = editDir();
    ASSERT_TRUE(fs::exists(dir / "expected.json")) << "нет tests/fixtures/edit — make_edit_fixtures.ps1";
    const json meta = json::parse(ot::paths::readFile(dir / "expected.json"));
    ASSERT_TRUE(meta.contains("fixtures"));
    const json& fixtures = meta["fixtures"];
    EXPECT_GE(fixtures.size(), 21u);  // 16 своих + 5 из Word (+ LibreOffice при наличии)

    for (const char* name : kModelFixtures) {
        EXPECT_TRUE(fixtures.contains(name)) << name;
    }
    for (const char* name : kBlockFixtures) {
        EXPECT_TRUE(fixtures.contains(name)) << name;
    }

    const std::set<std::string> aligns = {"left", "right", "center", "justify"};
    int spans = 0;
    int blocks = 0;
    for (const auto& [name, fx] : fixtures.items()) {
        SCOPED_TRACE(name);
        const fs::path path = dir / name;
        ASSERT_TRUE(fs::exists(path));
        EXPECT_LE(fs::file_size(path), 50u * 1024u) << "фикстура больше 50 КБ";
        ot::OpenResult opened = ot::Document::open(path);
        ASSERT_TRUE(opened.ok) << opened.error.message;
        EXPECT_EQ(opened.document->pageCount(), fx.at("pages").get<int>());
        EXPECT_FALSE(fx.value("about", "").empty());
        EXPECT_FALSE(fx.value("source", "").empty());

        const json noSpans = json::array();
        const json& spanList = fx.contains("spans") ? fx["spans"] : noSpans;
        for (const json& s : spanList) {
            EXPECT_FALSE(s.at("text").get<std::string>().empty());
            EXPECT_GT(s.at("size").get<double>(), 0.0);
            EXPECT_FALSE(s.at("font").get<std::string>().empty());
            EXPECT_TRUE(s.at("readOnly").is_string());
            EXPECT_EQ(s.at("color").get<std::string>().size(), 7u);  // #rrggbb
            const bool horizontal = s.contains("x0") && s.contains("x1") && s.contains("baseline");
            const bool turned = s.contains("origin") && s.contains("angle");
            EXPECT_TRUE(horizontal || turned) << s.dump();
            if (horizontal) {
                EXPECT_LT(s["x0"].get<double>(), s["x1"].get<double>()) << s.dump();
            }
            ++spans;
        }
        for (const json& b : fx.at("blocks")) {
            EXPECT_FALSE(b.at("text").get<std::string>().empty());
            EXPECT_TRUE(aligns.count(b.at("align").get<std::string>())) << b.dump();
            EXPECT_TRUE(b.at("readOnly").is_string());
            if (b.contains("spans")) {
                for (const json& i : b["spans"]) {
                    EXPECT_LT(i.get<size_t>(), spanList.size()) << b.dump();
                }
                EXPECT_GE(b.at("lines").get<int>(), 1);
            }
            if (b.contains("x0")) {
                EXPECT_LT(b["x0"].get<double>(), b["x1"].get<double>()) << b.dump();
            }
            ++blocks;
        }
    }
    EXPECT_GT(spans, 80);
    EXPECT_GT(blocks, 40);

    // Выравнивание: четыре фикстуры align-* — каждая своим видом во всех блоках.
    for (const char* align : {"left", "right", "center", "justify"}) {
        const json& fx = fixtures.at(std::string("align-") + align + ".pdf");
        ASSERT_EQ(fx.at("blocks").size(), 2u);
        for (const json& b : fx["blocks"]) {
            EXPECT_EQ(b.at("align").get<std::string>(), align);
        }
    }
    // «Три колонки» — ровно 3 блока без пересечения по X.
    const json& cols = fixtures.at("columns-3.pdf").at("blocks");
    ASSERT_EQ(cols.size(), 3u);
    for (size_t i = 1; i < cols.size(); ++i) {
        EXPECT_LT(cols[i - 1]["x1"].get<double>(), cols[i]["x0"].get<double>());
    }
}
