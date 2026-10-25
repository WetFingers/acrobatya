// Штампы: подстановки, постановка из страницы PDF и картинки, поворот (см. stamps.h).
#include "core/annot/stamps.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <utility>

#include "core/annot/annot_internal.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/system_fonts.h"
#include "core/mu/call.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"

namespace ot::annot {

std::string expandStampText(std::string_view text, const StampValues& values) {
    std::string out;
    std::size_t i = 0;
    bool skipSeparator = false;
    while (i < text.size()) {
        if (skipSeparator && text.substr(i, 2) == ", ") {
            i += 2;
            skipSeparator = false;
            continue;
        }
        skipSeparator = false;
        if (text[i] == '{') {
            const std::size_t close = text.find('}', i);
            if (close != std::string_view::npos) {
                const std::string_view key = text.substr(i + 1, close - i - 1);
                const std::string* value = nullptr;
                if (key == "name") {
                    value = &values.name;
                } else if (key == "title") {
                    value = &values.title;
                } else if (key == "org") {
                    value = &values.org;
                } else if (key == "date") {
                    value = &values.date;
                } else if (key == "time") {
                    value = &values.time;
                }
                if (value != nullptr) {
                    if (value->empty()) {
                        // Пустое значение — без разделителя «, » рядом.
                        if (out.size() >= 2 && out.compare(out.size() - 2, 2, ", ") == 0) {
                            out.resize(out.size() - 2);
                        } else {
                            skipSeparator = true;
                        }
                    } else {
                        out += *value;
                    }
                    i = close + 1;
                    continue;
                }
            }
        }
        out.push_back(text[i]);
        ++i;
    }
    const std::size_t first = out.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = out.find_last_not_of(' ');
    return out.substr(first, last - first + 1);
}

namespace {

// Документ PDF из байтов в контексте ctx: буфер, поток и документ — в ручках вызывающего.
struct OpenedPdf {
    explicit OpenedPdf(fz_context* ctx) : buffer(ctx), stream(ctx), doc(ctx) {}
    mu::BufferHandle buffer;
    mu::StreamHandle stream;
    mu::DocumentHandle doc;
};

void openPdf(fz_context* ctx, const std::string& bytes, OpenedPdf& out) {
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data());
    const std::size_t size = bytes.size();
    mu::call(ctx, [&] {
        out.buffer.reset(fz_new_buffer_from_copied_data(ctx, data, size));
        out.stream.reset(fz_open_buffer(ctx, out.buffer));
        out.doc.reset(fz_open_document_with_stream(ctx, "application/pdf", out.stream));
    });
}

// Документ pdf — байтами (сжатые потоки, без мусора).
std::string writePdf(fz_context* ctx, pdf_document* pdf) {
    mu::BufferHandle buf(ctx);
    mu::OutputHandle out(ctx);
    const unsigned char* data = nullptr;
    std::size_t len = 0;
    mu::call(ctx, [&] {
        buf.reset(fz_new_buffer(ctx, 4096));
        out.reset(fz_new_output_with_buffer(ctx, buf));
        pdf_write_options opts = pdf_default_write_options;
        opts.do_compress = 1;
        opts.do_compress_images = 1;
        opts.do_garbage = 2;
        pdf_write_document(ctx, pdf, out, &opts);
        fz_close_output(ctx, out);
        unsigned char* storage = nullptr;
        len = fz_buffer_storage(ctx, buf, &storage);
        data = storage;
    });
    return std::string(reinterpret_cast<const char*>(data), len);
}

// Шрифт строк динамического штампа: Segoe UI (%WINDIR%\Fonts), нет — Helvetica MuPDF (с запасными шрифтами).
void lineFont(fz_context* ctx, mu::FontHandle& font) {
    const std::filesystem::path p = edit::FakeSystemFonts::defaultDir() / "segoeui.ttf";
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) {
        const std::string path = paths::toUtf8(p);
        const char* file = path.c_str();
        try {
            mu::call(ctx, [&] { font.reset(fz_new_font_from_file(ctx, nullptr, file, 0, 0)); });
            return;
        } catch (const MuError&) {  // NOLINT(bugprone-empty-catch): не прочитался — Helvetica
        }
    }
    mu::call(ctx, [&] { font.reset(fz_new_base14_font(ctx, "Helvetica")); });
}

struct LineGlyph {
    fz_font* font = nullptr;
    int gid = 0;
    float adv = 0;
};

// Глифы строки (коды и ширины — задачей MuPDF в готовый массив).
std::vector<LineGlyph> shapeLine(fz_context* ctx, fz_font* font, const std::u32string& text) {
    std::vector<LineGlyph> glyphs(text.size());
    LineGlyph* g = glyphs.data();
    const char32_t* chars = text.data();
    const std::size_t count = text.size();
    mu::call(ctx, [&] {
        for (std::size_t i = 0; i < count; ++i) {
            fz_font* used = nullptr;
            g[i].gid = fz_encode_character_with_fallback(ctx, font, static_cast<int>(chars[i]), 0,
                                                         FZ_LANG_UNSET, &used);
            g[i].font = used;
            g[i].adv = fz_advance_glyph(ctx, used, g[i].gid, 0);
        }
    });
    return glyphs;
}

// Строка контурами глифов в устройство (внутри mu::call).
void drawLine(fz_context* ctx, fz_device* dev, const LineGlyph* glyphs, std::size_t count,
              const StampLine& line) {
    float width = 0;
    for (std::size_t i = 0; i < count; ++i) {
        width += glyphs[i].adv * line.size;
    }
    float x = line.x;
    if (line.align == 1) {
        x -= width / 2;
    } else if (line.align == 2) {
        x -= width;
    }
    const float rgb[3] = {static_cast<float>((line.color >> 16) & 0xFF) / 255.0f,
                          static_cast<float>((line.color >> 8) & 0xFF) / 255.0f,
                          static_cast<float>(line.color & 0xFF) / 255.0f};
    for (std::size_t i = 0; i < count; ++i) {
        const fz_matrix trm = fz_make_matrix(line.size, 0, 0, -line.size, x, line.y);
        if (fz_path* path = fz_outline_glyph(ctx, glyphs[i].font, glyphs[i].gid, trm)) {
            fz_fill_path(ctx, dev, path, 0, fz_identity, fz_device_rgb(ctx), rgb, 1.0f,
                         fz_default_color_params);
            fz_drop_path(ctx, path);
        }
        x += glyphs[i].adv * line.size;
    }
}

// Общие поля новой аннотации-штампа (внутри mu::call).
void putCommon(fz_context* ctx, pdf_annot* a, const AnnotProps& p, const char* nm, std::int64_t created,
               const char* name) {
    pdf_obj* obj = pdf_annot_obj(ctx, a);
    pdf_dict_put_name(ctx, obj, PDF_NAME(Name), name);
    pdf_set_annot_opacity(ctx, a, p.opacity);
    pdf_set_annot_contents(ctx, a, p.contents.c_str());
    if (!p.author.empty()) {
        pdf_dict_put_text_string(ctx, obj, PDF_NAME(T), p.author.c_str());
    }
    pdf_dict_put_text_string(ctx, obj, PDF_NAME(Subj), p.subject.empty() ? "Stamp" : p.subject.c_str());
    pdf_dict_put_text_string(ctx, obj, PDF_NAME(NM), nm);
    pdf_set_annot_creation_date(ctx, a, created);
    pdf_set_annot_modification_date(ctx, a, created);
    pdf_set_annot_flags(ctx, a, p.flags);
}

void checkTarget(Document& doc, int page, const AnnotProps& props) {
    detail::pdfOf(doc);
    if (page < 0 || page >= doc.pageCount()) {
        detail::badArgument("page " + std::to_string(page) + " is out of range");
    }
    if (props.rect.empty()) {
        detail::badArgument("stamp rectangle is empty");
    }
}

// Картинка штампа и её /SMask без фильтра (pdf_add_image кладёт распакованный PNG как есть) — сжать
// FlateDecode: инкрементальная запись потоки не сжимает, а подпись Fill & Sign — сотни килобайт растра.
void compressStampImage(fz_context* ctx, pdf_document* pdf, pdf_obj* annot) {
    pdf_obj* image = nullptr;
    mu::call(ctx, [&] { image = pdf_dict_getp(ctx, annot, "AP/N/Resources/XObject/I"); });
    pdf_obj* mask = nullptr;
    mu::call(ctx, [&] { mask = pdf_dict_get(ctx, image, PDF_NAME(SMask)); });
    for (pdf_obj* stream : {image, mask}) {
        mu::BufferHandle raw(ctx);
        mu::BufferHandle packed(ctx);
        mu::call(ctx, [&] {
            if (pdf_is_stream(ctx, stream) && pdf_dict_get(ctx, stream, PDF_NAME(Filter)) == nullptr) {
                raw.reset(pdf_load_raw_stream(ctx, stream));
                std::size_t length = 0;
                unsigned char* data = fz_new_deflated_data_from_buffer(ctx, &length, raw, FZ_DEFLATE_DEFAULT);
                packed.reset(fz_new_buffer_from_data(ctx, data, length));
                pdf_update_stream(ctx, pdf, stream, packed, 1);
                pdf_dict_put(ctx, stream, PDF_NAME(Filter), PDF_NAME(FlateDecode));
                pdf_dict_del(ctx, stream, PDF_NAME(DecodeParms));
            }
        });
    }
}

// Картинка из байтов.
void loadImage(fz_context* ctx, const std::string& image, mu::BufferHandle& buf, mu::ImageHandle& img) {
    const auto* data = reinterpret_cast<const unsigned char*>(image.data());
    const std::size_t size = image.size();
    mu::call(ctx, [&] {
        buf.reset(fz_new_buffer_from_copied_data(ctx, data, size));
        img.reset(fz_new_image_from_buffer(ctx, buf));
    });
}

}  // namespace

StampSize stampPageSize(const std::string& pdf, int page) {
    fz_context* ctx = MuContext::forThisThread().get();
    StampSize size;
    OpenedPdf doc(ctx);
    openPdf(ctx, pdf, doc);
    mu::PageHandle p(ctx);
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.doc, page));
        const fz_rect b = fz_bound_page(ctx, p);
        size = {b.x1 - b.x0, b.y1 - b.y0};
    });
    return size;
}

AnnotRef createStamp(Document& doc, int page, const StampSource& source, const StampValues& values,
                     AnnotProps props) {
    checkTarget(doc, page, props);
    detail::PageAnnots pa(doc, page);
    fz_context* ctx = pa.ctx();
    // Строки шаблона — глифами шрифта строк.
    mu::FontHandle font(ctx);
    std::vector<std::vector<LineGlyph>> shaped;
    if (!source.lines.empty()) {
        lineFont(ctx, font);
        for (const StampLine& line : source.lines) {
            shaped.push_back(shapeLine(ctx, font, utf8::decode(expandStampText(line.text, values))));
        }
    }
    // Страница штампа и строки — display list.
    mu::DisplayListHandle list(ctx);
    {
        OpenedPdf stamp(ctx);
        openPdf(ctx, source.pdf, stamp);
        mu::PageHandle stampPage(ctx);
        mu::DeviceHandle dev(ctx);
        const int stampIndex = source.page;
        const std::size_t lineCount = shaped.size();
        const std::vector<LineGlyph>* glyphs = shaped.data();
        const StampLine* lines = source.lines.data();
        mu::call(ctx, [&] {
            stampPage.reset(fz_load_page(ctx, stamp.doc, stampIndex));
            list.reset(fz_new_display_list(ctx, fz_bound_page(ctx, stampPage)));
            dev.reset(fz_new_list_device(ctx, list));
            fz_run_page_contents(ctx, stampPage, dev, fz_identity, nullptr);
            for (std::size_t i = 0; i < lineCount; ++i) {
                drawLine(ctx, dev, glyphs[i].data(), glyphs[i].size(), lines[i]);
            }
            fz_close_device(ctx, dev);
        });
    }
    const std::string nm = props.nm.empty() ? newAnnotName() : props.nm;
    const std::int64_t created = props.created > 0 ? props.created : detail::now();
    const std::string name = source.name.empty() ? std::string("Stamp") : source.name;
    const fz_rect rect = fz_make_rect(props.rect.x0, props.rect.y0, props.rect.x1, props.rect.y1);
    Journal::Operation op(doc, "Add stamp");
    mu::PdfAnnotHandle annot(ctx);
    int objnum = 0;
    pdf_page* pdfPage = pa.page();
    const char* nmText = nm.c_str();
    const char* nameText = name.c_str();
    mu::call(ctx, [&] {
        annot.reset(pdf_create_annot(ctx, pdfPage, PDF_ANNOT_STAMP));
        objnum = pdf_to_num(ctx, pdf_annot_obj(ctx, annot));
        pdf_set_annot_rect(ctx, annot, rect);
        putCommon(ctx, annot, props, nmText, created, nameText);
        // Вид последним: он снимает просьбу о синтезе (нестандартное имя — MuPDF его не перерисует).
        pdf_set_annot_appearance_from_display_list(ctx, annot, "N", nullptr, fz_identity, list);
    });
    op.invalidate(page);
    op.commit();
    pa.refresh();
    return {page, objnum, nm};
}

StampSize imageStampSize(const std::string& image) {
    fz_context* ctx = MuContext::forThisThread().get();
    StampSize size;
    mu::BufferHandle buf(ctx);
    mu::ImageHandle img(ctx);
    loadImage(ctx, image, buf, img);
    mu::call(ctx, [&] {
        int xres = 0;
        int yres = 0;
        fz_image_resolution(img, &xres, &yres);
        const float dx = xres > 0 ? static_cast<float>(xres) : 96.0f;
        const float dy = yres > 0 ? static_cast<float>(yres) : 96.0f;
        fz_image* im = img;
        size = {static_cast<float>(im->w) * 72.0f / dx, static_cast<float>(im->h) * 72.0f / dy};
    });
    return size;
}

AnnotRef createImageStamp(Document& doc, int page, const std::string& image, AnnotProps props,
                          const std::string& name) {
    checkTarget(doc, page, props);
    detail::PageAnnots pa(doc, page);
    fz_context* ctx = pa.ctx();
    const std::string nm = props.nm.empty() ? newAnnotName() : props.nm;
    const std::int64_t created = props.created > 0 ? props.created : detail::now();
    const std::string stampName = name.empty() ? std::string("Image") : name;
    const fz_rect rect = fz_make_rect(props.rect.x0, props.rect.y0, props.rect.x1, props.rect.y1);
    mu::BufferHandle buf(ctx);
    mu::ImageHandle img(ctx);
    loadImage(ctx, image, buf, img);
    Journal::Operation op(doc, "Add stamp");
    mu::PdfAnnotHandle annot(ctx);
    int objnum = 0;
    pdf_page* pdfPage = pa.page();
    const char* nmText = nm.c_str();
    const char* nameText = stampName.c_str();
    mu::call(ctx, [&] {
        annot.reset(pdf_create_annot(ctx, pdfPage, PDF_ANNOT_STAMP));
        objnum = pdf_to_num(ctx, pdf_annot_obj(ctx, annot));
        pdf_set_annot_rect(ctx, annot, rect);
        pdf_set_annot_stamp_image(ctx, annot, img);
        putCommon(ctx, annot, props, nmText, created, nameText);
        pdf_update_annot(ctx, annot);
    });
    compressStampImage(ctx, pa.pdf(), pdf_annot_obj(ctx, annot));
    op.invalidate(page);
    op.commit();
    pa.refresh();
    return {page, objnum, nm};
}

std::string stampImage(Document& doc, const AnnotRef& ref) {
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_obj* obj = pa.object(ref.objnum);
    if (obj == nullptr) {
        detail::notFound(ref);
    }
    fz_context* ctx = pa.ctx();
    pdf_document* pdf = pa.pdf();
    mu::ImageHandle img(ctx);
    mu::PixmapHandle color(ctx);
    mu::PixmapHandle rgb(ctx);
    mu::PixmapHandle mask(ctx);
    mu::PixmapHandle out(ctx);
    mu::BufferHandle png(ctx);
    pdf_obj* image = nullptr;
    mu::call(ctx, [&] {
        if (pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Stamp))) {
            pdf_obj* xobjects = pdf_dict_getp(ctx, obj, "AP/N/Resources/XObject");
            image = pdf_dict_get(ctx, xobjects, PDF_NAME(I));
            for (int i = 0; image == nullptr && i < pdf_dict_len(ctx, xobjects); ++i) {
                pdf_obj* x = pdf_dict_get_val(ctx, xobjects, i);
                if (pdf_name_eq(ctx, pdf_dict_get(ctx, x, PDF_NAME(Subtype)), PDF_NAME(Image))) {
                    image = x;
                }
            }
            if (!pdf_is_stream(ctx, image)) {
                image = nullptr;
            }
        }
    });
    const bool found = image != nullptr;
    if (!found) {
        detail::badArgument("annotation is not an image stamp");
    }
    mu::call(ctx, [&] {
        img.reset(pdf_load_image(ctx, pdf, image));
        color.reset(fz_get_pixmap_from_image(ctx, img, nullptr, nullptr, nullptr, nullptr));
        rgb.reset(
            fz_convert_pixmap(ctx, color, fz_device_rgb(ctx), nullptr, nullptr, fz_default_color_params, 1));
        if (img->mask != nullptr) {
            mask.reset(fz_get_pixmap_from_image(ctx, img->mask, nullptr, nullptr, nullptr, nullptr));
        }
        const int w = rgb->w;
        const int h = rgb->h;
        out.reset(fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, nullptr, 1));
        // Альфа — из /SMask (ближайший отсчёт, если размер другой) или своя; данные — с предумножением, как
        // у пиксмапов MuPDF (запись PNG его снимает).
        for (int y = 0; y < h; ++y) {
            const unsigned char* src = rgb->samples + static_cast<std::ptrdiff_t>(y) * rgb->stride;
            unsigned char* dst = out->samples + static_cast<std::ptrdiff_t>(y) * out->stride;
            for (int x = 0; x < w; ++x) {
                const unsigned char* p = src + static_cast<std::ptrdiff_t>(x) * rgb->n;
                int alpha = rgb->alpha != 0 ? p[3] : 255;
                if (mask != nullptr) {
                    const int mx = std::min(mask->w - 1, x * mask->w / std::max(1, w));
                    const int my = std::min(mask->h - 1, y * mask->h / std::max(1, h));
                    alpha = mask->samples[static_cast<std::ptrdiff_t>(my) * mask->stride +
                                          static_cast<std::ptrdiff_t>(mx) * mask->n];
                }
                const int un = rgb->alpha != 0 ? std::max(1, static_cast<int>(p[3])) : 255;
                for (int k = 0; k < 3; ++k) {
                    const int c = rgb->alpha != 0 ? p[k] * 255 / un : p[k];  // без своего предумножения
                    dst[x * 4 + k] = static_cast<unsigned char>(c * alpha / 255);
                }
                dst[x * 4 + 3] = static_cast<unsigned char>(alpha);
            }
        }
        png.reset(fz_new_buffer_from_pixmap_as_png(ctx, out, fz_default_color_params));
    });
    unsigned char* data = nullptr;
    const std::size_t size = fz_buffer_storage(ctx, png, &data);
    return {reinterpret_cast<const char*>(data), size};
}

void setStampImage(Document& doc, const AnnotRef& ref, const std::string& image,
                   const std::string& operation) {
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_annot* a = pa.find(ref.objnum);
    if (a == nullptr) {
        detail::notFound(ref);
    }
    fz_context* ctx = pa.ctx();
    mu::BufferHandle buf(ctx);
    mu::ImageHandle img(ctx);
    loadImage(ctx, image, buf, img);
    bool stamp = false;
    mu::call(ctx, [&] { stamp = pdf_annot_type(ctx, a) == PDF_ANNOT_STAMP; });
    if (!stamp) {
        detail::badArgument("annotation is not a stamp");
    }
    Journal::Operation op(doc, operation);
    mu::call(ctx, [&] {
        const fz_rect rect = pdf_dict_get_rect(ctx, pdf_annot_obj(ctx, a), PDF_NAME(Rect));
        pdf_set_annot_stamp_image(ctx, a, img);
        pdf_dict_put_rect(ctx, pdf_annot_obj(ctx, a), PDF_NAME(Rect), rect);
        pdf_set_annot_modification_date(ctx, a, time(nullptr));
        pdf_update_annot(ctx, a);
    });
    compressStampImage(ctx, pa.pdf(), pdf_annot_obj(ctx, a));
    op.invalidate(ref.page);
    op.commit();
}

std::string imageStampPdf(const std::string& image) {
    const StampSize size = imageStampSize(image);
    fz_context* ctx = MuContext::forThisThread().get();
    mu::PdfDocumentHandle pdf(ctx);
    mu::BufferHandle buf(ctx);
    mu::ImageHandle img(ctx);
    mu::BufferHandle contents(ctx);
    mu::PdfObjHandle res(ctx);
    mu::PdfObjHandle pageObj(ctx);
    loadImage(ctx, image, buf, img);
    mu::call(ctx, [&] {
        pdf.reset(pdf_create_document(ctx));
        res.reset(pdf_new_dict(ctx, pdf, 1));
        pdf_obj* xobj = pdf_dict_put_dict(ctx, res, PDF_NAME(XObject), 1);
        pdf_dict_puts_drop(ctx, xobj, "Im0", pdf_add_image(ctx, pdf, img));
        contents.reset(fz_new_buffer(ctx, 64));
        fz_append_printf(ctx, contents, "q %g 0 0 %g 0 0 cm /Im0 Do Q\n", size.width, size.height);
        pageObj.reset(pdf_add_page(ctx, pdf, fz_make_rect(0, 0, size.width, size.height), 0, res, contents));
        pdf_insert_page(ctx, pdf, -1, pageObj);
    });
    return writePdf(ctx, pdf);
}

std::string pageStampPdf(const std::string& pdf, int page) {
    fz_context* ctx = MuContext::forThisThread().get();
    OpenedPdf src(ctx);
    openPdf(ctx, pdf, src);
    mu::PdfDocumentHandle dst(ctx);
    bool ok = false;
    mu::call(ctx, [&] {
        pdf_document* srcPdf = pdf_document_from_fz_document(ctx, src.doc);
        if (srcPdf != nullptr && page >= 0 && page < pdf_count_pages(ctx, srcPdf)) {
            dst.reset(pdf_create_document(ctx));
            pdf_graft_page(ctx, dst, -1, srcPdf, page);
            ok = true;
        }
    });
    if (!ok) {
        detail::badArgument("stamp page " + std::to_string(page) + " is not in the PDF");
    }
    return writePdf(ctx, dst);
}

void setStampRotation(Document& doc, const AnnotRef& ref, float degrees) {
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_obj* obj = pa.object(ref.objnum);
    if (obj == nullptr) {
        detail::notFound(ref);
    }
    fz_context* ctx = pa.ctx();
    bool ok = false;
    mu::call(ctx, [&] {
        ok = pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Stamp)) != 0 &&
             pdf_is_stream(ctx, pdf_dict_getp(ctx, obj, "AP/N"));
    });
    if (!ok) {
        detail::badArgument("annotation is not a stamp with an appearance");
    }
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0) {
        normalized += 360.0f;
    }
    const bool whole = std::fabs(normalized - std::round(normalized)) < 0.001f;
    Journal::Operation op(doc, "Rotate stamp");
    mu::call(ctx, [&] {
        pdf_obj* n = pdf_dict_getp(ctx, obj, "AP/N");
        const fz_rect bbox = pdf_dict_get_rect(ctx, n, PDF_NAME(BBox));
        const fz_matrix oldM = pdf_dict_get_matrix(ctx, n, PDF_NAME(Matrix));
        const fz_rect rect = pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect));
        const fz_rect oldBox = fz_transform_rect(bbox, oldM);
        const float cx = (bbox.x0 + bbox.x1) / 2;
        const float cy = (bbox.y0 + bbox.y1) / 2;
        fz_matrix m = fz_translate(-cx, -cy);
        m = fz_concat(m, fz_rotate(degrees));
        m = fz_concat(m, fz_translate(cx, cy));
        const fz_rect newBox = fz_transform_rect(bbox, m);
        // Масштаб рамки к виду (штамп мог быть растянут) сохраняется.
        const float sx = oldBox.x1 > oldBox.x0 ? (rect.x1 - rect.x0) / (oldBox.x1 - oldBox.x0) : 1.0f;
        const float sy = oldBox.y1 > oldBox.y0 ? (rect.y1 - rect.y0) / (oldBox.y1 - oldBox.y0) : 1.0f;
        const float s = std::sqrt(std::max(0.0001f, sx * sy));
        const float w = (newBox.x1 - newBox.x0) * s;
        const float h = (newBox.y1 - newBox.y0) * s;
        const float rx = (rect.x0 + rect.x1) / 2;
        const float ry = (rect.y0 + rect.y1) / 2;
        pdf_dict_put_matrix(ctx, n, PDF_NAME(Matrix), m);
        pdf_dict_put_rect(ctx, obj, PDF_NAME(Rect),
                          fz_make_rect(rx - w / 2, ry - h / 2, rx + w / 2, ry + h / 2));
        if (whole) {
            pdf_dict_put_int(ctx, obj, PDF_NAME(Rotate),
                             static_cast<std::int64_t>(std::round(normalized)) % 360);
        } else {
            pdf_dict_put_real(ctx, obj, PDF_NAME(Rotate), normalized);
        }
    });
    op.invalidate(ref.page);
    op.commit();
}

float stampRotation(Document& doc, const AnnotRef& ref) {
    detail::pdfOf(doc);
    if (!ref.valid() || ref.page >= doc.pageCount()) {
        detail::notFound(ref);
    }
    detail::PageAnnots pa(doc, ref.page);
    pdf_obj* obj = pa.object(ref.objnum);
    if (obj == nullptr) {
        detail::notFound(ref);
    }
    float r = 0;
    fz_context* ctx = pa.ctx();
    mu::call(ctx, [&] { r = pdf_dict_get_real(ctx, obj, PDF_NAME(Rotate)); });
    return r;
}

}  // namespace ot::annot
