#include "core/links/destinations.h"

#include <algorithm>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>

// Явное назначение → адрес «#page=N&view=…» в координатах MuPDF (source/pdf/pdf-annot-imp.h; в публичных
// заголовках его нет, в статической библиотеке — есть).
char* pdf_parse_link_dest(fz_context* ctx, pdf_document* doc, pdf_obj* dest);
}

namespace ot::dests {

namespace {

// Имена ключей словаря (дерево /Names /Dests уже собрано pdf_load_name_tree в словарь с ключами-именами).
void destKeys(fz_context* ctx, pdf_obj* dict, std::vector<std::string>& out) {
    int n = 0;
    mu::call(ctx, [&] { n = pdf_dict_len(ctx, dict); });
    for (int i = 0; i < n; ++i) {
        const char* key = nullptr;
        mu::call(ctx, [&] {
            pdf_obj* k = pdf_dict_get_key(ctx, dict, i);
            key = pdf_is_name(ctx, k) ? pdf_to_name(ctx, k) : nullptr;
        });
        if (key != nullptr && *key != 0) {
            out.emplace_back(key);
        }
    }
}

}  // namespace

std::vector<Destination> list(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    mu::PdfObjHandle tree(ctx);
    pdf_obj* catalogDests = nullptr;
    mu::call(ctx, [&] {
        tree.reset(pdf_load_name_tree(ctx, pdf, PDF_NAME(Dests)));
        catalogDests =
            pdf_dict_get(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root)), PDF_NAME(Dests));
    });
    std::vector<std::string> names;
    if (tree) {
        destKeys(ctx, tree, names);
    }
    int isDict = 0;
    mu::call(ctx, [&] { isDict = pdf_is_dict(ctx, catalogDests); });
    if (isDict != 0) {
        destKeys(ctx, catalogDests, names);
    }
    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());

    std::vector<Destination> out;
    out.reserve(names.size());
    // Назначение ищется само: pdf_lookup_dest MuPDF при /Dests каталога в дерево /Names не заглядывает.
    for (const std::string& name : names) {
        char* uri = nullptr;
        mu::call(ctx, [&] {
            pdf_obj* d = isDict != 0 ? pdf_dict_gets(ctx, catalogDests, name.c_str()) : nullptr;
            if (d == nullptr && tree) {
                d = pdf_dict_gets(ctx, tree, name.c_str());
            }
            if (pdf_is_dict(ctx, d)) {
                d = pdf_dict_get(ctx, d, PDF_NAME(D));
            }
            if (pdf_is_array(ctx, d)) {
                uri = pdf_parse_link_dest(ctx, pdf, d);
            }
        });
        const std::string link = uri != nullptr ? std::string(uri) : std::string();
        fz_free(ctx, uri);
        Destination d;
        d.name = name;
        d.action = links::actionFromUri(doc, link);
        d.action.destName = name;
        out.push_back(std::move(d));
    }
    return out;
}

}  // namespace ot::dests
