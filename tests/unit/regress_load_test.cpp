// unit.regress.load — каждый файл tests/fixtures/regress/ (входы, на которых падали фаззер или корпус;
// tools\fuzz.ps1 кладёт падения в build\fuzz\artifacts) открывается без падения процесса: открыть →
// страницы → первая страница 32 × 32 → закрыть. Ошибка MuPDF (MuError) допустима, падение и чужое
// исключение — нет. Замена песочницы security-34 — устойчивость парсера (решение человека); сам ID — out
// (docs/scope.json), OT_FEATURE на него не ставится (рубеж Р1: scope_report.py предупреждал).
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/error.h"
#include "core/render/renderer.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;

namespace {

// Пусто — открылся и нарисовался или честная ошибка MuPDF; иначе — что пошло не так.
std::string loadOnce(const fs::path& file) {
    try {
        ot::OpenResult r = ot::Document::openMemory(ot::paths::readFile(file), ".pdf");
        if (!r.ok) {
            return r.error.kind == ot::ErrorKind::Other && r.error.code == 0
                       ? "не MuError: " + r.error.message
                       : std::string();
        }
        const int pages = r.document->pageCount();
        if (pages > 0) {
            const ot::RectF box = r.document->pageBox(0);
            const float side = std::max(box.width(), box.height());
            if (side > 0) {
                const ot::Renderer renderer(*r.document);
                ot::RenderRequest req;
                req.matrix = ot::Matrix::scale(32.0f / side);
                req.scissor = renderer.deviceBounds(0, req.matrix);
                if (!req.scissor.empty()) {
                    std::vector<std::uint8_t> buf(static_cast<size_t>(req.scissor.width()) *
                                                  req.scissor.height() * 4);
                    renderer.render(req, buf.data(), req.scissor.width() * 4);
                }
            }
        }
    } catch (const ot::MuError&) {
        return {};
    } catch (const std::exception& e) {
        return std::string("исключение не MuPDF: ") + e.what();
    }
    return {};
}

}  // namespace

TEST(regress, load) {
    const fs::path dir = ot::test::sourceDir() / L"tests" / L"fixtures" / L"regress";
    ASSERT_TRUE(fs::is_directory(dir)) << "нет tests/fixtures/regress";
    int files = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        const std::wstring name = entry.path().filename().native();
        if (!entry.is_regular_file() || name == L"README.md" || name == L".gitattributes") {
            continue;
        }
        ++files;
        EXPECT_EQ(loadOnce(entry.path()), "") << ot::paths::toUtf8(entry.path().filename());
    }
    RecordProperty("files", files);
}
