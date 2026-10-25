// Общее для обмена FDF и XFDF (план 5.4): набор записи, поиск по /NM, постановка перенесённой аннотации на
// страницу (совмещение по /NM), окна записок и синтез вида после импорта.
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core/annot/annot_internal.h"
#include "core/annot/comments.h"
#include "core/annot/fdf.h"
#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"

namespace ot::annot {

namespace {

// Имя файла без пути, строчными (латиница и кириллица — fz_tolower).
std::string baseName(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\:");
    if (slash != std::string_view::npos) {
        path.remove_prefix(slash + 1);
    }
    return foldCase(path);
}

// Типы, вид которых синтезирует MuPDF (pdf-appearance.c), — без поля формы.
bool synthesizable(std::string_view subtype) {
    static const std::set<std::string_view> kTypes = {
        "Ink",       "Polygon",  "PolyLine",       "Line",  "Square",    "Circle",
        "Caret",     "Text",     "FileAttachment", "Sound", "Highlight", "Underline",
        "StrikeOut", "Squiggly", "Redact",         "Stamp", "FreeText"};
    return kTypes.contains(subtype);
}

// Шрифты base14 в синтезированном MuPDF виде (штамп — Times-Bold, Helv, подпись линии-измерения — HelvCYR…)
// пишутся без /FirstChar /LastChar /Widths или без /FontDescriptor; PDF 2.0 и строгий pdfcpu их требуют.
// Дописать ширины WinAnsi 32–255 по метрикам base14 MuPDF (если их нет) и дескриптор грани.
void completeBase14Widths(fz_context* ctx, pdf_document* pdf, pdf_obj* annot) {
    std::vector<pdf_obj*> fonts;
    std::vector<std::string> names;
    mu::call(ctx, [&] {
        pdf_obj* dict = pdf_dict_getp(ctx, annot, "AP/N/Resources/Font");
        const int n = pdf_dict_len(ctx, dict);
        for (int i = 0; i < n; ++i) {
            pdf_obj* font = pdf_dict_get_val(ctx, dict, i);
            const char* base = pdf_dict_get_name(ctx, font, PDF_NAME(BaseFont));
            if (pdf_name_eq(ctx, pdf_dict_get(ctx, font, PDF_NAME(Subtype)), PDF_NAME(Type1)) &&
                (pdf_dict_get(ctx, font, PDF_NAME(Widths)) == nullptr ||
                 pdf_dict_get(ctx, font, PDF_NAME(FontDescriptor)) == nullptr) &&
                base != nullptr) {
                fonts.push_back(font);
                names.emplace_back(base);
            }
        }
    });
    for (std::size_t k = 0; k < fonts.size(); ++k) {
        mu::FontHandle base(ctx);
        pdf_obj* font = fonts[k];
        const char* name = names[k].c_str();
        mu::call(ctx, [&] {
            base.reset(fz_new_base14_font(ctx, name));
            if (base != nullptr && pdf_dict_get(ctx, font, PDF_NAME(Widths)) == nullptr) {
                pdf_obj* widths = pdf_new_array(ctx, pdf, 224);
                for (int c = 32; c <= 255; ++c) {
                    const int gid = fz_encode_character(ctx, base, fz_unicode_from_windows_1252[c]);
                    pdf_array_push_int(
                        ctx, widths,
                        static_cast<std::int64_t>(fz_advance_glyph(ctx, base, gid, 0) * 1000 + 0.5f));
                }
                pdf_dict_put_int(ctx, font, PDF_NAME(FirstChar), 32);
                pdf_dict_put_int(ctx, font, PDF_NAME(LastChar), 255);
                pdf_dict_put_drop(ctx, font, PDF_NAME(Widths), widths);
            }
            if (base != nullptr) {
                if (pdf_dict_get(ctx, font, PDF_NAME(FontDescriptor)) == nullptr) {
                    // Метрики грани base14 MuPDF (единицы 1/1000 кегля).
                    const std::string_view n(name);
                    const bool symbolic = n == "Symbol" || n == "ZapfDingbats";
                    const bool serif = n.rfind("Times", 0) == 0 || n.rfind("Courier", 0) == 0;
                    const bool italic = n.find("Italic") != std::string_view::npos ||
                                        n.find("Oblique") != std::string_view::npos;
                    const bool bold = n.find("Bold") != std::string_view::npos;
                    const fz_rect bbox = fz_font_bbox(ctx, base);
                    pdf_obj* fd = pdf_add_new_dict(ctx, pdf, 10);
                    pdf_dict_put(ctx, fd, PDF_NAME(Type), PDF_NAME(FontDescriptor));
                    pdf_dict_put_name(ctx, fd, PDF_NAME(FontName), name);
                    pdf_dict_put_int(ctx, fd, PDF_NAME(Flags),
                                     (symbolic ? 4 : 32) | (serif ? 2 : 0) | (italic ? 64 : 0));
                    pdf_dict_put_rect(
                        ctx, fd, PDF_NAME(FontBBox),
                        fz_make_rect(bbox.x0 * 1000, bbox.y0 * 1000, bbox.x1 * 1000, bbox.y1 * 1000));
                    pdf_dict_put_real(ctx, fd, PDF_NAME(ItalicAngle), italic ? -12 : 0);
                    pdf_dict_put_real(ctx, fd, PDF_NAME(Ascent), fz_font_ascender(ctx, base) * 1000);
                    pdf_dict_put_real(ctx, fd, PDF_NAME(Descent), fz_font_descender(ctx, base) * 1000);
                    pdf_dict_put_int(ctx, fd, PDF_NAME(CapHeight), 700);
                    pdf_dict_put_int(ctx, fd, PDF_NAME(StemV), bold ? 140 : 80);
                    pdf_dict_put_drop(ctx, font, PDF_NAME(FontDescriptor), fd);
                }
            }
        });
    }
}

}  // namespace

bool sameDocumentName(std::string_view exchangeFile, std::string_view documentFile) {
    if (exchangeFile.empty() || documentFile.empty()) {
        return true;
    }
    return baseName(exchangeFile) == baseName(documentFile);
}

namespace detail {

void completeBase14Fonts(fz_context* ctx, pdf_document* pdf, pdf_obj* annot) {
    completeBase14Widths(ctx, pdf, annot);
}

std::vector<AnnotRef> exchangeSet(Document& doc, const std::vector<AnnotRef>& refs) {
    std::vector<AnnotRef> out;
    if (refs.empty()) {
        for (int page = 0; page < doc.pageCount(); ++page) {
            for (const AnnotInfo& info : listAnnots(doc, page)) {
                if (info.type != AnnotType::Popup && info.type != AnnotType::Widget && info.ref.valid()) {
                    out.push_back(info.ref);
                }
            }
        }
        return out;
    }
    const CommentTree tree = buildComments(doc);
    std::set<std::pair<int, int>> seen;
    for (const AnnotRef& ref : refs) {
        std::vector<AnnotRef> branch = tree.branch(ref);
        if (branch.empty()) {
            branch.push_back(ref);  // не комментарий (ссылка…) — сама
        }
        for (const AnnotRef& r : branch) {
            if (r.valid() && seen.insert({r.page, r.objnum}).second) {
                out.push_back(r);
            }
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

int findByName(PageAnnots& pa, const std::string& nm) {
    if (nm.empty() || pa.annots() == nullptr) {
        return 0;
    }
    fz_context* ctx = pa.ctx();
    pdf_obj* annots = pa.annots();
    int found = 0;
    mu::call(ctx, [&] {
        const int n = pdf_array_len(ctx, annots);
        for (int i = 0; i < n && found == 0; ++i) {
            pdf_obj* obj = pdf_array_get(ctx, annots, i);
            const char* name = pdf_dict_get_text_string(ctx, obj, PDF_NAME(NM));
            if (name != nullptr && nm == name &&
                !pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Popup))) {
                found = pdf_to_num(ctx, obj);
            }
        }
    });
    return found;
}

bool placeImported(PageAnnots& pa, pdf_obj* ref, const std::string& nm) {
    const int existing = findByName(pa, nm);
    pdf_obj* annots = pa.ensureAnnots();
    fz_context* ctx = pa.ctx();
    pdf_page* page = pa.page();
    mu::call(ctx, [&] {
        pdf_dict_put(ctx, ref, PDF_NAME(P), page->obj);
        // Та же /NM: новая — на место прежней; окно прежней уходит.
        const int n = existing == 0 ? 0 : pdf_array_len(ctx, annots);
        int at = -1;
        int oldPopup = 0;
        for (int i = 0; i < n; ++i) {
            pdf_obj* obj = pdf_array_get(ctx, annots, i);
            if (pdf_to_num(ctx, obj) == existing) {
                at = i;
                oldPopup = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Popup)));
            }
        }
        if (at >= 0) {
            pdf_array_put(ctx, annots, at, ref);
        } else {
            pdf_array_push(ctx, annots, ref);
        }
        if (oldPopup > 0) {
            for (int i = pdf_array_len(ctx, annots) - 1; i >= 0; --i) {
                if (pdf_to_num(ctx, pdf_array_get(ctx, annots, i)) == oldPopup) {
                    pdf_array_delete(ctx, annots, i);
                }
            }
        }
    });
    return existing != 0;
}

void attachPopup(PageAnnots& pa, pdf_obj* parent, pdf_obj* popup) {
    pdf_obj* annots = pa.ensureAnnots();
    fz_context* ctx = pa.ctx();
    pdf_page* page = pa.page();
    mu::call(ctx, [&] {
        pdf_dict_put(ctx, popup, PDF_NAME(Type), PDF_NAME(Annot));
        pdf_dict_put(ctx, popup, PDF_NAME(Subtype), PDF_NAME(Popup));
        pdf_dict_put(ctx, popup, PDF_NAME(Parent), parent);
        pdf_dict_put(ctx, popup, PDF_NAME(P), page->obj);
        pdf_dict_put(ctx, parent, PDF_NAME(Popup), popup);
        pdf_array_push(ctx, annots, popup);
    });
}

void finishImported(Document& doc, const std::map<int, std::vector<int>>& byPage) {
    for (const auto& [index, objnums] : byPage) {
        PageAnnots pa(doc, index);
        pa.refresh(true);
        fz_context* ctx = pa.ctx();
        for (const int objnum : objnums) {
            pdf_obj* obj = pa.object(objnum);
            if (obj == nullptr || pa.find(objnum) == nullptr) {
                continue;
            }
            int hasAp = 0;
            char subtype[64] = {};
            mu::call(ctx, [&] {
                hasAp = pdf_dict_getp(ctx, obj, "AP/N") != nullptr ? 1 : 0;
                const char* name = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
                strncpy_s(subtype, sizeof(subtype), name != nullptr ? name : "", _TRUNCATE);
            });
            if (hasAp == 0 && synthesizable(subtype)) {
                updateAppearance(doc, pa, objnum, true);
                completeBase14Widths(ctx, pa.pdf(), obj);
            }
        }
    }
}

}  // namespace detail

}  // namespace ot::annot
