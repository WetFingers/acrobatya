#include "core/links/links.h"

#include <cmath>
#include <cstring>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::links {

namespace {

using LinkHandle = mu::Handle<fz_link, fz_drop_link>;

// Ссылка MuPDF: рамка и адрес (адрес живёт, пока жив список fz_load_links).
struct MuLink {
    fz_rect rect{};
    const char* uri = nullptr;
    bool used = false;
};

// Словарь /Link, прочитанный внутри mu::call (без объектов с деструкторами): строки — указатели в объекты
// документа, живут, пока жив документ.
struct RawLink {
    int objnum = 0;
    fz_rect rect{};
    LinkKind kind = LinkKind::Unsupported;
    const char* action = nullptr;  // /S
    const char* named = nullptr;   // /N у /Named
    const char* file = nullptr;    // /F у /GoToR и /Launch
    const char* destName = nullptr;
    int remotePage = -1;  // /GoToR /D [целое …]
};

const char* linkFileSpec(fz_context* ctx, pdf_obj* fs) {
    if (pdf_is_string(ctx, fs)) {
        return pdf_to_text_string(ctx, fs);
    }
    if (pdf_is_dict(ctx, fs)) {
        for (pdf_obj* key : {PDF_NAME(UF), PDF_NAME(F), PDF_NAME(Unix), PDF_NAME(DOS)}) {
            pdf_obj* v = pdf_dict_get(ctx, fs, key);
            if (pdf_is_string(ctx, v)) {
                return pdf_to_text_string(ctx, v);
            }
        }
    }
    return nullptr;
}

const char* linkDestName(fz_context* ctx, pdf_obj* dest) {
    if (pdf_is_name(ctx, dest)) {
        return pdf_to_name(ctx, dest);
    }
    if (pdf_is_string(ctx, dest)) {
        return pdf_to_text_string(ctx, dest);
    }
    return nullptr;
}

// Вызывать внутри mu::call.
void linkReadRaw(fz_context* ctx, pdf_obj* dict, fz_matrix ctm, RawLink* out) {
    out->objnum = pdf_to_num(ctx, dict);
    out->rect = fz_transform_rect(pdf_dict_get_rect(ctx, dict, PDF_NAME(Rect)), ctm);
    pdf_obj* dest = pdf_dict_get(ctx, dict, PDF_NAME(Dest));
    if (dest != nullptr) {
        out->kind = LinkKind::GoTo;
        out->destName = linkDestName(ctx, dest);
        return;
    }
    pdf_obj* action = pdf_dict_get(ctx, dict, PDF_NAME(A));
    if (action == nullptr) {  // как у MuPDF: действие кнопки — /AA /U, иначе /AA /D
        action = pdf_dict_geta(ctx, pdf_dict_get(ctx, dict, PDF_NAME(AA)), PDF_NAME(U), PDF_NAME(D));
    }
    pdf_obj* s = pdf_dict_get(ctx, action, PDF_NAME(S));
    out->action = pdf_is_name(ctx, s) ? pdf_to_name(ctx, s) : nullptr;
    if (pdf_name_eq(ctx, s, PDF_NAME(GoTo))) {
        out->kind = LinkKind::GoTo;
        out->destName = linkDestName(ctx, pdf_dict_get(ctx, action, PDF_NAME(D)));
    } else if (pdf_name_eq(ctx, s, PDF_NAME(GoToR))) {
        out->kind = LinkKind::GoToR;
        out->file = linkFileSpec(ctx, pdf_dict_get(ctx, action, PDF_NAME(F)));
        pdf_obj* d = pdf_dict_get(ctx, action, PDF_NAME(D));
        if (pdf_is_array(ctx, d) && pdf_is_int(ctx, pdf_array_get(ctx, d, 0))) {
            out->remotePage = pdf_array_get_int(ctx, d, 0);
        } else if (pdf_is_int(ctx, d)) {
            out->remotePage = pdf_to_int(ctx, d);
        } else {
            out->destName = linkDestName(ctx, d);
        }
    } else if (pdf_name_eq(ctx, s, PDF_NAME(URI))) {
        out->kind = LinkKind::Uri;
    } else if (pdf_name_eq(ctx, s, PDF_NAME(Named))) {
        out->kind = LinkKind::Named;
        pdf_obj* n = pdf_dict_get(ctx, action, PDF_NAME(N));
        out->named = pdf_is_name(ctx, n) ? pdf_to_name(ctx, n) : nullptr;
    } else if (pdf_name_eq(ctx, s, PDF_NAME(Launch))) {
        out->kind = LinkKind::Launch;
        out->file = linkFileSpec(ctx, pdf_dict_get(ctx, action, PDF_NAME(F)));
        if (out->file == nullptr) {
            out->file = linkFileSpec(ctx, pdf_dict_getp(ctx, action, "Win/F"));
        }
    } else {
        out->kind = LinkKind::Unsupported;
    }
}

DestFit linkFitFrom(fz_link_dest_type type) {
    switch (type) {
        case FZ_LINK_DEST_FIT:
            return DestFit::Fit;
        case FZ_LINK_DEST_FIT_B:
            return DestFit::FitB;
        case FZ_LINK_DEST_FIT_H:
            return DestFit::FitH;
        case FZ_LINK_DEST_FIT_BH:
            return DestFit::FitBH;
        case FZ_LINK_DEST_FIT_V:
            return DestFit::FitV;
        case FZ_LINK_DEST_FIT_BV:
            return DestFit::FitBV;
        case FZ_LINK_DEST_FIT_R:
            return DestFit::FitR;
        case FZ_LINK_DEST_XYZ:
            return DestFit::XYZ;
    }
    return DestFit::None;
}

// Назначение адреса «#…» в этом документе: страница, вид, точка, масштаб.
void linkResolve(Document& doc, const char* uri, LinkAction& action) {
    fz_context* ctx = doc.context().get();
    fz_document* fz = doc.handle();
    fz_link_dest dest = fz_make_link_dest_none();
    int page = -1;
    mu::call(ctx, [&] {
        dest = fz_resolve_link_dest(ctx, fz, uri);
        page = fz_page_number_from_location(ctx, fz, dest.loc);
    });
    action.page = page;
    if (page < 0) {
        return;
    }
    action.fit = linkFitFrom(dest.type);
    if (!std::isnan(dest.x) || !std::isnan(dest.y)) {
        action.point = PointF{dest.x, dest.y};  // компонента, которой нет в назначении, — NaN
    }
    action.zoom = std::isfinite(dest.zoom) && dest.zoom > 0 ? dest.zoom / 100.0f : 0.0f;
}

bool linkSameRect(const fz_rect& a, const fz_rect& b) {
    constexpr float kEps = 0.01f;
    return std::fabs(a.x0 - b.x0) < kEps && std::fabs(a.y0 - b.y0) < kEps && std::fabs(a.x1 - b.x1) < kEps &&
           std::fabs(a.y1 - b.y1) < kEps;
}

std::string linkString(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

}  // namespace

std::string_view linkKindName(LinkKind kind) noexcept {
    switch (kind) {
        case LinkKind::GoTo:
            return "GoTo";
        case LinkKind::GoToR:
            return "GoToR";
        case LinkKind::Uri:
            return "URI";
        case LinkKind::Named:
            return "Named";
        case LinkKind::Launch:
            return "Launch";
        case LinkKind::Unsupported:
            break;
    }
    return "Unsupported";
}

std::vector<LinkInfo> pageLinks(Document& doc, int page) {
    fz_context* ctx = doc.context().get();
    fz_document* fz = doc.handle();
    mu::PageHandle handle(ctx);
    LinkHandle head(ctx);
    mu::call(ctx, [&] {
        handle.reset(fz_load_page(ctx, fz, page));
        head.reset(fz_load_links(ctx, handle));
    });
    std::vector<MuLink> mu;
    for (const fz_link* l = head.get(); l != nullptr; l = l->next) {
        mu.push_back({l->rect, l->uri, false});
    }

    std::vector<LinkInfo> result;
    pdf_page* ppage = nullptr;
    mu::call(ctx, [&] { ppage = pdf_page_from_fz_page(ctx, handle); });
    if (ppage == nullptr) {
        // Не PDF: только адреса MuPDF.
        for (const MuLink& l : mu) {
            LinkInfo info;
            info.page = page;
            info.rect = {l.rect.x0, l.rect.y0, l.rect.x1, l.rect.y1};
            info.action.uri = linkString(l.uri);
            int external = 0;
            mu::call(ctx, [&] { external = fz_is_external_link(ctx, l.uri); });
            if (external != 0) {
                info.action.kind = LinkKind::Uri;
            } else {
                info.action.kind = LinkKind::GoTo;
                linkResolve(doc, l.uri, info.action);
            }
            result.push_back(std::move(info));
        }
        return result;
    }

    pdf_obj* annots = nullptr;
    fz_matrix ctm = fz_identity;
    int n = 0;
    mu::call(ctx, [&] {
        pdf_page_transform(ctx, ppage, nullptr, &ctm);
        annots = pdf_dict_get(ctx, ppage->obj, PDF_NAME(Annots));
        n = pdf_array_len(ctx, annots);
    });
    for (int i = 0; i < n; ++i) {
        RawLink raw;
        int isLink = 0;
        mu::call(ctx, [&] {
            pdf_obj* dict = pdf_array_get(ctx, annots, i);
            isLink = pdf_is_dict(ctx, dict) &&
                     pdf_name_eq(ctx, pdf_dict_get(ctx, dict, PDF_NAME(Subtype)), PDF_NAME(Link)) &&
                     pdf_is_array(ctx, pdf_dict_get(ctx, dict, PDF_NAME(Rect)));
            if (isLink != 0) {
                linkReadRaw(ctx, dict, ctm, &raw);
            }
        });
        if (isLink == 0) {
            continue;
        }
        LinkInfo info;
        info.page = page;
        info.objnum = raw.objnum;
        info.rect = {raw.rect.x0, raw.rect.y0, raw.rect.x1, raw.rect.y1};
        LinkAction& action = info.action;
        action.kind = raw.kind;
        for (MuLink& l : mu) {
            if (!l.used && linkSameRect(l.rect, raw.rect)) {
                l.used = true;
                action.uri = linkString(l.uri);
                break;
            }
        }
        action.file = linkString(raw.file);
        action.destName = linkString(raw.destName);
        switch (raw.kind) {
            case LinkKind::GoTo:
            case LinkKind::Named:
                action.name = linkString(raw.named);
                if (!action.uri.empty()) {
                    linkResolve(doc, action.uri.c_str(), action);
                }
                if (raw.kind == LinkKind::Named && action.uri.empty()) {
                    action.kind = LinkKind::Unsupported;  // неизвестное имя — MuPDF перехода не знает
                }
                break;
            case LinkKind::GoToR:
                action.page = raw.remotePage;
                break;
            case LinkKind::Unsupported:
                action.name = linkString(raw.action);
                break;
            default:
                break;
        }
        result.push_back(std::move(info));
    }
    return result;
}

LinkAction actionFromUri(Document& doc, std::string_view uri) {
    LinkAction action;
    if (uri.empty()) {
        return action;
    }
    action.uri = std::string(uri);
    fz_context* ctx = doc.context().get();
    int external = 0;
    mu::call(ctx, [&] { external = fz_is_external_link(ctx, action.uri.c_str()); });
    if (external == 0) {
        action.kind = LinkKind::GoTo;
        linkResolve(doc, action.uri.c_str(), action);
        return action;
    }
    constexpr std::string_view kFile = "file:";
    if (uri.substr(0, kFile.size()) == kFile) {
        // «file:путь#page=N» или «file:путь#nameddest=имя» (pdf_new_uri_from_path_and_*).
        action.kind = LinkKind::GoToR;
        const std::string_view rest = uri.substr(kFile.size());
        const std::size_t hash = rest.find('#');
        action.file = std::string(rest.substr(0, hash));
        if (hash != std::string_view::npos) {
            const std::string_view frag = rest.substr(hash + 1);
            constexpr std::string_view kPage = "page=";
            constexpr std::string_view kNamed = "nameddest=";
            if (frag.substr(0, kPage.size()) == kPage) {
                int n = 0;
                for (const char c : frag.substr(kPage.size())) {
                    if (c < '0' || c > '9') {
                        break;
                    }
                    n = n * 10 + (c - '0');
                }
                action.page = n > 0 ? n - 1 : -1;
            } else if (frag.substr(0, kNamed.size()) == kNamed) {
                action.destName = std::string(frag.substr(kNamed.size()));
            }
        }
        return action;
    }
    action.kind = LinkKind::Uri;
    return action;
}

std::optional<LinkInfo> linkAt(const std::vector<LinkInfo>& links, PointF point, float tolerance) {
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        if (rectContains(it->rect, point)) {
            return *it;
        }
    }
    if (tolerance <= 0) {
        return std::nullopt;
    }
    const LinkInfo* best = nullptr;
    float bestDistance = tolerance;
    for (auto it = links.rbegin(); it != links.rend(); ++it) {
        const float d = rectDistance(it->rect, point);
        if (d <= bestDistance) {
            bestDistance = d;
            best = &*it;
        }
    }
    return best != nullptr ? std::optional<LinkInfo>(*best) : std::nullopt;
}

}  // namespace ot::links
