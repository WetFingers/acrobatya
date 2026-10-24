#include "core/links/initial_view.h"

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

// Явное назначение → «#page=N&view=…» (source/pdf/pdf-annot-imp.h; в публичных заголовках нет).
char* pdf_parse_link_dest(fz_context* ctx, pdf_document* doc, pdf_obj* dest);
}

#include <stdexcept>

namespace ot {

namespace {

// Ключи каталога и /ViewerPreferences, прочитанные внутри mu::call: строки — в объектах документа.
struct RawInitialView {
    const char* pageMode = nullptr;
    const char* pageLayout = nullptr;
    const char* nonFullScreen = nullptr;
    const char* direction = nullptr;
    const char* title = nullptr;
    char* openUri = nullptr;  // fz_free
    int hideToolbar = 0;
    int hideMenubar = 0;
    int hideWindowUI = 0;
    int fitWindow = 0;
    int centerWindow = 0;
    int displayDocTitle = 0;
};

const char* initialName(fz_context* ctx, pdf_obj* obj) {
    return pdf_is_name(ctx, obj) ? pdf_to_name(ctx, obj) : nullptr;
}

// Вызывать внутри mu::call: /OpenAction — массив-назначение или действие /GoTo (с /D — массивом или именем).
char* initialOpenUri(fz_context* ctx, pdf_document* pdf, pdf_obj* action) {
    pdf_obj* dest = nullptr;
    if (pdf_is_array(ctx, action)) {
        dest = action;
    } else if (pdf_is_dict(ctx, action) &&
               pdf_name_eq(ctx, pdf_dict_get(ctx, action, PDF_NAME(S)), PDF_NAME(GoTo))) {
        dest = pdf_dict_get(ctx, action, PDF_NAME(D));
    }
    if (pdf_is_array(ctx, dest)) {
        return pdf_parse_link_dest(ctx, pdf, dest);
    }
    if (pdf_is_name(ctx, dest)) {
        return pdf_new_uri_from_path_and_named_dest(ctx, nullptr, pdf_to_name(ctx, dest));
    }
    if (pdf_is_string(ctx, dest)) {
        return pdf_new_uri_from_path_and_named_dest(ctx, nullptr, pdf_to_text_string(ctx, dest));
    }
    return nullptr;
}

std::string initialString(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

const char* fitName(links::DestFit fit) {
    switch (fit) {
        case links::DestFit::FitH:
            return "FitH";
        case links::DestFit::FitV:
            return "FitV";
        case links::DestFit::FitB:
            return "FitB";
        case links::DestFit::FitBH:
            return "FitBH";
        case links::DestFit::FitBV:
            return "FitBV";
        case links::DestFit::XYZ:
            return "XYZ";
        default:
            break;
    }
    return "Fit";
}

const char* nameOrNull(const std::string& s) { return s.empty() ? nullptr : s.c_str(); }

pdf_document* requireInitialPdf(Document& doc, const char* what) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        throw std::logic_error(std::string(what) + ": not a PDF");
    }
    return pdf;
}

// Вызывать внутри mu::call: имя или удалить ключ.
void putNameOrDelete(fz_context* ctx, pdf_obj* dict, const char* key, const char* value) {
    if (value != nullptr) {
        pdf_dict_puts_drop(ctx, dict, key, pdf_new_name(ctx, value));
    } else {
        pdf_dict_dels(ctx, dict, key);
    }
}

void putFlag(fz_context* ctx, pdf_obj* dict, const char* key, bool on) {
    if (on) {
        pdf_dict_puts(ctx, dict, key, PDF_TRUE);
    } else {
        pdf_dict_dels(ctx, dict, key);
    }
}

}  // namespace

InitialView readInitialView(Document& doc) {
    InitialView v;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return v;
    }
    fz_context* ctx = doc.context().get();
    RawInitialView raw;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        raw.pageMode = initialName(ctx, pdf_dict_get(ctx, root, PDF_NAME(PageMode)));
        raw.pageLayout = initialName(ctx, pdf_dict_gets(ctx, root, "PageLayout"));
        pdf_obj* prefs = pdf_dict_gets(ctx, root, "ViewerPreferences");
        raw.hideToolbar = pdf_to_bool(ctx, pdf_dict_gets(ctx, prefs, "HideToolbar"));
        raw.hideMenubar = pdf_to_bool(ctx, pdf_dict_gets(ctx, prefs, "HideMenubar"));
        raw.hideWindowUI = pdf_to_bool(ctx, pdf_dict_gets(ctx, prefs, "HideWindowUI"));
        raw.fitWindow = pdf_to_bool(ctx, pdf_dict_gets(ctx, prefs, "FitWindow"));
        raw.centerWindow = pdf_to_bool(ctx, pdf_dict_gets(ctx, prefs, "CenterWindow"));
        raw.displayDocTitle = pdf_to_bool(ctx, pdf_dict_gets(ctx, prefs, "DisplayDocTitle"));
        raw.nonFullScreen = initialName(ctx, pdf_dict_gets(ctx, prefs, "NonFullScreenPageMode"));
        raw.direction = initialName(ctx, pdf_dict_gets(ctx, prefs, "Direction"));
        pdf_obj* title =
            pdf_dict_get(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Info)), PDF_NAME(Title));
        raw.title = pdf_is_string(ctx, title) ? pdf_to_text_string(ctx, title) : nullptr;
        raw.openUri = initialOpenUri(ctx, pdf, pdf_dict_gets(ctx, root, "OpenAction"));
    });
    v.pageMode = initialString(raw.pageMode);
    v.pageLayout = initialString(raw.pageLayout);
    v.hideToolbar = raw.hideToolbar != 0;
    v.hideMenubar = raw.hideMenubar != 0;
    v.hideWindowUI = raw.hideWindowUI != 0;
    v.fitWindow = raw.fitWindow != 0;
    v.centerWindow = raw.centerWindow != 0;
    v.displayDocTitle = raw.displayDocTitle != 0;
    v.nonFullScreenPageMode = initialString(raw.nonFullScreen);
    v.direction = initialString(raw.direction);
    v.title = initialString(raw.title);
    const std::string uri = initialString(raw.openUri);
    fz_free(ctx, raw.openUri);
    if (!uri.empty()) {
        links::LinkAction action = links::actionFromUri(doc, uri);
        if (action.kind == links::LinkKind::GoTo && action.page >= 0) {
            v.openAction = std::move(action);
        }
    }
    return v;
}

void writeInitialView(Document& doc, const InitialView& v) {
    pdf_document* pdf = requireInitialPdf(doc, "initial view");
    fz_context* ctx = doc.context().get();
    const int pages = doc.pageCount();
    const bool hasAction = v.openAction.has_value() && v.openAction->kind == links::LinkKind::GoTo &&
                           v.openAction->page >= 0 && v.openAction->page < pages;
    const int page = hasAction ? v.openAction->page : -1;
    const links::DestFit fit = hasAction ? v.openAction->fit : links::DestFit::Fit;
    const char* fitKey = fitName(fit);
    const bool xyz = fit == links::DestFit::XYZ;
    const float zoom = hasAction ? v.openAction->zoom : 0.0f;
    const char* pageMode = nameOrNull(v.pageMode);
    const char* pageLayout = nameOrNull(v.pageLayout);
    const char* nonFullScreen = nameOrNull(v.nonFullScreenPageMode);
    const char* direction = nameOrNull(v.direction);
    const bool flags[] = {v.hideToolbar, v.hideMenubar,  v.hideWindowUI,
                          v.fitWindow,   v.centerWindow, v.displayDocTitle};
    const char* const flagKeys[] = {"HideToolbar", "HideMenubar",  "HideWindowUI",
                                    "FitWindow",   "CenterWindow", "DisplayDocTitle"};
    const bool anyPrefs = v.hideToolbar || v.hideMenubar || v.hideWindowUI || v.fitWindow || v.centerWindow ||
                          v.displayDocTitle || nonFullScreen != nullptr || direction != nullptr;

    Journal::Operation op(doc, "Initial view");
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        putNameOrDelete(ctx, root, "PageMode", pageMode);
        putNameOrDelete(ctx, root, "PageLayout", pageLayout);
        if (page >= 0) {
            pdf_obj* dest = pdf_new_array(ctx, pdf, 5);
            pdf_dict_puts_drop(ctx, root, "OpenAction", dest);
            pdf_array_push(ctx, dest, pdf_lookup_page_obj(ctx, pdf, page));
            pdf_array_push_name(ctx, dest, fitKey);
            if (xyz) {
                pdf_array_push(ctx, dest, PDF_NULL);
                pdf_array_push(ctx, dest, PDF_NULL);
                if (zoom > 0) {
                    pdf_array_push_real(ctx, dest, zoom);
                } else {
                    pdf_array_push(ctx, dest, PDF_NULL);
                }
            } else if (fit == links::DestFit::FitH || fit == links::DestFit::FitV ||
                       fit == links::DestFit::FitBH || fit == links::DestFit::FitBV) {
                pdf_array_push(ctx, dest, PDF_NULL);  // край вида — как есть
            }
        } else {
            pdf_dict_dels(ctx, root, "OpenAction");
        }
        pdf_obj* prefs = pdf_dict_gets(ctx, root, "ViewerPreferences");
        if (!pdf_is_dict(ctx, prefs) && anyPrefs) {
            pdf_dict_puts_drop(ctx, root, "ViewerPreferences", pdf_new_dict(ctx, pdf, 8));
            prefs = pdf_dict_gets(ctx, root, "ViewerPreferences");
        }
        if (pdf_is_dict(ctx, prefs)) {
            for (int i = 0; i < 6; ++i) {
                putFlag(ctx, prefs, flagKeys[i], flags[i]);
            }
            putNameOrDelete(ctx, prefs, "NonFullScreenPageMode", nonFullScreen);
            putNameOrDelete(ctx, prefs, "Direction", direction);
            if (pdf_dict_len(ctx, prefs) == 0) {
                pdf_dict_dels(ctx, root, "ViewerPreferences");
            }
        }
    });
    op.commit();
}

bool hasViewerPreferences(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return false;
    }
    fz_context* ctx = doc.context().get();
    int has = 0;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        has = pdf_dict_gets(ctx, root, "ViewerPreferences") != nullptr ? 1 : 0;
    });
    return has != 0;
}

std::string baseUrl(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    const char* base = nullptr;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* value = pdf_dict_gets(ctx, pdf_dict_gets(ctx, root, "URI"), "Base");
        base = pdf_is_string(ctx, value) ? pdf_to_text_string(ctx, value) : nullptr;
    });
    return initialString(base);
}

void setBaseUrl(Document& doc, const std::string& url) {
    pdf_document* pdf = requireInitialPdf(doc, "base url");
    fz_context* ctx = doc.context().get();
    const char* text = url.empty() ? nullptr : url.c_str();
    Journal::Operation op(doc, "Base URL");
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* uri = pdf_dict_gets(ctx, root, "URI");
        if (text != nullptr) {
            if (!pdf_is_dict(ctx, uri)) {
                pdf_dict_puts_drop(ctx, root, "URI", pdf_new_dict(ctx, pdf, 1));
                uri = pdf_dict_gets(ctx, root, "URI");
            }
            pdf_dict_puts_drop(ctx, uri, "Base", pdf_new_text_string(ctx, text));
        } else if (pdf_is_dict(ctx, uri)) {
            pdf_dict_dels(ctx, uri, "Base");
            if (pdf_dict_len(ctx, uri) == 0) {
                pdf_dict_dels(ctx, root, "URI");
            }
        }
    });
    op.commit();
}

}  // namespace ot
