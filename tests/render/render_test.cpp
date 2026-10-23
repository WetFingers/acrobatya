// render.refs.* — сверка рендера с эталонами tests/render/expected (эталон 9.3). Каждая запись
// expected.json → отдельный тест (параметризация); порог — fuzzy записи или defaults. Источник
// "corpus:<путь>" ищется в C:\Tools\ottisk\corpus (или OTTISK_CORPUS); нет файла — SKIP.
// release_only=true и Debug-сборка — SKIP (расхождение Debug ↔ Release помечает только render_refs).
// Release (те же библиотеки MuPDF, что у эталонов) — совпадение бит в бит (pixels == 0); Debug — в порогах.
// Рендер — Document::renderPage → Renderer (display list, scissor), AA 8, ICC выкл.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/util/features.h"
#include "core/util/paths.h"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

struct RenderCase {
    std::string id;
    std::string source;
    int page = 1;
    int dpi = 72;
    std::string png;
    ot::FuzzyThreshold fuzzy;
    bool releaseOnly = false;
};

fs::path root() { return ot::paths::fromUtf8(OTTISK_SOURCE_DIR); }

fs::path corpusRoot() {
    if (const char* c = std::getenv("OTTISK_CORPUS"); c != nullptr && *c != '\0') {
        return ot::paths::fromUtf8(c);
    }
    // Не %LOCALAPPDATA%: приложение Claude перенаправляет туда записи своих процессов (docs/progress.md).
    return std::filesystem::path(L"C:\\Tools\\ottisk\\corpus");
}

fs::path resolveSource(const std::string& source) {
    const std::string prefix = "corpus:";
    if (source.rfind(prefix, 0) == 0) {
        return corpusRoot() / ot::paths::fromUtf8(source.substr(prefix.size()));
    }
    return root() / ot::paths::fromUtf8(source);
}

ot::FuzzyThreshold readFuzzy(const json& j, ot::FuzzyThreshold base) {
    if (!j.is_object()) {
        return base;
    }
    base.maxDelta = j.value("max_delta", base.maxDelta);
    base.mse = j.value("mse", base.mse);
    base.windowedMse = j.value("wmse", base.windowedMse);
    base.ssim = j.value("ssim", base.ssim);
    return base;
}

std::vector<RenderCase> loadCases() {
    std::vector<RenderCase> cases;
    const fs::path file = root() / L"tests" / L"render" / L"expected" / L"expected.json";
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return cases;
    }
    const json doc = json::parse(ot::paths::readFile(file));
    const ot::FuzzyThreshold defaults =
        readFuzzy(doc.value("defaults", json::object()).value("fuzzy", json()), {});
    for (const json& item : doc.value("items", json::array())) {
        for (const json& r : item.value("renders", json::array())) {
            RenderCase c;
            c.id = item.value("id", std::string());
            c.source = item.value("source", std::string());
            c.page = r.value("page", 1);
            c.dpi = r.value("dpi", 72);
            c.png = r.value("png", std::string());
            c.fuzzy = readFuzzy(item.value("fuzzy", json()), defaults);
            c.releaseOnly = item.value("release_only", false);
            cases.push_back(c);
        }
    }
    return cases;
}

std::string caseName(const testing::TestParamInfo<RenderCase>& info) {
    std::string n =
        info.param.id + "_p" + std::to_string(info.param.page) + "_" + std::to_string(info.param.dpi);
    for (char& ch : n) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9'))) {
            ch = '_';
        }
    }
    return n + "_" + std::to_string(info.index);
}

// Без PrintTo gtest дописывает к имени ctest дамп байтов параметра с адресами кучи — имя менялось бы
// от запуска к запуску (нельзя фильтровать -R и сравнивать ctest.xml).
void PrintTo(const RenderCase& c, std::ostream* os) { *os << c.id << " p" << c.page << " @" << c.dpi; }

class RenderRefs : public testing::TestWithParam<RenderCase> {};

}  // namespace

TEST_P(RenderRefs, matches_expected) {
    const RenderCase& c = GetParam();
#ifndef NDEBUG
    if (c.releaseOnly) {
        GTEST_SKIP() << "release_only: " << c.id;
    }
#endif
    const fs::path src = resolveSource(c.source);
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        GTEST_SKIP() << "нет источника " << ot::paths::toUtf8(src);
    }
    ot::MuContext mu;
    fz_set_aa_level(mu.get(), 8);
    fz_disable_icc(mu.get());
    auto doc = ot::Document::openOrThrow(mu, src);
    const ot::Bitmap actual = doc->renderPage(c.page - 1, {static_cast<float>(c.dpi), true});
    const ot::Bitmap expected = ot::loadPng(mu, root() / ot::paths::fromUtf8(c.png));
    ot::Bitmap diff;
    const ot::CompareResult r = ot::compareBitmaps(expected, actual, &diff);
    if (!r.within(c.fuzzy)) {
        const fs::path out =
            ot::paths::fromUtf8(OTTISK_BINARY_DIR) / L"render-actual" / ot::paths::fromUtf8(c.png).filename();
        ot::savePng(mu, actual, out);
        ot::savePng(mu, diff, fs::path(out).replace_extension(L".diff.png"));
    }
#ifdef NDEBUG
    EXPECT_EQ(r.differingPixels, 0) << c.id << " p" << c.page << " @" << c.dpi << ": " << r.toJson();
#endif
    EXPECT_TRUE(r.within(c.fuzzy)) << c.id << " p" << c.page << " @" << c.dpi << ": " << r.toJson();
}

INSTANTIATE_TEST_SUITE_P(refs, RenderRefs, testing::ValuesIn(loadCases()), caseName);
GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(RenderRefs);

// Страница 1 hello_world.pdf (pdfium) при 72 dpi: Release — точно, Debug — в порогах (эталон 9.3).
TEST(refs, hello_world_page1_72dpi) {
    OT_FEATURE("view-01");
    ot::MuContext mu;
    fz_set_aa_level(mu.get(), 8);
    fz_disable_icc(mu.get());
    auto doc = ot::Document::openOrThrow(
        mu, root() / L"tests/fixtures/pick/pdfium_resources/testing/resources/hello_world.pdf");
    const ot::Bitmap actual = doc->renderPage(0, {72.0f, true});
    const ot::Bitmap expected =
        ot::loadPng(mu, root() / L"tests/render/expected/pdfium_resources/hello_world.1.72.png");
    const ot::CompareResult r = ot::compareBitmaps(expected, actual);
    ASSERT_TRUE(r.sameSize) << r.toJson();
#ifdef NDEBUG
    EXPECT_EQ(r.differingPixels, 0) << r.toJson();
#else
    EXPECT_TRUE(r.within({})) << r.toJson();
#endif
}

TEST(refs, expected_json_is_present) {
    const auto cases = loadCases();
    if (cases.empty()) {
        GTEST_SKIP() << "tests/render/expected/expected.json ещё нет (этап 0, пункт «Эталоны рендера»)";
    }
    SUCCEED() << cases.size() << " renders";
}
