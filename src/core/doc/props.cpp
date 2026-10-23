#include "core/doc/props.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <ctime>
#include <set>
#include <stdexcept>
#include <string_view>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/xmp.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

namespace ot::props {

namespace {

struct StandardKey {
    const char* key;
    std::string InfoFields::* field;
};

constexpr StandardKey kStandard[] = {
    {"Title", &InfoFields::title},
    {"Author", &InfoFields::author},
    {"Subject", &InfoFields::subject},
    {"Keywords", &InfoFields::keywords},
    {"Creator", &InfoFields::creator},
    {"Producer", &InfoFields::producer},
    {"CreationDate", &InfoFields::creationDate},
    {"ModDate", &InfoFields::modDate},
};

bool isStandard(std::string_view key) {
    for (const StandardKey& s : kStandard) {
        if (key == s.key) {
            return true;
        }
    }
    return key == "Trapped";
}

pdf_document* requirePdf(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        throw std::logic_error("document properties: not a PDF document");
    }
    return pdf;
}

std::string nowPdfDate(fz_context* ctx) {
    char buf[64] = {};
    const std::int64_t now = std::time(nullptr);
    char* s = nullptr;
    mu::call(ctx, [&] { s = pdf_format_date(ctx, now, buf, sizeof(buf)); });
    return s != nullptr ? std::string(s) : std::string();
}

std::string bufferString(fz_context* ctx, fz_buffer* buf) {
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

// Синхронизировать XMP с /Info (fields — уже записанные значения).
void syncXmp(Document& doc, const InfoFields& f, const std::map<std::string, std::string>& oldCustom) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    const std::string existing = xmpPacket(doc);
    xmp::Packet packet = xmp::Packet::create();
    if (!existing.empty()) {
        try {
            packet = xmp::Packet::parse(existing);
        } catch (const std::exception& e) {
            // Чужой XMP, который не разобрать, не портим: /Info записан, метаданные — как были.
            spdlog::warn("props: XMP is not well-formed ({}), left as is", e.what());
            return;
        }
    }
    auto simple = [&](std::string_view ns, std::string_view local, const std::string& value) {
        if (value.empty()) {
            packet.remove(ns, local);
        } else {
            packet.setSimple(ns, local, value);
        }
    };
    if (f.title.empty()) {
        packet.remove(xmp::kDc, "title");
    } else {
        packet.setAlt(xmp::kDc, "title", f.title);
    }
    if (f.author.empty()) {
        packet.remove(xmp::kDc, "creator");
    } else {
        packet.setSeq(xmp::kDc, "creator", {f.author});
    }
    if (f.subject.empty()) {
        packet.remove(xmp::kDc, "description");
    } else {
        packet.setAlt(xmp::kDc, "description", f.subject);
    }
    simple(xmp::kPdf, "Keywords", f.keywords);
    simple(xmp::kXmp, "CreatorTool", f.creator);
    simple(xmp::kPdf, "Producer", f.producer);
    const std::string created = xmp::pdfDateToIso(f.creationDate);
    if (!created.empty()) {
        packet.setSimple(xmp::kXmp, "CreateDate", created);
    }
    const std::string modified = xmp::pdfDateToIso(f.modDate);
    if (!modified.empty()) {
        packet.setSimple(xmp::kXmp, "ModifyDate", modified);
        packet.setSimple(xmp::kXmp, "MetadataDate", modified);
    }
    for (const auto& [key, value] : oldCustom) {
        if (f.custom.count(key) == 0 && xmp::isXmlName(key)) {
            packet.remove(xmp::kPdfx, key);
        }
    }
    for (const auto& [key, value] : f.custom) {
        if (xmp::isXmlName(key)) {
            simple(xmp::kPdfx, key, value);
        }
    }
    const std::string out = packet.serialize();
    const auto* data = reinterpret_cast<const unsigned char*>(out.data());
    const size_t size = out.size();
    mu::BufferHandle buf(ctx);
    mu::PdfObjHandle dict(ctx);
    mu::PdfObjHandle ref(ctx);
    mu::call(ctx, [&] {
        buf.reset(fz_new_buffer_from_copied_data(ctx, data, size));
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* meta = pdf_dict_get(ctx, root, PDF_NAME(Metadata));
        if (pdf_is_stream(ctx, meta)) {
            pdf_update_stream(ctx, pdf, meta, buf, 0);  // XMP — без фильтра (PDF/A)
        } else {
            dict.reset(pdf_new_dict(ctx, pdf, 2));
            pdf_dict_put(ctx, dict, PDF_NAME(Type), PDF_NAME(Metadata));
            pdf_dict_put(ctx, dict, PDF_NAME(Subtype), PDF_NAME(XML));
            ref.reset(pdf_add_stream(ctx, pdf, buf, dict, 0));
            pdf_dict_put(ctx, root, PDF_NAME(Metadata), ref);
        }
    });
}

// ---------------------------------------------------------------- шрифты

struct FontScan {
    fz_context* ctx = nullptr;
    std::vector<FontInfo> list;
    std::map<std::string, size_t> index;  // ключ шрифта → строка списка
    std::set<size_t> onPage;              // уже посчитанные на текущей странице
    std::set<int> visited;                // ресурсы и формы текущей страницы (защита от петель)
    int page = 0;
};

FontInfo describeFont(fz_context* ctx, pdf_obj* font) {
    FontInfo info;
    const char* subtype = "";
    const char* base = "";
    const char* descSubtype = "";
    const char* encName = "";
    const char* baseEnc = "";
    int embedded = 0;
    int differences = 0;
    int encDict = 0;
    int encStream = 0;
    mu::call(ctx, [&] {
        subtype = pdf_to_name(ctx, pdf_dict_get(ctx, font, PDF_NAME(Subtype)));
        base = pdf_to_name(ctx, pdf_dict_get(ctx, font, PDF_NAME(BaseFont)));
        if (*base == '\0') {
            base = pdf_to_name(ctx, pdf_dict_get(ctx, font, PDF_NAME(Name)));
        }
        pdf_obj* descriptor = pdf_dict_get(ctx, font, PDF_NAME(FontDescriptor));
        if (pdf_name_eq(ctx, pdf_dict_get(ctx, font, PDF_NAME(Subtype)), PDF_NAME(Type0))) {
            pdf_obj* desc = pdf_array_get(ctx, pdf_dict_get(ctx, font, PDF_NAME(DescendantFonts)), 0);
            descSubtype = pdf_to_name(ctx, pdf_dict_get(ctx, desc, PDF_NAME(Subtype)));
            descriptor = pdf_dict_get(ctx, desc, PDF_NAME(FontDescriptor));
        }
        embedded = pdf_dict_get(ctx, descriptor, PDF_NAME(FontFile)) != nullptr ||
                   pdf_dict_get(ctx, descriptor, PDF_NAME(FontFile2)) != nullptr ||
                   pdf_dict_get(ctx, descriptor, PDF_NAME(FontFile3)) != nullptr ||
                   pdf_name_eq(ctx, pdf_dict_get(ctx, font, PDF_NAME(Subtype)), PDF_NAME(Type3));
        pdf_obj* enc = pdf_dict_get(ctx, font, PDF_NAME(Encoding));
        if (pdf_is_name(ctx, enc)) {
            encName = pdf_to_name(ctx, enc);
        } else if (pdf_is_stream(ctx, enc)) {
            encStream = 1;
        } else if (pdf_is_dict(ctx, enc)) {
            encDict = 1;
            baseEnc = pdf_to_name(ctx, pdf_dict_get(ctx, enc, PDF_NAME(BaseEncoding)));
            differences = pdf_dict_get(ctx, enc, PDF_NAME(Differences)) != nullptr;
        }
    });
    info.name = base;
    info.type = subtype;
    if (*descSubtype != '\0') {
        info.type += std::string(" (") + descSubtype + ")";
    }
    info.embedded = embedded != 0;
    info.subset =
        info.name.size() > 7 && info.name[6] == '+' &&
        std::all_of(info.name.begin(), info.name.begin() + 6, [](char c) { return c >= 'A' && c <= 'Z'; });
    if (*encName != '\0') {
        info.encoding = encName;
    } else if (encStream != 0) {
        info.encoding = "Custom CMap";
    } else if (encDict != 0) {
        info.encoding = *baseEnc != '\0' ? std::string(baseEnc) : std::string("Built-in");
        if (differences != 0) {
            info.encoding += " + Differences";
        }
    } else {
        info.encoding = "Built-in";
    }
    return info;
}

void scanResources(FontScan& scan, pdf_obj* res, int depth);

// Форма XObject: её ресурсы (один раз за страницу).
void scanForm(FontScan& scan, pdf_obj* xobj, int depth) {
    fz_context* ctx = scan.ctx;
    pdf_obj* res = nullptr;
    int form = 0;
    int num = 0;
    mu::call(ctx, [&] {
        form = pdf_is_stream(ctx, xobj) &&
               pdf_name_eq(ctx, pdf_dict_get(ctx, xobj, PDF_NAME(Subtype)), PDF_NAME(Form));
        num = pdf_to_num(ctx, xobj);
        res = pdf_dict_get(ctx, xobj, PDF_NAME(Resources));
    });
    if (form != 0 && (num == 0 || scan.visited.insert(num).second)) {
        scanResources(scan, res, depth + 1);
    }
}

void scanXObjects(FontScan& scan, pdf_obj* dict, int depth) {
    fz_context* ctx = scan.ctx;
    int n = 0;
    mu::call(ctx, [&] { n = pdf_dict_len(ctx, dict); });
    for (int k = 0; k < n; ++k) {
        pdf_obj* xobj = nullptr;
        mu::call(ctx, [&] { xobj = pdf_dict_get_val(ctx, dict, k); });
        scanForm(scan, xobj, depth);
    }
}

void scanResources(FontScan& scan, pdf_obj* res, int depth) {
    if (res == nullptr || depth > 32) {
        return;
    }
    fz_context* ctx = scan.ctx;
    pdf_obj* fonts = nullptr;
    pdf_obj* xobjects = nullptr;
    int n = 0;
    mu::call(ctx, [&] {
        fonts = pdf_dict_get(ctx, res, PDF_NAME(Font));
        xobjects = pdf_dict_get(ctx, res, PDF_NAME(XObject));
        n = pdf_dict_len(ctx, fonts);
    });
    for (int k = 0; k < n; ++k) {
        pdf_obj* font = nullptr;
        int num = 0;
        mu::call(ctx, [&] {
            font = pdf_dict_get_val(ctx, fonts, k);
            num = pdf_to_num(ctx, font);
        });
        if (font == nullptr) {
            continue;
        }
        FontInfo info;
        std::string key;
        if (num > 0) {
            key = "#" + std::to_string(num);
        } else {
            info = describeFont(ctx, font);
            key = info.name + "|" + info.type;
        }
        auto it = scan.index.find(key);
        if (it == scan.index.end()) {
            if (num > 0) {
                info = describeFont(ctx, font);
            }
            info.firstPage = scan.page;
            scan.list.push_back(info);
            it = scan.index.emplace(key, scan.list.size() - 1).first;
        }
        if (scan.onPage.insert(it->second).second) {
            ++scan.list[it->second].pages;
        }
    }
    if (xobjects != nullptr) {
        scanXObjects(scan, xobjects, depth);
    }
}

// Ресурсы внешнего вида аннотаций страницы (/AP /N — поток или словарь состояний).
void scanAnnotations(FontScan& scan, pdf_obj* page) {
    fz_context* ctx = scan.ctx;
    pdf_obj* annots = nullptr;
    int n = 0;
    mu::call(ctx, [&] {
        annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
        n = pdf_array_len(ctx, annots);
    });
    for (int k = 0; k < n; ++k) {
        pdf_obj* normal = nullptr;
        mu::call(ctx, [&] { normal = pdf_dict_getp(ctx, pdf_array_get(ctx, annots, k), "AP/N"); });
        if (normal == nullptr) {
            continue;
        }
        int isStream = 0;
        mu::call(ctx, [&] { isStream = pdf_is_stream(ctx, normal); });
        if (isStream != 0) {
            scanForm(scan, normal, 0);
        } else {
            scanXObjects(scan, normal, 0);  // состояния: каждое — форма
        }
    }
}

// Сборщик предупреждений MuPDF на время pdf_check_document (колбэк не бросает).
struct WarningSink {
    std::vector<std::string>* out = nullptr;
};

void collectMessage(void* user, const char* message) {
    auto* sink = static_cast<WarningSink*>(user);
    try {
        sink->out->emplace_back(message != nullptr ? message : "");
    } catch (...) {  // NOLINT(bugprone-empty-catch): колбэк MuPDF не должен бросать
    }
}

class CallbackSwap {
public:
    CallbackSwap(fz_context* ctx, WarningSink* sink) : ctx_(ctx) {
        warn_ = fz_warning_callback(ctx, &warnUser_);
        error_ = fz_error_callback(ctx, &errorUser_);
        fz_set_warning_callback(ctx, collectMessage, sink);
        fz_set_error_callback(ctx, collectMessage, sink);
    }
    ~CallbackSwap() {
        fz_set_warning_callback(ctx_, warn_, warnUser_);
        fz_set_error_callback(ctx_, error_, errorUser_);
    }
    CallbackSwap(const CallbackSwap&) = delete;
    CallbackSwap& operator=(const CallbackSwap&) = delete;

private:
    fz_context* ctx_;
    fz_warning_cb* warn_ = nullptr;
    void* warnUser_ = nullptr;
    fz_error_cb* error_ = nullptr;
    void* errorUser_ = nullptr;
};

}  // namespace

InfoFields readInfo(Document& doc) {
    InfoFields f;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return f;
    }
    fz_context* ctx = doc.context().get();
    pdf_obj* info = nullptr;
    int n = 0;
    mu::call(ctx, [&] {
        info = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Info));
        n = pdf_is_dict(ctx, info) ? pdf_dict_len(ctx, info) : 0;
    });
    for (int k = 0; k < n; ++k) {
        const char* key = "";
        const char* value = "";
        int isString = 0;
        mu::call(ctx, [&] {
            key = pdf_to_name(ctx, pdf_dict_get_key(ctx, info, k));
            pdf_obj* v = pdf_dict_get_val(ctx, info, k);
            isString = pdf_is_string(ctx, v);
            value = pdf_to_text_string(ctx, v);
        });
        if (isString == 0) {
            continue;
        }
        bool standard = false;
        for (const StandardKey& s : kStandard) {
            if (std::string_view(key) == s.key) {
                f.*(s.field) = value;
                standard = true;
            }
        }
        if (!standard && !isStandard(key)) {
            f.custom[key] = value;
        }
    }
    return f;
}

void writeInfo(Document& doc, const InfoFields& in, const WriteOptions& options) {
    pdf_document* pdf = requirePdf(doc);
    fz_context* ctx = doc.context().get();
    InfoFields f = in;
    if (options.touchModDate) {
        f.modDate = nowPdfDate(ctx);
    }
    const std::map<std::string, std::string> oldCustom = readInfo(doc).custom;
    Journal::Operation op(doc, "Document properties");
    pdf_obj* info = nullptr;
    mu::PdfObjHandle created(ctx);
    mu::call(ctx, [&] {
        info = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Info));
        if (!pdf_is_dict(ctx, info)) {
            // Трейлер в журнал не входит: после undo ссылка повиснет — Saver её уберёт.
            created.reset(pdf_add_new_dict(ctx, pdf, 8));
            pdf_dict_put(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Info), created);
            info = created;
        }
    });
    for (const StandardKey& s : kStandard) {
        const std::string& value = f.*(s.field);
        const char* key = s.key;
        const char* text = value.c_str();
        const int erase = value.empty() ? 1 : 0;
        mu::call(ctx, [&] {
            if (erase != 0) {
                pdf_dict_dels(ctx, info, key);
            } else {
                pdf_dict_puts_drop(ctx, info, key, pdf_new_text_string(ctx, text));
            }
        });
    }
    for (const auto& [key, value] : oldCustom) {
        if (f.custom.count(key) == 0) {
            const char* k = key.c_str();
            mu::call(ctx, [&] { pdf_dict_dels(ctx, info, k); });
        }
    }
    for (const auto& [key, value] : f.custom) {
        if (key.empty() || isStandard(key)) {
            continue;
        }
        const char* k = key.c_str();
        const char* text = value.c_str();
        mu::call(ctx, [&] { pdf_dict_puts_drop(ctx, info, k, pdf_new_text_string(ctx, text)); });
    }
    syncXmp(doc, readInfo(doc), oldCustom);
    op.commit();
}

std::string xmpPacket(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    mu::BufferHandle buf(ctx);
    mu::call(ctx, [&] {
        pdf_obj* meta = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/Metadata");
        if (pdf_is_stream(ctx, meta)) {
            buf.reset(pdf_load_stream(ctx, meta));
        }
    });
    return buf ? bufferString(ctx, buf) : std::string();
}

std::string lang(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    const char* value = "";
    mu::call(ctx, [&] {
        value = pdf_to_text_string(ctx, pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/Lang"));
    });
    return value;
}

void setLang(Document& doc, const std::string& value) {
    pdf_document* pdf = requirePdf(doc);
    fz_context* ctx = doc.context().get();
    Journal::Operation op(doc, "Document language");
    const char* text = value.c_str();
    const int erase = value.empty() ? 1 : 0;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        if (erase != 0) {
            pdf_dict_del(ctx, root, PDF_NAME(Lang));
        } else {
            pdf_dict_put_text_string(ctx, root, PDF_NAME(Lang), text);
        }
    });
    op.commit();
}

bool tagged(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return false;
    }
    fz_context* ctx = doc.context().get();
    int marked = 0;
    mu::call(ctx, [&] {
        marked = pdf_to_bool(ctx, pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/MarkInfo/Marked"));
    });
    return marked != 0;
}

std::vector<FontInfo> fonts(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    FontScan scan;
    scan.ctx = doc.context().get();
    fz_context* ctx = scan.ctx;
    const int count = doc.pageCount();
    for (int p = 0; p < count; ++p) {
        scan.page = p;
        scan.onPage.clear();
        scan.visited.clear();
        pdf_obj* page = nullptr;
        pdf_obj* res = nullptr;
        mu::call(ctx, [&] {
            page = pdf_lookup_page_obj(ctx, pdf, p);
            res = pdf_dict_get_inheritable(ctx, page, PDF_NAME(Resources));
        });
        scanResources(scan, res, 0);
        scanAnnotations(scan, page);
    }
    return std::move(scan.list);
}

const std::vector<FontInfo>& FontCache::fonts(Document& doc, std::uint64_t revision) {
    if (!valid_ || doc_ != &doc || revision_ != revision) {
        list_ = props::fonts(doc);
        doc_ = &doc;
        revision_ = revision;
        valid_ = true;
        ++scans_;
    }
    return list_;
}

Advanced advanced(Document& doc, bool check) {
    Advanced a;
    a.pages = doc.pageCount();
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return a;
    }
    fz_context* ctx = doc.context().get();
    if (check) {
        WarningSink sink{&a.warnings};
        const CallbackSwap swap(ctx, &sink);
        try {
            mu::call(ctx, [&] {
                (void)pdf_check_document(ctx, pdf);
                fz_flush_warnings(ctx);
            });
        } catch (const MuError& e) {
            a.warnings.emplace_back(e.what());
        }
    }
    int version = 0;
    int linearized = 0;
    int objects = 0;
    mu::call(ctx, [&] {
        version = pdf_version(ctx, pdf);
        linearized = pdf_doc_was_linearized(ctx, pdf);
        const int len = pdf_xref_len(ctx, pdf);
        for (int num = 1; num < len; ++num) {
            objects += pdf_object_exists(ctx, pdf, num);
        }
    });
    a.pdfVersion = version;
    a.linearized = linearized != 0;
    a.objects = objects;
    a.fileSize = pdf->file_size > 0 ? static_cast<std::uintmax_t>(pdf->file_size) : 0;
    a.tagged = tagged(doc);
    a.repaired = doc.repaired();
    a.lang = lang(doc);
    a.pdfa = doc.properties().pdfa;
    return a;
}

}  // namespace ot::props
