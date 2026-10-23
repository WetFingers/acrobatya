// pdf_open_fuzzer — libFuzzer-цель (эталон 9.5), пресет msvc-fuzz (/fsanitize=address и /fsanitize=fuzzer):
// память → Document::openMemory (клон MuContext::global()) → до 4 страниц: рамка, рендер 32 × 32 через
// Renderer (display list), stext → закрыть. Идёт через наш core (mu::call, Document, Renderer), а не прямо
// в MuPDF: покрытие libFuzzer видит и наш код (ottisk_core в этом пресете собран с /fsanitize-coverage).
//
// Запуск — tools\fuzz.ps1 (60 с в сессии, 10 мин — рубеж):
//   pdf_open_fuzzer.exe <рабочий корпус> <семена…> -max_total_time=600 -rss_limit_mb=4096 -timeout=20
// Упавшие входы → tests/fixtures/regress/ + тест unit.regress.load.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/renderer.h"

namespace {

constexpr int kMaxPages = 4;

ot::MuContext& base() {
    static ot::MuContext& ctx = []() -> ot::MuContext& {
        spdlog::set_level(spdlog::level::off);  // тысячи предупреждений битых файлов — не в вывод
        return ot::MuContext::global();
    }();
    return ctx;
}

void exercisePage(const ot::Document& doc, int index) {
    const ot::RectF box = doc.pageBox(index);
    const float side = std::max(box.width(), box.height());
    if (side > 0) {
        const ot::Renderer renderer(doc);
        ot::RenderRequest req;
        req.page = index;
        req.matrix = ot::Matrix::scale(32.0f / side);
        req.scissor = renderer.deviceBounds(index, req.matrix);
        if (!req.scissor.empty() && req.scissor.width() <= 64 && req.scissor.height() <= 64) {
            std::vector<std::uint8_t> buf(static_cast<size_t>(req.scissor.width()) * req.scissor.height() *
                                          4);
            renderer.render(req, buf.data(), req.scissor.width() * 4);
        }
    }
    fz_context* ctx = doc.context().get();
    ot::mu::PageHandle page(ctx);
    ot::mu::StextPageHandle text(ctx);
    ot::mu::call(ctx, [&] {
        page.reset(fz_load_page(ctx, doc.handle(), index));
        text.reset(fz_new_stext_page_from_page(ctx, page, nullptr));
    });
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    ot::OpenResult r =
        ot::Document::openMemory(base(), std::string(reinterpret_cast<const char*>(data), size), ".pdf");
    if (!r.ok) {
        return 0;
    }
    try {
        const int pages = std::min(r.document->pageCount(), kMaxPages);
        for (int i = 0; i < pages; ++i) {
            try {
                exercisePage(*r.document, i);
            } catch (const ot::MuError&) {  // NOLINT(bugprone-empty-catch): битая страница — ожидаемо
            }
        }
        (void)r.document->properties();
    } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): ожидаемо для битых файлов
    }
    return 0;  // документ закрывается деструктором — часть проверки
}
