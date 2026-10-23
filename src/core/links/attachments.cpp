#include "core/links/attachments.h"

#include <algorithm>
#include <array>
#include <cctype>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::attachments {

namespace {

// Эталон 7.3: открыть внешней программой нельзя — только «Сохранить как…».
constexpr std::array<std::string_view, 21> kBlocked = {
    "exe", "bat", "cmd", "com", "js",  "jse", "vbs", "vbe", "ps1", "msi", "msp",
    "scr", "pif", "reg", "hta", "jar", "lnk", "wsf", "wsh", "cpl", "inf",
};

// Спецификация файла, прочитанная внутри mu::call: строки — указатели в объекты документа.
struct RawSpec {
    const char* fileName = nullptr;
    const char* description = nullptr;
    const char* mimeType = nullptr;
    int size = -1;
    std::int64_t created = -1;
    std::int64_t modified = -1;
    int embedded = 0;
};

// Имя из /UF, /F, /Unix, /DOS (как у ссылок /GoToR).
const char* attachSpecName(fz_context* ctx, pdf_obj* fs) {
    if (pdf_is_string(ctx, fs)) {
        return pdf_to_text_string(ctx, fs);
    }
    for (pdf_obj* key : {PDF_NAME(UF), PDF_NAME(F), PDF_NAME(Unix), PDF_NAME(DOS)}) {
        pdf_obj* v = pdf_dict_get(ctx, fs, key);
        if (pdf_is_string(ctx, v)) {
            return pdf_to_text_string(ctx, v);
        }
    }
    return nullptr;
}

// Вызывать внутри mu::call.
void attachReadSpec(fz_context* ctx, pdf_obj* fs, RawSpec* out) {
    out->embedded = pdf_is_embedded_file(ctx, fs);
    out->fileName = attachSpecName(ctx, fs);
    pdf_obj* desc = pdf_dict_get(ctx, fs, PDF_NAME(Desc));
    out->description = pdf_is_string(ctx, desc) ? pdf_to_text_string(ctx, desc) : nullptr;
    if (out->embedded == 0) {
        return;
    }
    pdf_filespec_params params;
    pdf_get_filespec_params(ctx, fs, &params);
    if (params.filename != nullptr && *params.filename != 0) {
        out->fileName = params.filename;
    }
    pdf_obj* subtype = pdf_dict_getp(ctx, fs, "EF/F/Subtype");
    out->mimeType = pdf_is_name(ctx, subtype) ? pdf_to_name(ctx, subtype) : nullptr;
    pdf_obj* size = pdf_dict_getp(ctx, fs, "EF/F/Params/Size");
    if (size == nullptr) {
        size = pdf_dict_getp(ctx, fs, "EF/UF/Params/Size");
    }
    out->size = pdf_is_int(ctx, size) ? pdf_to_int(ctx, size) : -1;
    out->created = params.created;
    out->modified = params.modified;
}

std::string attachString(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

Attachment attachFromRaw(const RawSpec& raw) {
    Attachment a;
    a.fileName = attachString(raw.fileName);
    a.description = attachString(raw.description);
    a.mimeType = attachString(raw.mimeType);
    a.size = raw.size;
    a.created = raw.created;
    a.modified = raw.modified;
    a.embedded = raw.embedded != 0;
    return a;
}

char attachLower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// Без хвостовых точек и пробелов (Windows их отбрасывает: «a.exe. » открылось бы как a.exe).
std::string_view attachTrimTail(std::string_view s) {
    while (!s.empty() && (s.back() == '.' || s.back() == ' ')) {
        s.remove_suffix(1);
    }
    return s;
}

}  // namespace

std::vector<Attachment> list(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    std::vector<Attachment> out;

    // Дерево /Names /EmbeddedFiles — ключи по алфавиту.
    mu::PdfObjHandle tree(ctx);
    int n = 0;
    mu::call(ctx, [&] {
        tree.reset(pdf_load_name_tree(ctx, pdf, PDF_NAME(EmbeddedFiles)));
        n = pdf_dict_len(ctx, tree);
    });
    std::vector<Attachment> files;
    for (int i = 0; i < n; ++i) {
        RawSpec raw;
        const char* key = nullptr;
        mu::call(ctx, [&] {
            pdf_obj* k = pdf_dict_get_key(ctx, tree, i);
            key = pdf_is_name(ctx, k) ? pdf_to_name(ctx, k) : nullptr;
            attachReadSpec(ctx, pdf_dict_get_val(ctx, tree, i), &raw);
        });
        if (key == nullptr) {
            continue;
        }
        Attachment a = attachFromRaw(raw);
        a.source = Source::Document;
        a.key = key;
        if (a.fileName.empty()) {
            a.fileName = a.key;
        }
        files.push_back(std::move(a));
    }
    std::sort(files.begin(), files.end(),
              [](const Attachment& x, const Attachment& y) { return x.key < y.key; });
    out = std::move(files);

    // Аннотации /FileAttachment — по страницам (без загрузки страниц: только объекты).
    int pages = 0;
    mu::call(ctx, [&] { pages = pdf_count_pages(ctx, pdf); });
    for (int page = 0; page < pages; ++page) {
        pdf_obj* annots = nullptr;
        int count = 0;
        mu::call(ctx, [&] {
            annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Annots));
            count = pdf_array_len(ctx, annots);
        });
        for (int i = 0; i < count; ++i) {
            RawSpec raw;
            int isAttachment = 0;
            int objnum = 0;
            const char* contents = nullptr;
            mu::call(ctx, [&] {
                pdf_obj* annot = pdf_array_get(ctx, annots, i);
                isAttachment =
                    pdf_name_eq(ctx, pdf_dict_get(ctx, annot, PDF_NAME(Subtype)), PDF_NAME(FileAttachment)) &&
                    pdf_dict_get(ctx, annot, PDF_NAME(FS)) != nullptr;
                if (isAttachment != 0) {
                    objnum = pdf_to_num(ctx, annot);
                    attachReadSpec(ctx, pdf_dict_get(ctx, annot, PDF_NAME(FS)), &raw);
                    pdf_obj* c = pdf_dict_get(ctx, annot, PDF_NAME(Contents));
                    contents = pdf_is_string(ctx, c) ? pdf_to_text_string(ctx, c) : nullptr;
                }
            });
            if (isAttachment == 0) {
                continue;
            }
            Attachment a = attachFromRaw(raw);
            a.source = Source::Annotation;
            a.page = page;
            a.objnum = objnum;
            if (a.description.empty()) {
                a.description = attachString(contents);
            }
            out.push_back(std::move(a));
        }
    }
    return out;
}

std::string contents(Document& doc, const Attachment& attachment) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        throw MuError(FZ_ERROR_ARGUMENT, "attachments: not a PDF");
    }
    fz_context* ctx = doc.context().get();
    mu::PdfObjHandle tree(ctx);
    mu::BufferHandle buffer(ctx);
    int found = 0;
    mu::call(ctx, [&] {
        pdf_obj* fs = nullptr;
        if (attachment.source == Source::Document) {
            tree.reset(pdf_load_name_tree(ctx, pdf, PDF_NAME(EmbeddedFiles)));
            fs = pdf_dict_gets(ctx, tree, attachment.key.c_str());
        } else if (attachment.page >= 0 && attachment.page < pdf_count_pages(ctx, pdf)) {
            pdf_obj* annots =
                pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, attachment.page), PDF_NAME(Annots));
            const int count = pdf_array_len(ctx, annots);
            for (int i = 0; i < count && fs == nullptr; ++i) {
                pdf_obj* annot = pdf_array_get(ctx, annots, i);
                if (pdf_to_num(ctx, annot) == attachment.objnum) {
                    fs = pdf_dict_get(ctx, annot, PDF_NAME(FS));
                }
            }
        }
        if (fs != nullptr && pdf_is_embedded_file(ctx, fs)) {
            found = 1;
            buffer.reset(pdf_load_embedded_file_contents(ctx, fs));
        }
    });
    if (found == 0 || !buffer) {
        throw MuError(FZ_ERROR_ARGUMENT, "attachments: no embedded file " + attachment.fileName);
    }
    unsigned char* data = nullptr;
    const std::size_t len = fz_buffer_storage(ctx, buffer, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

std::string extensionOf(std::string_view fileName) {
    const std::string_view name = attachTrimTail(fileName);
    const std::size_t slash = name.find_last_of("/\\");
    const std::string_view base = slash == std::string_view::npos ? name : name.substr(slash + 1);
    const std::size_t dot = base.rfind('.');
    if (dot == std::string_view::npos || dot == 0) {
        return {};
    }
    std::string ext(base.substr(dot + 1));
    std::transform(ext.begin(), ext.end(), ext.begin(), attachLower);
    return ext;
}

bool isBlocked(std::string_view fileName) {
    const std::string ext = extensionOf(fileName);
    return !ext.empty() && std::find(kBlocked.begin(), kBlocked.end(), ext) != kBlocked.end();
}

bool isPdf(std::string_view fileName) { return extensionOf(fileName) == "pdf"; }

std::string safeFileName(std::string_view fileName) {
    const std::size_t slash = fileName.find_last_of("/\\");
    std::string_view base = slash == std::string_view::npos ? fileName : fileName.substr(slash + 1);
    std::string name;
    name.reserve(base.size());
    for (const char c : base) {
        const auto u = static_cast<unsigned char>(c);
        const bool reserved =
            u < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '|' || c == '?' || c == '*';
        name.push_back(reserved ? '_' : c);
    }
    name = std::string(attachTrimTail(name));
    const std::size_t lead = name.find_first_not_of(' ');
    name = lead == std::string::npos ? std::string() : name.substr(lead);
    if (name.empty()) {
        return "attachment";
    }
    // Имена устройств Windows (CON, PRN, AUX, NUL, COM1–9, LPT1–9) — с расширением или без.
    std::string stem = name.substr(0, name.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), attachLower);
    const bool device = stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
                        (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
                         stem[3] >= '1' && stem[3] <= '9');
    return device ? "_" + name : name;
}

}  // namespace ot::attachments
