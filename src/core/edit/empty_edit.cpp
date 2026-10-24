#include "core/edit/empty_edit.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/text_blocks.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"
#include "core/text/stext.h"

namespace ot::edit {

namespace {

float cornerDelta(const QuadF& a, const QuadF& b) {
    auto d = [](PointF p, PointF q) { return std::fmax(std::fabs(p.x - q.x), std::fabs(p.y - q.y)); };
    return std::fmax(std::fmax(d(a.ul, b.ul), d(a.ur, b.ur)), std::fmax(d(a.ll, b.ll), d(a.lr, b.lr)));
}

// Декодированный поток объекта objnum (после undo).
std::string loadStream(Document& doc, int objnum, int gen) {
    fz_context* ctx = doc.context().get();
    mu::PdfObjHandle ref(ctx);
    mu::BufferHandle buf(ctx);
    mu::call(ctx, [&] {
        ref.reset(pdf_new_indirect(ctx, doc.pdf(), objnum, gen));
        buf.reset(pdf_load_stream(ctx, ref));
    });
    unsigned char* data = nullptr;
    const std::size_t len = fz_buffer_storage(ctx, buf, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

}  // namespace

EmptyEditReport emptyEditPage(Document& doc, int page, const EmptyEditOptions& options) {
    EmptyEditReport r;
    r.page = page;
    TextOptions textOptions;
    textOptions.readingOrder = false;  // порядок потока: символы до и после сравниваются по индексу

    ContentModel model = ContentModel::build(doc, page);
    const std::vector<TextBlock> blocks = blocksForPage(model);
    r.blocks = static_cast<int>(blocks.size());
    for (const TextBlock& b : blocks) {
        if (b.editable()) {
            ++r.editable;
        } else {
            ++r.readOnly[readOnlyCode(b.reason)];
        }
    }
    if (r.editable == 0) {
        return r;
    }
    const Bitmap before = doc.renderPage(page, RenderOptions{options.dpi, false});
    const std::shared_ptr<const TextPage> textBefore = TextPage::build(doc, page, textOptions);
    for (const TextBlock& b : blocks) {
        const RebuildResult res = model.rebuildParagraph(b);
        if (res.ok) {
            ++r.rebuilt;
            r.ops += res.ops;
        }
    }
    r.streams = model.apply(doc, "Empty edit");
    if (r.streams == 0) {
        return r;  // всё в формах или неизвестных потоках — писать нечего
    }

    const Bitmap after = doc.renderPage(page, RenderOptions{options.dpi, false});
    r.render = compareBitmaps(before, after);
    r.renderSame = r.render.within(FuzzyThreshold{});
    if (!r.renderSame && r.problem.empty()) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "render: %lld px differ, max delta %d, MSE %.3f",
                      r.render.differingPixels, r.render.maxDelta, r.render.mse);
        r.problem = buf;
    }

    const std::shared_ptr<const TextPage> textAfter = TextPage::build(doc, page, textOptions);
    const auto& a = textBefore->chars();
    const auto& z = textAfter->chars();
    // Символы по индексу; при разном числе — первое расхождение (добавленный пробел fz_stext и т. п.).
    const std::size_t n = std::min(a.size(), z.size());
    std::size_t firstDiff = n;
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i].c != z[i].c) {
            firstDiff = std::min(firstDiff, i);
            r.textSame = false;
            continue;
        }
        const float delta = cornerDelta(a[i].quad, z[i].quad);
        r.maxBoxDelta = std::fmax(r.maxBoxDelta, delta);
        if (delta > options.boxTolerance) {
            if (r.boxesSame && r.problem.empty()) {
                char buf[128];
                std::snprintf(buf, sizeof buf, "stext: char %zu U+%04X moved by %.2f pt (%.2f, %.2f)", i,
                              static_cast<unsigned>(a[i].c), delta, a[i].quad.ll.x, a[i].quad.ll.y);
                r.problem = buf;
            }
            r.boxesSame = false;
        }
    }
    if (a.size() != z.size()) {
        r.textSame = false;
    }
    if (!r.textSame && r.problem.empty()) {
        char buf[192];
        if (firstDiff < n) {
            std::snprintf(
                buf, sizeof buf, "stext: %zu chars before, %zu after; char %zu U+%04X (%.2f, %.2f) vs U+%04X",
                a.size(), z.size(), firstDiff, static_cast<unsigned>(a[firstDiff].c), a[firstDiff].quad.ll.x,
                a[firstDiff].quad.ll.y, static_cast<unsigned>(z[firstDiff].c));
        } else {
            std::snprintf(buf, sizeof buf, "stext: %zu chars before, %zu after", a.size(), z.size());
        }
        r.problem = buf;
    }

    if (options.checkUndo && doc.journaled()) {
        doc.undoStack().undo();
        for (const ContentStream& s : model.streams()) {
            if (s.form || s.objnum <= 0) {
                continue;
            }
            if (loadStream(doc, s.objnum, s.gen) != s.bytes) {
                r.undoSame = false;
                if (r.problem.empty()) {
                    r.problem = "undo: stream " + std::to_string(s.objnum) + " differs";
                }
            }
        }
    }
    return r;
}

}  // namespace ot::edit
