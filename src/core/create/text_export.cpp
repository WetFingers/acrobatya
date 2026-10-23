#include "core/create/text_export.h"

#include <windows.h>

#include <algorithm>

#include "core/create/rtf_writer.h"
#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/text/stext.h"
#include "core/util/paths.h"

namespace ot {

namespace {

std::string crlf(std::string_view text) {
    std::string out;
    out.reserve(text.size() + text.size() / 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            out += '\r';
        }
        out += text[i];
    }
    return out;
}

void checkCancelled(const std::function<bool()>& cancelled) {
    if (cancelled && cancelled()) {
        throw MuError(FZ_ERROR_ABORT, "text export aborted");
    }
}

// HTML, XHTML, XML — печать слоёв функциями MuPDF в буфер.
std::string exportMarkup(const Document& doc, TextFormat format, int first, int last,
                         const TextOptions& layer, const std::function<bool()>& cancelled) {
    fz_context* ctx = doc.context().get();
    mu::BufferHandle buffer(ctx);
    mu::OutputHandle out(ctx);
    mu::call(ctx, [&] {
        buffer.reset(fz_new_buffer(ctx, 64 * 1024));
        out.reset(fz_new_output_with_buffer(ctx, buffer));
        if (format == TextFormat::Html) {
            fz_print_stext_header_as_html(ctx, out);
        } else if (format == TextFormat::Xhtml) {
            fz_print_stext_header_as_xhtml(ctx, out);
        } else {
            fz_write_string(ctx, out, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<document>\n");
        }
    });
    for (int p = first; p <= last; ++p) {
        checkCancelled(cancelled);
        const auto text = TextPage::build(doc, p, layer);
        fz_stext_page* page = text->stext();
        const int id = p + 1;
        mu::call(ctx, [&] {
            if (format == TextFormat::Html) {
                fz_print_stext_page_as_html(ctx, out, page, id);
            } else if (format == TextFormat::Xhtml) {
                fz_print_stext_page_as_xhtml(ctx, out, page, id);
            } else {
                fz_print_stext_page_as_xml(ctx, out, page, id);
            }
        });
    }
    mu::call(ctx, [&] {
        if (format == TextFormat::Html) {
            fz_print_stext_trailer_as_html(ctx, out);
        } else if (format == TextFormat::Xhtml) {
            fz_print_stext_trailer_as_xhtml(ctx, out);
        } else {
            fz_write_string(ctx, out, "</document>\n");
        }
        fz_close_output(ctx, out);
    });
    std::string result(reinterpret_cast<const char*>(buffer->data), buffer->len);
    if (format == TextFormat::Html) {
        // У заголовка MuPDF нет кодировки — браузер угадывал бы её по-своему.
        const std::string head = "<head>\n";
        if (const auto at = result.find(head); at != std::string::npos) {
            result.insert(at + head.size(), "<meta charset=\"utf-8\">\n");
        }
    }
    return result;
}

}  // namespace

std::string encodeText(std::string_view utf8, TextEncoding encoding, bool bom) {
    const std::string text = crlf(utf8);
    switch (encoding) {
        case TextEncoding::Utf8:
            return bom ? "\xEF\xBB\xBF" + text : text;
        case TextEncoding::Utf16: {
            const std::wstring wide = paths::widen(text);
            std::string out("\xFF\xFE", 2);
            out.append(reinterpret_cast<const char*>(wide.data()), wide.size() * sizeof(wchar_t));
            return out;
        }
        case TextEncoding::Cp1251: {
            const std::wstring wide = paths::widen(text);
            if (wide.empty()) {
                return {};
            }
            const int n = WideCharToMultiByte(1251, WC_NO_BEST_FIT_CHARS, wide.data(),
                                              static_cast<int>(wide.size()), nullptr, 0, "?", nullptr);
            std::string out(static_cast<std::size_t>(std::max(n, 0)), '\0');
            WideCharToMultiByte(1251, WC_NO_BEST_FIT_CHARS, wide.data(), static_cast<int>(wide.size()),
                                out.data(), n, "?", nullptr);
            return out;
        }
    }
    return text;
}

std::string exportText(const Document& doc, const TextExportOptions& options,
                       const std::function<bool()>& cancelled) {
    const int count = doc.pageCount();
    const int first = std::clamp(options.firstPage, 0, count);
    const int last = options.lastPage < 0 ? count - 1 : std::min(options.lastPage, count - 1);
    TextOptions layer;  // порядок чтения — SEGMENT и абзацы
    layer.images = options.format == TextFormat::Html || options.format == TextFormat::Xhtml;
    switch (options.format) {
        case TextFormat::Txt: {
            std::string utf8;
            for (int p = first; p <= last; ++p) {
                checkCancelled(cancelled);
                if (p > first) {
                    utf8 += '\f';
                }
                utf8 += TextPage::build(doc, p, layer)->text();
            }
            return encodeText(utf8, options.encoding, options.bom);
        }
        case TextFormat::Rtf: {
            RtfWriter writer;
            for (int p = first; p <= last; ++p) {
                checkCancelled(cancelled);
                writer.addPage(*TextPage::build(doc, p, layer));
            }
            return writer.finish();
        }
        case TextFormat::Html:
        case TextFormat::Xhtml:
        case TextFormat::Xml:
            return exportMarkup(doc, options.format, first, last, layer, cancelled);
    }
    return {};
}

}  // namespace ot
