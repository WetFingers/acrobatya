#include "core/doc/presentation.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::presentation {

namespace {

// Поля перехода, прочитанные внутри mu::call (как pdf_page_presentation MuPDF).
struct RawTransition {
    int type = 0;  // TransitionType
    float duration = 1.0f;
    int vertical = 1;
    int outwards = 1;
    int direction = 0;
    float pageDuration = 0;
};

void presentationRead(fz_context* ctx, pdf_obj* page, RawTransition* out) {
    out->pageDuration = pdf_dict_get_real(ctx, page, PDF_NAME(Dur));
    pdf_obj* trans = pdf_dict_get(ctx, page, PDF_NAME(Trans));
    if (!pdf_is_dict(ctx, trans)) {
        return;
    }
    out->duration = pdf_to_real_default(ctx, pdf_dict_get(ctx, trans, PDF_NAME(D)), 1);
    out->vertical = !pdf_name_eq(ctx, pdf_dict_get(ctx, trans, PDF_NAME(Dm)), PDF_NAME(H));
    out->outwards = !pdf_name_eq(ctx, pdf_dict_get(ctx, trans, PDF_NAME(M)), PDF_NAME(I));
    out->direction = pdf_dict_get_int(ctx, trans, PDF_NAME(Di));
    pdf_obj* s = pdf_dict_get(ctx, trans, PDF_NAME(S));
    const std::pair<pdf_obj*, TransitionType> names[] = {
        {PDF_NAME(Split), TransitionType::Split},       {PDF_NAME(Blinds), TransitionType::Blinds},
        {PDF_NAME(Box), TransitionType::Box},           {PDF_NAME(Wipe), TransitionType::Wipe},
        {PDF_NAME(Dissolve), TransitionType::Dissolve}, {PDF_NAME(Glitter), TransitionType::Glitter},
        {PDF_NAME(Fly), TransitionType::Fly},           {PDF_NAME(Push), TransitionType::Push},
        {PDF_NAME(Cover), TransitionType::Cover},       {PDF_NAME(Uncover), TransitionType::Uncover},
        {PDF_NAME(Fade), TransitionType::Fade},
    };
    for (const auto& [name, type] : names) {
        if (pdf_name_eq(ctx, s, name)) {
            out->type = static_cast<int>(type);
        }
    }
}

PageTransition presentationFromRaw(const RawTransition& raw) {
    PageTransition t;
    t.type = static_cast<TransitionType>(raw.type);
    t.duration = raw.duration > 0 ? raw.duration : 1.0f;
    t.vertical = raw.vertical != 0;
    t.outwards = raw.outwards != 0;
    t.direction = raw.direction;
    t.pageDuration = raw.pageDuration > 0 ? raw.pageDuration : 0;
    return t;
}

}  // namespace

std::string_view transitionName(TransitionType type) noexcept {
    switch (type) {
        case TransitionType::Split:
            return "Split";
        case TransitionType::Blinds:
            return "Blinds";
        case TransitionType::Box:
            return "Box";
        case TransitionType::Wipe:
            return "Wipe";
        case TransitionType::Dissolve:
            return "Dissolve";
        case TransitionType::Glitter:
            return "Glitter";
        case TransitionType::Fly:
            return "Fly";
        case TransitionType::Push:
            return "Push";
        case TransitionType::Cover:
            return "Cover";
        case TransitionType::Uncover:
            return "Uncover";
        case TransitionType::Fade:
            return "Fade";
        case TransitionType::None:
            break;
    }
    return "None";
}

PageTransition pageTransition(Document& doc, int page) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    RawTransition raw;
    mu::call(ctx, [&] { presentationRead(ctx, pdf_lookup_page_obj(ctx, pdf, page), &raw); });
    return presentationFromRaw(raw);
}

std::vector<PageTransition> transitions(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return std::vector<PageTransition>(static_cast<std::size_t>(doc.pageCount()));
    }
    fz_context* ctx = doc.context().get();
    int count = 0;
    mu::call(ctx, [&] { count = pdf_count_pages(ctx, pdf); });
    std::vector<PageTransition> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        RawTransition raw;
        mu::call(ctx, [&] { presentationRead(ctx, pdf_lookup_page_obj(ctx, pdf, i), &raw); });
        out.push_back(presentationFromRaw(raw));
    }
    return out;
}

}  // namespace ot::presentation
