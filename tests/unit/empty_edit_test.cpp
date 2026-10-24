// unit.empty_edit.* — «пустая правка» (план 3.1, п. 5; эталон 3.9 «Стабильность»): каждый редактируемый абзац
// фикстур редактора пересобран без изменений (явная Tm и TJ по позициям модели), потоки записаны операцией
// журнала со сжатием FlateDecode — fz_stext до и после равен по тексту и рамкам (±0,5 pt), рендер в порогах
// imgcmp, undo возвращает потоки байт в байт; новые операторы — Tm и TJ, старые байты вне блоков не тронуты.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/edit/content_model.h"
#include "core/edit/empty_edit.h"
#include "core/edit/text_blocks.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::edit::ContentModel;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

}  // namespace

TEST(empty_edit, every_fixture_page_is_stable) {
    const json meta = json::parse(ot::paths::readFile(editFixture("expected.json")));
    int rebuilt = 0;
    for (const auto& [name, fx] : meta["fixtures"].items()) {
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(editFixture(name));
        for (int page = 0; page < doc->pageCount(); ++page) {
            ot::edit::EmptyEditOptions options;
            options.dpi = 48;  // быстрый набор: растр меньше, пороги те же (корпус — 72 dpi)
            const ot::edit::EmptyEditReport r = ot::edit::emptyEditPage(*doc, page, options);
            EXPECT_TRUE(r.stable()) << "стр. " << page + 1 << ": " << r.problem;
            EXPECT_EQ(r.rebuilt, r.editable);
            EXPECT_LE(r.maxBoxDelta, 0.5f);
            rebuilt += r.rebuilt;
        }
    }
    EXPECT_GT(rebuilt, 60);
}

TEST(empty_edit, rebuilt_operators_are_tm_and_tj) {
    auto doc = Document::openOrThrow(editFixture("cyr-cid.pdf"));
    ContentModel model = ContentModel::build(*doc, 0);
    const auto blocks = ot::edit::blocksForPage(model);
    ASSERT_EQ(blocks.size(), 2u);
    const std::string original = model.serialize(0);
    const ot::edit::RebuildResult r = model.rebuildParagraph(blocks[1]);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.ops, static_cast<int>(blocks[1].spans.size()));
    EXPECT_TRUE(model.modified(0));
    const std::string rebuilt = model.serialize(0);
    EXPECT_NE(rebuilt, original);
    for (const auto& [op, bytes] : model.replacements()) {
        EXPECT_NE(bytes.find(" Tm"), std::string::npos) << bytes;
        EXPECT_NE(bytes.find("] TJ"), std::string::npos) << bytes;
    }
    // Операции первого абзаца — байт в байт: префикс потока до первой заменённой операции совпадает.
    const auto& first = model.operations()[static_cast<size_t>(model.replacements().begin()->first)];
    EXPECT_EQ(rebuilt.substr(0, first.offset), original.substr(0, first.offset));

    // ' и " заменяются своим T* (и Tw/Tc у "): в фикстуре по ширине есть оба.
    auto justify = Document::openOrThrow(editFixture("align-justify.pdf"));
    ContentModel jm = ContentModel::build(*justify, 0);
    bool dquote = false;
    for (const auto& b : ot::edit::blocksForPage(jm)) {
        ASSERT_TRUE(jm.rebuildParagraph(b).ok);
    }
    for (const auto& [op, bytes] : jm.replacements()) {
        if (jm.operations()[static_cast<size_t>(op)].kind == ot::edit::OpKind::NextLineShowSpaced) {
            dquote = true;
            EXPECT_NE(bytes.find(" Tw "), std::string::npos);
            EXPECT_NE(bytes.find("T* "), std::string::npos);
        }
    }
    EXPECT_TRUE(dquote);
}

TEST(empty_edit, read_only_blocks_are_not_rebuilt) {
    auto doc = Document::openOrThrow(editFixture("xobject-form.pdf"));
    ContentModel model = ContentModel::build(*doc, 0);
    int readOnly = 0;
    for (const auto& b : ot::edit::blocksForPage(model)) {
        if (!b.editable()) {
            ++readOnly;
            EXPECT_FALSE(model.rebuildParagraph(b).ok);
        }
    }
    EXPECT_EQ(readOnly, 1);
    EXPECT_TRUE(model.replacements().empty());
    EXPECT_EQ(model.apply(*doc), 0);
    const ot::edit::EmptyEditReport r = ot::edit::emptyEditPage(*doc, 0);
    EXPECT_EQ(r.readOnly.at("form"), 1);
    EXPECT_TRUE(r.stable()) << r.problem;
}

// Помощник: пустая правка страницы OTTISK_DUMP_FILE (стр. OTTISK_DUMP_PAGE) — разбор нестабильных файлов
// корпуса.
TEST(empty_edit, DISABLED_file) {
    const char* file = std::getenv("OTTISK_DUMP_FILE");
    if (file == nullptr) {
        GTEST_SKIP();
    }
    const char* pageEnv = std::getenv("OTTISK_DUMP_PAGE");
    auto doc = Document::openOrThrow(ot::paths::fromUtf8(file));
    const ot::edit::EmptyEditReport r =
        ot::edit::emptyEditPage(*doc, pageEnv != nullptr ? std::atoi(pageEnv) : 0);
    std::printf("blocks %d editable %d rebuilt %d ops %d streams %d stable %d: %s\n", r.blocks, r.editable,
                r.rebuilt, r.ops, r.streams, r.stable() ? 1 : 0, r.problem.c_str());
}

// Помощник: оператор у точки OTTISK_DUMP_X, OTTISK_DUMP_Y — исходные байты, состояние спана и замена.
TEST(empty_edit, DISABLED_op_at) {
    const char* file = std::getenv("OTTISK_DUMP_FILE");
    if (file == nullptr || std::getenv("OTTISK_DUMP_X") == nullptr) {
        GTEST_SKIP();
    }
    const float x = static_cast<float>(std::atof(std::getenv("OTTISK_DUMP_X")));
    const float y = static_cast<float>(std::atof(std::getenv("OTTISK_DUMP_Y")));
    auto doc = Document::openOrThrow(ot::paths::fromUtf8(file));
    ContentModel model = ContentModel::build(*doc, 0);
    int best = -1;
    float dist = 1e9f;
    for (size_t g = 0; g < model.glyphs().size(); ++g) {
        const auto& gl = model.glyphs()[g];
        const float d = std::fabs(gl.quad.ll.x - x) + std::fabs(gl.quad.ll.y - y);
        if (d < dist) {
            dist = d;
            best = static_cast<int>(g);
        }
    }
    ASSERT_GE(best, 0);
    const auto& span = model.spans()[static_cast<size_t>(model.glyphs()[static_cast<size_t>(best)].span)];
    std::printf("op %d raw: [%s]\n", span.op, std::string(model.raw(span.op)).c_str());
    std::printf("tm %g %g %g %g %g %g tlm %g %g %g %g %g %g size %g scale %g Tc %g Tw %g advance %g\n",
                span.tm.a, span.tm.b, span.tm.c, span.tm.d, span.tm.e, span.tm.f, span.tlm.a, span.tlm.b,
                span.tlm.c, span.tlm.d, span.tlm.e, span.tlm.f, span.size, span.scale, span.charSpace,
                span.wordSpace, span.advance);
    for (const auto& b : ot::edit::blocksForPage(model)) {
        if (std::find(b.spans.begin(), b.spans.end(), model.glyphs()[static_cast<size_t>(best)].span) !=
            b.spans.end()) {
            model.rebuildParagraph(b);
        }
    }
    if (const auto it = model.replacements().find(span.op); it != model.replacements().end()) {
        std::printf("new: [%s]\n", it->second.c_str());
    }
    if (span.op + 1 < static_cast<int>(model.operations().size())) {
        std::printf("next raw: [%s]\n", std::string(model.raw(span.op + 1)).c_str());
    }
}
