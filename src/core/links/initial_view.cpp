#include "core/links/initial_view.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

// Явное назначение → «#page=N&view=…» (source/pdf/pdf-annot-imp.h; в публичных заголовках нет).
char* pdf_parse_link_dest(fz_context* ctx, pdf_document* doc, pdf_obj* dest);
}

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

}  // namespace ot
