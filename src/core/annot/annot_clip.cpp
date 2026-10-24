// Копия аннотаций в буфер и вставка (план 5.1; эталон 3.4 «Выделение и правка», comment-43): копия — свой
// одностраничный PDF в памяти (страница той же /MediaBox и /Rotate), куда pdf_graft переносит словарь
// аннотации без /P, /Popup, /IRT, /Parent вместе с внешним видом и всеми его ресурсами (шрифты, картинки,
// вложение); рядом JSON — свойства (toJson) и матрица исходной страницы. Вставка — pdf_graft из этого PDF в
// документ, новый /NM, геометрия переносится так, чтобы в координатах страницы аннотация встала туда же, где
// была (Acrobat), со сдвигом offset; не чистый сдвиг (другой поворот страницы) — внешний вид синтезируется
// заново. Окно записки (/Popup) создаётся заново по рамке из JSON.
#include <nlohmann/json.hpp>

#include <cmath>
#include <map>
#include <memory>

#include "core/annot/annot_internal.h"
#include "core/annot/annots.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"

namespace ot::annot {

namespace annot_clip_detail {

using nlohmann::json;

json matrixJson(fz_matrix m) { return json::array({m.a, m.b, m.c, m.d, m.e, m.f}); }

fz_matrix matrixFrom(const json& j) {
    if (!j.is_array() || j.size() != 6) {
        return fz_identity;
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>(),
            j[3].get<float>(), j[4].get<float>(), j[5].get<float>()};
}

bool pureTranslation(fz_matrix m) {
    return std::fabs(m.a - 1) < 1e-4f && std::fabs(m.d - 1) < 1e-4f && std::fabs(m.b) < 1e-4f &&
           std::fabs(m.c) < 1e-4f;
}

}  // namespace annot_clip_detail

AnnotClip copy(Document& doc, const std::vector<AnnotRef>& refs) {
    using namespace annot_clip_detail;
    pdf_document* pdf = detail::pdfOf(doc);
    (void)pdf;
    fz_context* ctx = doc.context().get();
    mu::PdfDocumentHandle clip(ctx);
    mu::GraftMapHandle map(ctx);
    mu::call(ctx, [&] {
        clip.reset(pdf_create_document(ctx));
        map.reset(pdf_new_graft_map(ctx, clip));
    });
    json items = json::array();
    std::map<int, std::unique_ptr<detail::PageAnnots>> pages;
    std::map<int, int> clipPageOf;  // страница документа → страница копии
    std::map<int, int> clipCount;   // страница копии → аннотаций в ней
    for (const AnnotRef& ref : refs) {
        if (!ref.valid() || ref.page >= doc.pageCount()) {
            detail::notFound(ref);
        }
        auto& slot = pages[ref.page];
        if (!slot) {
            slot = std::make_unique<detail::PageAnnots>(doc, ref.page);
        }
        detail::PageAnnots& pa = *slot;
        pdf_obj* obj = pa.object(ref.objnum);
        if (obj == nullptr) {
            detail::notFound(ref);
        }
        const std::optional<AnnotProps> props = readProps(doc, ref);
        if (!props || props->type == AnnotType::Popup || props->type == AnnotType::Widget ||
            props->type == AnnotType::Link) {
            continue;  // окна записок, поля и ссылки не копируются
        }
        if (clipPageOf.count(ref.page) == 0) {
            const int index = static_cast<int>(clipPageOf.size());
            pdf_obj* source = pa.page()->obj;
            mu::PdfObjHandle added(ctx);
            mu::call(ctx, [&] {
                const fz_rect media = pdf_dict_get_inheritable_rect(ctx, source, PDF_NAME(MediaBox));
                const int rotate = pdf_dict_get_inheritable_int(ctx, source, PDF_NAME(Rotate));
                added.reset(pdf_add_page(ctx, clip, media, rotate, nullptr, nullptr));
                pdf_insert_page(ctx, clip, -1, added);
            });
            clipPageOf[ref.page] = index;
        }
        const int clipPage = clipPageOf[ref.page];
        mu::PdfObjHandle shallow(ctx);
        mu::PdfObjHandle grafted(ctx);
        mu::PdfObjHandle indirect(ctx);
        mu::call(ctx, [&] {
            shallow.reset(pdf_copy_dict(ctx, obj));
            pdf_dict_del(ctx, shallow, PDF_NAME(P));
            pdf_dict_del(ctx, shallow, PDF_NAME(Popup));
            pdf_dict_del(ctx, shallow, PDF_NAME(IRT));
            pdf_dict_del(ctx, shallow, PDF_NAME(Parent));
            grafted.reset(pdf_graft_mapped_object(ctx, map, shallow));
            indirect.reset(pdf_add_object(ctx, clip, grafted));
            pdf_obj* page = pdf_lookup_page_obj(ctx, clip, clipPage);
            pdf_obj* annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
            if (annots == nullptr) {
                annots = pdf_dict_put_array(ctx, page, PDF_NAME(Annots), 4);
            }
            pdf_dict_put(ctx, indirect, PDF_NAME(P), page);
            pdf_array_push(ctx, annots, indirect);
        });
        items.push_back({{"page", clipPage},
                         {"index", clipCount[clipPage]++},
                         {"ctm", matrixJson(pa.ctm())},
                         {"props", json::parse(toJson(*props))}});
    }
    AnnotClip result;
    result.count = static_cast<int>(items.size());
    if (result.count == 0) {
        return result;
    }
    mu::BufferHandle buffer(ctx);
    mu::OutputHandle out(ctx);
    mu::call(ctx, [&] {
        buffer.reset(fz_new_buffer(ctx, 8192));
        out.reset(fz_new_output_with_buffer(ctx, buffer));
        pdf_write_options options = pdf_default_write_options;
        options.do_compress = 1;
        pdf_write_document(ctx, clip, out, &options);
        fz_close_output(ctx, out);
    });
    unsigned char* data = nullptr;
    std::size_t size = 0;
    mu::call(ctx, [&] { size = fz_buffer_storage(ctx, buffer, &data); });
    result.pdf.assign(reinterpret_cast<const char*>(data), size);
    result.json = json{{"version", 1}, {"items", items}}.dump();
    return result;
}

std::vector<AnnotRef> paste(Document& doc, int page, const AnnotClip& clip, PointF offset) {
    using namespace annot_clip_detail;
    pdf_document* pdf = detail::pdfOf(doc);
    if (page < 0 || page >= doc.pageCount()) {
        detail::badArgument("page " + std::to_string(page) + " is out of range");
    }
    if (clip.empty() || clip.pdf.empty()) {
        detail::badArgument("annotation clipboard is empty");
    }
    json items;
    try {
        items = json::parse(clip.json).at("items");
    } catch (const json::exception& e) {
        detail::badArgument(std::string("annotation clipboard: ") + e.what());
    }
    if (!items.is_array() || items.empty()) {
        detail::badArgument("annotation clipboard has no annotations");
    }
    fz_context* ctx = doc.context().get();
    mu::StreamHandle stream(ctx);
    mu::PdfDocumentHandle source(ctx);
    mu::GraftMapHandle map(ctx);
    mu::call(ctx, [&] {
        stream.reset(
            fz_open_memory(ctx, reinterpret_cast<const unsigned char*>(clip.pdf.data()), clip.pdf.size()));
        source.reset(pdf_open_document_with_stream(ctx, stream));
        map.reset(pdf_new_graft_map(ctx, pdf));
    });
    detail::PageAnnots pa(doc, page);
    Journal::Operation op(doc, "Paste annotations");
    pdf_obj* annots = pa.ensureAnnots();
    pdf_obj* pageObj = pa.page()->obj;
    const fz_matrix inverse = fz_invert_matrix(pa.ctm());
    struct Pasted {
        int objnum = 0;
        std::string nm;
        AnnotProps props;
        bool resynth = false;
    };
    std::vector<Pasted> pasted;
    for (const json& item : items) {
        Pasted p;
        try {
            p.props = propsFromJson(item.at("props").dump());
        } catch (const std::exception& e) {
            detail::badArgument(std::string("annotation clipboard: ") + e.what());
        }
        const int clipPage = item.value("page", 0);
        const int clipIndex = item.value("index", 0);
        const fz_matrix m = fz_concat(
            fz_concat(matrixFrom(item.value("ctm", json())), fz_translate(offset.x, offset.y)), inverse);
        p.resynth = !pureTranslation(m);
        p.nm = newAnnotName();
        const bool resynth = p.resynth;
        mu::PdfObjHandle shallow(ctx);
        mu::PdfObjHandle grafted(ctx);
        mu::PdfObjHandle indirect(ctx);
        int objnum = 0;
        mu::call(ctx, [&] {
            pdf_obj* clipPageObj = pdf_lookup_page_obj(ctx, source, clipPage);
            pdf_obj* entry = pdf_array_get(ctx, pdf_dict_get(ctx, clipPageObj, PDF_NAME(Annots)), clipIndex);
            if (!pdf_is_dict(ctx, entry)) {
                fz_throw(ctx, FZ_ERROR_ARGUMENT, "annotation clipboard is damaged");
            }
            shallow.reset(pdf_copy_dict(ctx, entry));
            pdf_dict_del(ctx, shallow, PDF_NAME(P));
            pdf_dict_del(ctx, shallow, PDF_NAME(Popup));
            pdf_dict_del(ctx, shallow, PDF_NAME(IRT));
            pdf_dict_del(ctx, shallow, PDF_NAME(RT));
            pdf_dict_del(ctx, shallow, PDF_NAME(Parent));
            grafted.reset(pdf_graft_mapped_object(ctx, map, shallow));
            indirect.reset(pdf_add_object(ctx, pdf, grafted));
            pdf_dict_put(ctx, indirect, PDF_NAME(P), pageObj);
            pdf_dict_put_text_string(ctx, indirect, PDF_NAME(NM), p.nm.c_str());
            detail::transformGeometry(ctx, indirect, m);
            if (resynth) {
                pdf_dict_del(ctx, indirect, PDF_NAME(AP));
            }
            pdf_array_push(ctx, annots, indirect);
            objnum = pdf_to_num(ctx, indirect);
        });
        p.objnum = objnum;
        pasted.push_back(std::move(p));
    }
    pa.refresh(true);
    std::vector<AnnotRef> refs;
    for (const Pasted& p : pasted) {
        pdf_annot* a = pa.find(p.objnum);
        if (a != nullptr) {
            const bool popup = p.props.hasPopup();
            const fz_rect rect =
                fz_make_rect(p.props.popupRect.x0 + offset.x, p.props.popupRect.y0 + offset.y,
                             p.props.popupRect.x1 + offset.x, p.props.popupRect.y1 + offset.y);
            const int open = p.props.open ? 1 : 0;
            const bool resynth = p.resynth;
            mu::call(ctx, [&] {
                if (popup && pdf_annot_has_popup(ctx, a)) {
                    pdf_set_annot_popup(ctx, a, rect);
                    pdf_set_annot_is_open(ctx, a, open);
                }
                if (resynth) {
                    pdf_dirty_annot(ctx, a);
                }
                pdf_update_annot(ctx, a);
            });
        }
        refs.push_back({page, p.objnum, p.nm});
    }
    op.invalidate(page);
    op.commit();
    return refs;
}

}  // namespace ot::annot
