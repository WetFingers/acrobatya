// engine.annots_matrix.* — матрица 28 подтипов и корпус «annots» (план 5.1; эталон 3.4 «Ядро», 9.2):
// открыть, перечислить (listAnnots, readProps, JSON), отрендерить, дописать записку и сохранить инкрементом,
// переоткрыть — без падений; на своей матрице — ещё qpdf --check = 0 и все 28 подтипов на месте после записи.
// Корпуса нет — пропуск (сессии корпус не качают).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <set>
#include <string>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/mu/error.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/probe.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using json = nlohmann::json;
using ot::Document;
using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;

namespace {

fs::path matrixFile() { return ot::test::sourceDir() / "tests" / "fixtures" / "annots" / "annots-all.pdf"; }

// Все аннотации страницы: список, свойства, JSON туда и обратно. Возвращает число записей.
std::size_t walkPage(Document& doc, int page) {
    const std::vector<AnnotInfo> list = ot::annot::listAnnots(doc, page);
    for (const AnnotInfo& a : list) {
        if (!a.ref.valid()) {
            continue;
        }
        const auto props = ot::annot::readProps(doc, a.ref);
        EXPECT_TRUE(props.has_value()) << a.subtype;
        if (props) {
            const AnnotProps back = ot::annot::propsFromJson(ot::annot::toJson(*props));
            EXPECT_EQ(back.type, props->type);
        }
    }
    return list.size();
}

AnnotProps note(float x, float y) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
    p.rect = {x, y, x + 20, y + 20};
    p.contents = "engine note";
    p.author = "engine";
    return p;
}

}  // namespace

TEST(annots_matrix, fixture_28_subtypes_open_render_save) {
    OT_FEATURE("comment-08");
    ot::test::TempDir dir;
    const fs::path path = dir.path() / "matrix.pdf";
    fs::copy_file(matrixFile(), path);
    {
        auto doc = Document::openOrThrow(path);
        const std::vector<AnnotInfo> list = ot::annot::listAnnots(*doc, 0);
        ASSERT_EQ(list.size(), 28u);
        std::set<std::string> subtypes;
        for (const AnnotInfo& a : list) {
            subtypes.insert(a.subtype);
        }
        EXPECT_EQ(subtypes.size(), 28u);
        EXPECT_EQ(walkPage(*doc, 0), 28u);
        const ot::Bitmap bitmap = doc->renderPage(0, {.dpi = 72});
        EXPECT_FALSE(bitmap.empty());
        ot::annot::create(*doc, 0, note(20, 20));
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
        EXPECT_EQ(report.mode, ot::SaveMode::Incremental);
    }
    auto reopened = Document::openOrThrow(path);
    const std::vector<AnnotInfo> after = ot::annot::listAnnots(*reopened, 0);
    std::set<std::string> subtypes;
    for (const AnnotInfo& a : after) {
        subtypes.insert(a.subtype);
    }
    EXPECT_EQ(subtypes.size(), 28u);
    EXPECT_GE(after.size(), 29u);  // + записка и её окно
    EXPECT_FALSE(reopened->renderPage(0, {.dpi = 72}).empty());
    const ot::QpdfTool qpdf;
    if (qpdf.available()) {
        const ot::ToolResult check = qpdf.check(path);
        EXPECT_EQ(check.exitCode, 0) << check.output;
    }
}

// Корпус «annots» (pdfium_tests, pdf.js и др.): каждый файл — список и свойства на первых страницах, рендер
// первой страницы в 36 dpi; каждый пятый — записка и инкрементальная запись с переоткрытием. Битые файлы —
// только ошибки битого файла.
TEST(annots_matrix, corpus_annots_tag) {
    const fs::path indexPath = ot::test::corpusRoot() / L"index.json";
    std::error_code ec;
    if (!fs::exists(indexPath, ec)) {
        GTEST_SKIP() << "корпуса нет: " << ot::paths::toUtf8(indexPath);
    }
    const json index = json::parse(ot::paths::readFile(indexPath));
    const json records = index["tags"].value("annots", json::array());
    if (records.empty()) {
        GTEST_SKIP() << "в индексе корпуса нет тега annots";
    }
    const ot::test::QuietLog quiet;
    ot::test::TempDir dir;
    int opened = 0;
    int saved = 0;
    std::size_t annots = 0;
    std::vector<std::string> failures;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const json& rec = records[i];
        const std::string expect = rec.value("expect", std::string("open"));
        if (expect != "open") {
            continue;  // пароли, данные и битые — у engine.corpus
        }
        const std::string rel = rec.value("rel", std::string());
        const fs::path file = ot::test::corpusFile(rec.value("path", std::string()), rel);
        try {
            ot::OpenResult result = Document::open(file);
            if (!result.ok || result.document == nullptr || result.document->pdf() == nullptr) {
                continue;
            }
            Document& doc = *result.document;
            ++opened;
            const int pages = std::min(doc.pageCount(), 3);
            for (int p = 0; p < pages; ++p) {
                annots += walkPage(doc, p);
            }
            (void)doc.renderPage(0, {.dpi = 36});
            if (i % 5 == 0 && doc.pageCount() > 0) {
                const fs::path copy = dir.path() / ("c" + std::to_string(i) + ".pdf");
                fs::copy_file(file, copy, fs::copy_options::overwrite_existing);
                ot::OpenResult own = Document::open(copy);
                if (own.ok && own.document != nullptr && own.document->pdf() != nullptr) {
                    ot::annot::create(*own.document, 0, note(10, 10));
                    const ot::SaveReport report = ot::Saver::save(*own.document, copy, {});
                    if (report.ok()) {
                        ++saved;
                        auto back = Document::openOrThrow(copy);
                        walkPage(*back, 0);
                    }
                }
            }
        } catch (const ot::MuError& e) {
            if (!ot::test::isBrokenFileCode(e.code())) {
                failures.push_back(rel + ": " + e.what());
            }
        } catch (const std::exception& e) {
            failures.push_back(rel + ": " + e.what());
        }
    }
    std::printf("progress: annots opened %d, annotations %zu, saved %d\n", opened, annots, saved);
    EXPECT_GT(opened, 0);
    EXPECT_GT(saved, 0);
    for (const std::string& f : failures) {
        ADD_FAILURE() << f;
    }
}
