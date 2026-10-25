#include "core/annot/fdf.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>

#include "core/annot/annot_internal.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

namespace ot::annot {

namespace {

// Ключи, которые в копию не идут: страница, окно, родитель окна и ответ — их ставят заново.
bool skippedKey(fz_context* ctx, pdf_obj* key) {
    return pdf_name_eq(ctx, key, PDF_NAME(P)) || pdf_name_eq(ctx, key, PDF_NAME(Popup)) ||
           pdf_name_eq(ctx, key, PDF_NAME(IRT)) || pdf_name_eq(ctx, key, PDF_NAME(Parent)) ||
           pdf_name_eq(ctx, key, PDF_NAME(Page));
}

// Копия словаря obj косвенным объектом документа dst (значения — pdf_graft_mapped_object). Внутри mu::call;
// новая ссылка на косвенный объект (он уже в документе — сбой посреди не оставляет висячего словаря).
pdf_obj* graftDict(fz_context* ctx, pdf_document* dst, pdf_graft_map* map, pdf_obj* obj) {
    const int len = pdf_dict_len(ctx, obj);
    pdf_obj* copy = pdf_add_new_dict(ctx, dst, len + 1);
    for (int k = 0; k < len; ++k) {
        pdf_obj* key = pdf_dict_get_key(ctx, obj, k);
        if (skippedKey(ctx, key)) {
            continue;
        }
        pdf_dict_put_drop(ctx, copy, key, pdf_graft_mapped_object(ctx, map, pdf_dict_get_val(ctx, obj, k)));
    }
    return copy;
}

// /F словаря FDF: строка или спецификация файла (/UF, /F).
const char* fdfFileName(fz_context* ctx, pdf_obj* f) {
    if (pdf_is_dict(ctx, f)) {
        pdf_obj* uf = pdf_dict_get(ctx, f, PDF_NAME(UF));
        f = pdf_is_string(ctx, uf) ? uf : pdf_dict_get(ctx, f, PDF_NAME(F));
    }
    return pdf_is_string(ctx, f) ? pdf_to_text_string(ctx, f) : "";
}

// Открытый FDF.
struct FdfSource {
    explicit FdfSource(fz_context* c) : ctx(c), buffer(c), stream(c), doc(c) {}
    fz_context* ctx;
    mu::BufferHandle buffer;
    mu::StreamHandle stream;
    mu::PdfDocumentHandle doc;
    pdf_obj* annots = nullptr;  // /FDF /Annots (заимствован у doc)
    std::string file;
};

void openFdf(FdfSource& src, std::string_view bytes) {
    fz_context* ctx = src.ctx;
    const char* fileName = nullptr;
    int ok = 0;
    try {
        mu::call(ctx, [&] {
            src.buffer.reset(fz_new_buffer_from_copied_data(
                ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
            src.stream.reset(fz_open_buffer(ctx, src.buffer));
            src.doc.reset(pdf_open_document_with_stream(ctx, src.stream));
            pdf_obj* fdf = pdf_dict_getp(ctx, pdf_trailer(ctx, src.doc), "Root/FDF");
            src.annots = pdf_dict_get(ctx, fdf, PDF_NAME(Annots));
            ok = pdf_is_dict(ctx, fdf) ? 1 : 0;
            fileName = fdfFileName(ctx, pdf_dict_get(ctx, fdf, PDF_NAME(F)));
        });
    } catch (const MuError& e) {
        throw ExchangeError(ExchangeError::Kind::Malformed, std::string("FDF cannot be read: ") + e.what());
    }
    if (ok == 0 || bytes.substr(0, 5) != "%FDF-") {
        throw ExchangeError(ExchangeError::Kind::Malformed, "not an FDF file");
    }
    src.file = fileName != nullptr ? fileName : "";
}

// Запись /Annots FDF, прочитанная внутри mu::call.
struct FdfEntry {
    int num = 0;
    bool popup = false;
    int page = -1;
    int irt = 0;
    int parent = 0;
    std::string nm;
};

std::vector<FdfEntry> fdfEntries(FdfSource& src) {
    fz_context* ctx = src.ctx;
    pdf_obj* annots = src.annots;
    int n = 0;
    mu::call(ctx, [&] { n = pdf_array_len(ctx, annots); });
    std::vector<FdfEntry> out(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const char* nm = nullptr;
        FdfEntry& e = out[static_cast<std::size_t>(i)];
        mu::call(ctx, [&] {
            pdf_obj* obj = pdf_array_get(ctx, annots, i);
            e.num = pdf_to_num(ctx, obj);
            e.popup = pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Popup)) ? 1 : 0;
            pdf_obj* page = pdf_dict_get(ctx, obj, PDF_NAME(Page));
            e.page = pdf_is_int(ctx, page) ? pdf_to_int(ctx, page) : (pdf_is_dict(ctx, obj) ? 0 : -1);
            e.irt = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(IRT)));
            e.parent = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Parent)));
            nm = pdf_dict_get_text_string(ctx, obj, PDF_NAME(NM));
        });
        e.nm = nm != nullptr ? nm : "";
    }
    return out;
}

}  // namespace

std::string exportFdf(Document& doc, const std::vector<AnnotRef>& refs, const std::string& fileName) {
    detail::pdfOf(doc);
    const std::vector<AnnotRef> set = detail::exchangeSet(doc, refs);
    fz_context* ctx = doc.context().get();
    mu::PdfDocumentHandle dst(ctx);
    mu::GraftMapHandle map(ctx);
    mu::PdfObjHandle list(ctx);
    mu::call(ctx, [&] {
        dst.reset(pdf_create_document(ctx));
        map.reset(pdf_new_graft_map(ctx, dst));
        list.reset(pdf_new_array(ctx, dst, static_cast<int>(set.size()) + 4));
    });
    std::map<int, std::unique_ptr<detail::PageAnnots>> pages;
    std::vector<int> srcNums(set.size(), 0);
    std::vector<pdf_obj*> copies(set.size(), nullptr);  // словари копий (принадлежат dst)
    std::vector<int> dstNums(set.size(), 0);            // их номера (pdf_to_num словаря — 0)
    std::vector<int> irts(set.size(), 0);
    std::vector<pdf_obj*> popups(set.size(), nullptr);  // окна-источники (заимствованы у документа)
    for (std::size_t i = 0; i < set.size(); ++i) {
        const AnnotRef& ref = set[i];
        auto& pa = pages[ref.page];
        if (!pa) {
            pa = std::make_unique<detail::PageAnnots>(doc, ref.page);
        }
        pdf_obj* obj = pa->object(ref.objnum);
        if (obj == nullptr) {
            continue;
        }
        const int page = ref.page;
        mu::call(ctx, [&] {
            pdf_obj* indirect = graftDict(ctx, dst, map, obj);
            pdf_dict_put_int(ctx, indirect, PDF_NAME(Page), page);
            copies[i] = pdf_resolve_indirect(ctx, indirect);
            dstNums[i] = pdf_to_num(ctx, indirect);
            pdf_array_push_drop(ctx, list, indirect);
            irts[i] = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(IRT)));
            pdf_obj* popup = pdf_dict_get(ctx, obj, PDF_NAME(Popup));
            popups[i] = pdf_is_dict(ctx, popup) ? popup : nullptr;
        });
        srcNums[i] = ref.objnum;
    }
    for (std::size_t i = 0; i < set.size(); ++i) {
        if (copies[i] == nullptr) {
            continue;
        }
        const auto parent = std::find(srcNums.begin(), srcNums.end(), irts[i]);
        pdf_obj* copy = copies[i];
        if (irts[i] > 0 && parent != srcNums.end()) {
            const int target = dstNums[static_cast<std::size_t>(parent - srcNums.begin())];
            mu::call(ctx, [&] {
                pdf_dict_put_drop(ctx, copy, PDF_NAME(IRT), pdf_new_indirect(ctx, dst, target, 0));
            });
        }
        if (popups[i] != nullptr) {
            pdf_obj* popup = popups[i];
            const int page = set[i].page;
            const int self = dstNums[i];
            mu::call(ctx, [&] {
                pdf_obj* indirect = graftDict(ctx, dst, map, popup);
                pdf_dict_put_int(ctx, indirect, PDF_NAME(Page), page);
                pdf_dict_put_drop(ctx, indirect, PDF_NAME(Parent), pdf_new_indirect(ctx, dst, self, 0));
                pdf_dict_put(ctx, copy, PDF_NAME(Popup), indirect);
                pdf_array_push_drop(ctx, list, indirect);
            });
        }
    }
    mu::BufferHandle buf(ctx);
    mu::OutputHandle out(ctx);
    const char* name = fileName.c_str();
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, dst), PDF_NAME(Root));
        pdf_dict_del(ctx, root, PDF_NAME(Type));
        pdf_dict_del(ctx, root, PDF_NAME(Pages));
        pdf_dict_puts_drop(ctx, root, "FDF", pdf_new_dict(ctx, dst, 2));
        pdf_obj* fdf = pdf_dict_gets(ctx, root, "FDF");
        pdf_dict_put(ctx, fdf, PDF_NAME(Annots), list);
        if (*name != 0) {
            pdf_dict_put_text_string(ctx, fdf, PDF_NAME(F), name);
        }
        buf.reset(fz_new_buffer(ctx, 16384));
        out.reset(fz_new_output_with_buffer(ctx, buf));
        pdf_write_options opts = pdf_default_write_options;
        opts.do_garbage = 1;
        opts.do_compress = 1;
        pdf_write_document(ctx, dst, out, &opts);
        fz_close_output(ctx, out);
    });
    unsigned char* data = nullptr;
    const std::size_t len = fz_buffer_storage(ctx, buf, &data);
    std::string bytes(reinterpret_cast<const char*>(data), len);
    // Заголовок PDF той же длины → FDF (смещения таблицы ссылок не меняются).
    if (bytes.size() >= 8 && bytes.compare(0, 5, "%PDF-") == 0) {
        bytes.replace(0, 8, "%FDF-1.2");
    }
    return bytes;
}

ExchangeInfo probeFdf(std::string_view fdf) {
    fz_context* ctx = MuContext::forThisThread().get();
    FdfSource src(ctx);
    openFdf(src, fdf);
    ExchangeInfo info;
    info.file = src.file;
    for (const FdfEntry& e : fdfEntries(src)) {
        if (!e.popup) {
            ++info.annots;
            info.maxPage = std::max(info.maxPage, e.page);
        }
    }
    return info;
}

ImportReport importFdf(Document& doc, std::string_view fdf, const std::string& documentFile) {
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    FdfSource src(ctx);
    openFdf(src, fdf);
    if (!sameDocumentName(src.file, documentFile)) {
        throw ExchangeError(ExchangeError::Kind::ForeignDocument,
                            "FDF belongs to another document: " + src.file, src.file);
    }
    const std::vector<FdfEntry> entries = fdfEntries(src);
    ImportReport report;
    std::set<int> skippedPages;
    std::unordered_map<int, int> srcToDst;  // номер в FDF → номер в документе
    std::unordered_map<int, int> pageOf;    // номер в документе → страница
    std::map<int, std::vector<int>> touched;
    std::map<int, std::unique_ptr<detail::PageAnnots>> pages;
    const auto pageAnnots = [&](int page) -> detail::PageAnnots& {
        auto& pa = pages[page];
        if (!pa) {
            pa = std::make_unique<detail::PageAnnots>(doc, page);
        }
        return *pa;
    };
    mu::GraftMapHandle map(ctx);
    mu::call(ctx, [&] { map.reset(pdf_new_graft_map(ctx, pdf)); });
    pdf_obj* annots = src.annots;
    Journal::Operation op(doc, "Import comments");
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const FdfEntry& e = entries[i];
        if (e.popup) {
            continue;
        }
        if (e.page < 0 || e.page >= doc.pageCount()) {
            ++report.skipped;
            skippedPages.insert(e.page);
            continue;
        }
        mu::PdfObjHandle ref(ctx);
        mu::call(ctx, [&] {
            pdf_obj* obj = pdf_array_get(ctx, annots, static_cast<int>(i));
            ref.reset(graftDict(ctx, pdf, map, obj));
        });
        detail::PageAnnots& pa = pageAnnots(e.page);
        const bool replaced = detail::placeImported(pa, ref, e.nm);
        pa.refresh();
        int num = 0;
        mu::call(ctx, [&] { num = pdf_to_num(ctx, ref); });
        srcToDst[e.num] = num;
        pageOf[num] = e.page;
        touched[e.page].push_back(num);
        (replaced ? report.updated : report.created) += 1;
        report.refs.push_back({e.page, num, e.nm});
    }
    // Ответы: на перенесённого родителя, иначе — на запись страницы с его /NM.
    for (const FdfEntry& e : entries) {
        if (e.popup || e.irt <= 0 || !srcToDst.contains(e.num)) {
            continue;
        }
        const int self = srcToDst.at(e.num);
        int target = 0;
        if (const auto it = srcToDst.find(e.irt); it != srcToDst.end()) {
            target = it->second;
        } else {
            const auto parent = std::find_if(entries.begin(), entries.end(),
                                             [&](const FdfEntry& p) { return p.num == e.irt; });
            if (parent != entries.end()) {
                target = detail::findByName(pageAnnots(pageOf.at(self)), parent->nm);
            }
        }
        if (target > 0) {
            mu::call(ctx, [&] {
                pdf_obj* obj = pdf_load_object(ctx, pdf, self);
                pdf_dict_put_drop(ctx, obj, PDF_NAME(IRT), pdf_new_indirect(ctx, pdf, target, 0));
                pdf_drop_obj(ctx, obj);
            });
        }
    }
    // Окна записок — к своим родителям.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const FdfEntry& e = entries[i];
        if (!e.popup || !srcToDst.contains(e.parent)) {
            continue;
        }
        const int parentNum = srcToDst.at(e.parent);
        detail::PageAnnots& pa = pageAnnots(pageOf.at(parentNum));
        mu::PdfObjHandle popup(ctx);
        mu::PdfObjHandle parent(ctx);
        mu::call(ctx, [&] {
            pdf_obj* obj = pdf_array_get(ctx, annots, static_cast<int>(i));
            popup.reset(graftDict(ctx, pdf, map, obj));
            parent.reset(pdf_new_indirect(ctx, pdf, parentNum, 0));
        });
        detail::attachPopup(pa, parent, popup);
    }
    pages.clear();
    detail::finishImported(doc, touched);
    for (const auto& [page, list] : touched) {
        op.invalidate(page);
    }
    op.commit();
    report.skippedPages.assign(skippedPages.begin(), skippedPages.end());
    return report;
}

}  // namespace ot::annot
