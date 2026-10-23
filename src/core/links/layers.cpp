#include "core/links/layers.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::layers {

namespace {

UiKind layerKind(int type) {
    switch (type) {
        case PDF_LAYER_UI_CHECKBOX:
            return UiKind::Checkbox;
        case PDF_LAYER_UI_RADIOBOX:
            return UiKind::Radio;
        default:
            return UiKind::Label;
    }
}

int layerUiCount(fz_context* ctx, pdf_document* pdf) {
    int n = 0;
    mu::call(ctx, [&] { n = pdf_count_layer_config_ui(ctx, pdf); });
    return n;
}

}  // namespace

bool hasLayers(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return false;
    }
    fz_context* ctx = doc.context().get();
    int n = 0;
    mu::call(ctx, [&] { n = pdf_count_layers(ctx, pdf); });
    return n > 0;
}

std::vector<LayerUi> list(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    const int n = layerUiCount(ctx, pdf);
    std::vector<LayerUi> out;
    out.reserve(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        pdf_layer_config_ui info{};
        mu::call(ctx, [&] { pdf_layer_config_ui_info(ctx, pdf, i, &info); });
        LayerUi row;
        row.index = i;
        row.text = info.text != nullptr ? std::string(info.text) : std::string();
        row.depth = info.depth;
        row.kind = layerKind(info.type);
        row.on = row.kind != UiKind::Label && info.selected != 0;
        row.locked = info.locked != 0;
        out.push_back(std::move(row));
    }
    return out;
}

bool setOn(Document& doc, int index, bool on) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return false;
    }
    fz_context* ctx = doc.context().get();
    if (index < 0 || index >= layerUiCount(ctx, pdf)) {
        return false;
    }
    pdf_layer_config_ui info{};
    mu::call(ctx, [&] { pdf_layer_config_ui_info(ctx, pdf, index, &info); });
    if (info.locked != 0 || layerKind(info.type) == UiKind::Label) {
        return false;
    }
    mu::call(ctx, [&] {
        if (on) {
            pdf_select_layer_config_ui(ctx, pdf, index);
        } else {
            pdf_deselect_layer_config_ui(ctx, pdf, index);
        }
    });
    return true;
}

bool toggle(Document& doc, int index) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return false;
    }
    fz_context* ctx = doc.context().get();
    if (index < 0 || index >= layerUiCount(ctx, pdf)) {
        return false;
    }
    pdf_layer_config_ui info{};
    mu::call(ctx, [&] { pdf_layer_config_ui_info(ctx, pdf, index, &info); });
    return setOn(doc, index, info.selected == 0);
}

void reset(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return;
    }
    fz_context* ctx = doc.context().get();
    mu::call(ctx, [&] {
        if (pdf_count_layers(ctx, pdf) > 0) {
            pdf_select_layer_config(ctx, pdf, -1);  // −1 — конфигурация /D
        }
    });
}

std::uint64_t stateHash(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return 0;
    }
    fz_context* ctx = doc.context().get();
    int n = 0;
    mu::call(ctx, [&] { n = pdf_count_layers(ctx, pdf); });
    if (n <= 0) {
        return 0;
    }
    constexpr std::uint64_t kOffset = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t h = kOffset;
    for (int i = 0; i < n; ++i) {
        int on = 0;
        mu::call(ctx, [&] { on = pdf_layer_is_enabled(ctx, pdf, i); });
        h = (h ^ static_cast<std::uint64_t>(on != 0 ? 0x9E37U + static_cast<unsigned>(i) : 0x51U)) * kPrime;
    }
    return h == 0 ? 1 : h;
}

}  // namespace ot::layers
