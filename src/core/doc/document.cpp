#include "core/doc/document.h"

#include <cstring>
#include <cwctype>
#include <stdexcept>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/renderer.h"
#include "core/util/paths.h"

namespace ot {

namespace fs = std::filesystem;

namespace {

std::string magicFor(const fs::path& path) {
    std::wstring ext = path.extension().native();
    for (auto& ch : ext) {
        ch = static_cast<wchar_t>(std::towlower(ch));
    }
    if (ext.empty()) {
        return ".pdf";
    }
    return paths::narrow(ext);
}

DocError toDocError(const MuError& e) { return {e.kind(), e.code(), e.what()}; }

// Поля AcroForm: обход дерева /Fields с наследуемым /FT (pdf_walk_tree сам защищён от петель).
struct FieldScan {
    pdf_document* doc = nullptr;
    int fillable = 0;    // поля, кроме подписей
    int signedSigs = 0;  // поля подписи с /V
};

void scanField(fz_context* ctx, pdf_obj* field, void* arg, pdf_obj** inherited) {
    auto* scan = static_cast<FieldScan*>(arg);
    pdf_obj* ft = inherited[0];
    if (ft == nullptr) {
        return;
    }
    if (pdf_name_eq(ctx, ft, PDF_NAME(Sig))) {
        if (pdf_is_dict(ctx, pdf_dict_get_inheritable(ctx, field, PDF_NAME(V)))) {
            ++scan->signedSigs;
        }
    } else {
        ++scan->fillable;
    }
}

FieldScan scanFields(fz_context* ctx, pdf_document* pdf) {
    FieldScan scan;
    scan.doc = pdf;
    pdf_obj* names[2] = {PDF_NAME(FT), nullptr};
    pdf_obj* values[2] = {nullptr, nullptr};
    mu::call(ctx, [&] {
        pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
        if (pdf_is_array(ctx, fields)) {
            pdf_walk_tree(ctx, fields, PDF_NAME(Kids), scanField, nullptr, &scan, names, values);
        }
    });
    return scan;
}

// Значение свойства XMP pdfaid:* — атрибут (pdfaid:part="2") или элемент (<pdfaid:part>2</pdfaid:part>).
std::string xmpValue(const std::string& xmp, const std::string& key) {
    const std::string tag = "pdfaid:" + key;
    size_t at = xmp.find(tag);
    while (at != std::string::npos) {
        size_t i = at + tag.size();
        while (i < xmp.size() && (xmp[i] == ' ' || xmp[i] == '\t' || xmp[i] == '\r' || xmp[i] == '\n')) {
            ++i;
        }
        if (i < xmp.size() && (xmp[i] == '=' || xmp[i] == '>')) {
            const bool attr = xmp[i] == '=';
            ++i;
            while (attr && i < xmp.size() && (xmp[i] == ' ' || xmp[i] == '"' || xmp[i] == '\'')) {
                ++i;
            }
            std::string value;
            while (i < xmp.size() && xmp[i] != '"' && xmp[i] != '\'' && xmp[i] != '<' && value.size() < 8) {
                value += xmp[i++];
            }
            if (!value.empty()) {
                return value;
            }
        }
        at = xmp.find(tag, at + tag.size());
    }
    return {};
}

std::string pdfaOf(fz_context* ctx, pdf_document* pdf) {
    mu::BufferHandle buf(ctx);
    mu::call(ctx, [&] {
        pdf_obj* meta = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/Metadata");
        if (pdf_is_stream(ctx, meta)) {
            buf.reset(pdf_load_stream(ctx, meta));
        }
    });
    if (!buf) {
        return {};
    }
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    const std::string xmp(reinterpret_cast<const char*>(data), len);
    const std::string part = xmpValue(xmp, "part");
    if (part.empty()) {
        return {};
    }
    std::string conformance = xmpValue(xmp, "conformance");
    for (char& c : conformance) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return "PDF/A-" + part + conformance;
}

std::unique_ptr<Document> requireOk(OpenResult r, const std::string& what) {
    if (r.ok) {
        return std::move(r.document);
    }
    if (r.needsPassword) {
        throw std::runtime_error("password required: " + what);
    }
    if (r.error.code != 0) {
        throw MuError(r.error.code, r.error.message);
    }
    throw std::runtime_error(r.error.message.empty() ? "cannot open " + what : r.error.message);
}

}  // namespace

Document::Document(std::unique_ptr<MuContext> mu, fz_document* doc, std::string bytes, fs::path path)
    : mu_(std::move(mu)), doc_(doc), memory_(std::move(bytes)), path_(std::move(path)) {}

Document::~Document() {
    if (doc_ != nullptr) {
        fz_drop_document(mu_->get(), doc_);
    }
}

OpenResult Document::open(const fs::path& path, const std::string& password) {
    return open(MuContext::global(), path, password);
}

OpenResult Document::open(MuContext& base, const fs::path& path, const std::string& password) {
    OpenResult r;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        r.error = {ErrorKind::NotFound, 0, "file not found: " + paths::toUtf8(path)};
        return r;
    }
    try {
        std::unique_ptr<Document> doc(new Document(base.clone(), nullptr, {}, path));
        fz_context* ctx = doc->mu_->get();
        const std::wstring wide = path.native();
        const std::string magic = magicFor(path);
        mu::StreamHandle stm(ctx);
        mu::call(ctx, [&] {
            stm.reset(fz_open_file_w(ctx, wide.c_str()));
            doc->doc_ = fz_open_document_with_stream(ctx, magic.c_str(), stm);
        });
        return finishOpen(std::move(doc), password);
    } catch (const MuError& e) {
        r.error = toDocError(e);
    } catch (const std::exception& e) {
        r.error = {ErrorKind::Other, 0, e.what()};
    }
    return r;
}

OpenResult Document::openMemory(std::string bytes, const std::string& magic, const std::string& password) {
    return openMemory(MuContext::global(), std::move(bytes), magic, password);
}

OpenResult Document::openMemory(MuContext& base, std::string bytes, const std::string& magic,
                                const std::string& password) {
    OpenResult r;
    try {
        // Данные переезжают в документ до открытия потока: указатель fz_open_memory остаётся верным.
        std::unique_ptr<Document> doc(new Document(base.clone(), nullptr, std::move(bytes), {}));
        fz_context* ctx = doc->mu_->get();
        const auto* data = reinterpret_cast<const unsigned char*>(doc->memory_.data());
        const size_t size = doc->memory_.size();
        mu::StreamHandle stm(ctx);
        mu::call(ctx, [&] {
            stm.reset(fz_open_memory(ctx, data, size));
            doc->doc_ = fz_open_document_with_stream(ctx, magic.c_str(), stm);
        });
        return finishOpen(std::move(doc), password);
    } catch (const MuError& e) {
        r.error = toDocError(e);
    } catch (const std::exception& e) {
        r.error = {ErrorKind::Other, 0, e.what()};
    }
    return r;
}

OpenResult Document::finishOpen(std::unique_ptr<Document> doc, const std::string& password) {
    OpenResult r;
    try {
        if (doc->needsPassword()) {
            if (password.empty() || !doc->authenticate(password)) {
                r.needsPassword = true;
                r.document = std::move(doc);
                return r;
            }
        } else if (!password.empty()) {
            // Пустой пароль пользователя MuPDF подставил сам; заданный пароль может быть паролем владельца.
            pdf_document* pdf = pdf_document_from_fz_document(doc->mu_->get(), doc->doc_);
            if (pdf != nullptr && pdf->crypt != nullptr) {
                (void)doc->authenticate(password);
            }
        }
        (void)doc->pageCount();  // дерево страниц читается сразу: ремонт виден в флаге
        r.repaired = doc->repaired();
        r.pdfa = doc->isPdfA();
        r.hasXfa = doc->hasXfa();
        if (doc->isPdf()) {
            const FieldScan scan =
                scanFields(doc->mu_->get(), pdf_document_from_fz_document(doc->mu_->get(), doc->doc_));
            r.hasForm = scan.fillable > 0;
            r.isSigned = scan.signedSigs > 0;
        }
        r.ok = true;
        r.document = std::move(doc);
    } catch (const MuError& e) {
        r.error = toDocError(e);
    }
    return r;
}

std::unique_ptr<Document> Document::openOrThrow(const fs::path& path, const std::string& password) {
    return requireOk(open(path, password), paths::toUtf8(path));
}

std::unique_ptr<Document> Document::openOrThrow(MuContext& base, const fs::path& path,
                                                const std::string& password) {
    return requireOk(open(base, path, password), paths::toUtf8(path));
}

std::unique_ptr<Document> Document::openOrThrowMemory(std::string bytes, const std::string& magic) {
    return requireOk(openMemory(std::move(bytes), magic), "memory");
}

std::unique_ptr<Document> Document::createBlank(int pages) { return createBlank(MuContext::global(), pages); }

std::unique_ptr<Document> Document::createBlank(MuContext& base, int pages) {
    std::unique_ptr<Document> doc(new Document(base.clone(), nullptr, {}, {}));
    fz_context* ctx = doc->mu_->get();
    mu::PdfDocumentHandle pdf(ctx);
    mu::PdfObjHandle page(ctx);
    mu::call(ctx, [&] {
        pdf.reset(pdf_create_document(ctx));
        for (int i = 0; i < pages; ++i) {
            page.reset(pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, nullptr, nullptr));
            pdf_insert_page(ctx, pdf, -1, page);
        }
    });
    doc->doc_ = &pdf.release()->super;
    return doc;
}

bool Document::needsPassword() const {
    int needs = 0;
    mu::call(mu_->get(), [&] { needs = fz_needs_password(mu_->get(), doc_); });
    return needs != 0;
}

bool Document::authenticate(const std::string& password) {
    int ok = 0;
    mu::call(mu_->get(), [&] { ok = fz_authenticate_password(mu_->get(), doc_, password.c_str()); });
    if ((ok & 4) != 0) {  // бит 2 — подошёл пароль владельца (pdf_authenticate_password)
        ownerAuthenticated_ = true;
    }
    return ok != 0;
}

bool Document::isPdf() const noexcept { return pdf_document_from_fz_document(mu_->get(), doc_) != nullptr; }

bool Document::repaired() const {
    pdf_document* pdf = pdf_document_from_fz_document(mu_->get(), doc_);
    return pdf != nullptr && pdf_was_repaired(mu_->get(), pdf) != 0;
}

bool Document::isPdfA() const {
    pdf_document* pdf = pdf_document_from_fz_document(mu_->get(), doc_);
    return pdf != nullptr && !pdfaOf(mu_->get(), pdf).empty();
}

bool Document::isSigned() const {
    pdf_document* pdf = pdf_document_from_fz_document(mu_->get(), doc_);
    return pdf != nullptr && scanFields(mu_->get(), pdf).signedSigs > 0;
}

bool Document::hasForm() const {
    pdf_document* pdf = pdf_document_from_fz_document(mu_->get(), doc_);
    return pdf != nullptr && scanFields(mu_->get(), pdf).fillable > 0;
}

bool Document::hasXfa() const {
    fz_context* ctx = mu_->get();
    pdf_document* pdf = pdf_document_from_fz_document(ctx, doc_);
    if (pdf == nullptr) {
        return false;
    }
    int xfa = 0;
    mu::call(ctx, [&] { xfa = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/XFA") != nullptr; });
    return xfa != 0;
}

bool Document::allowed(Perm perm) const {
    fz_permission p = FZ_PERMISSION_PRINT;
    switch (perm) {
        case Perm::Print:
            p = FZ_PERMISSION_PRINT;
            break;
        case Perm::PrintHighRes:
            p = FZ_PERMISSION_PRINT_HQ;
            break;
        case Perm::Copy:
            p = FZ_PERMISSION_COPY;
            break;
        case Perm::Edit:
            p = FZ_PERMISSION_EDIT;
            break;
        case Perm::Annotate:
            p = FZ_PERMISSION_ANNOTATE;
            break;
        case Perm::FillForms:
            p = FZ_PERMISSION_FORM;
            break;
        case Perm::Accessibility:
            p = FZ_PERMISSION_ACCESSIBILITY;
            break;
        case Perm::Assemble:
            p = FZ_PERMISSION_ASSEMBLE;
            break;
    }
    int ok = 0;
    mu::call(mu_->get(), [&] { ok = fz_has_permission(mu_->get(), doc_, p); });
    return ok != 0;
}

DocProperties Document::properties() const {
    fz_context* ctx = mu_->get();
    DocProperties props;
    auto meta = [&](const char* key) {
        char buf[2048];
        int n = -1;
        mu::call(ctx, [&] { n = fz_lookup_metadata(ctx, doc_, key, buf, sizeof(buf)); });
        return n > 0 ? std::string(buf) : std::string();
    };
    props.title = meta(FZ_META_INFO_TITLE);
    props.author = meta(FZ_META_INFO_AUTHOR);
    props.subject = meta(FZ_META_INFO_SUBJECT);
    props.keywords = meta(FZ_META_INFO_KEYWORDS);
    props.creator = meta(FZ_META_INFO_CREATOR);
    props.producer = meta(FZ_META_INFO_PRODUCER);
    props.creationDate = meta(FZ_META_INFO_CREATIONDATE);
    props.modDate = meta(FZ_META_INFO_MODIFICATIONDATE);
    props.format = meta(FZ_META_FORMAT);
    props.pageCount = pageCount();
    pdf_document* pdf = pdf_document_from_fz_document(ctx, doc_);
    if (pdf == nullptr) {
        return props;
    }
    int version = 0, linearized = 0, revision = 0, p = -4;
    mu::call(ctx, [&] {
        version = pdf_version(ctx, pdf);
        linearized = pdf_doc_was_linearized(ctx, pdf);
        if (pdf->crypt != nullptr) {
            revision = pdf_crypt_revision(ctx, pdf->crypt);
            p = pdf_crypt_permissions(ctx, pdf->crypt);
        }
    });
    props.pdfVersion = version;
    props.linearized = linearized != 0;
    props.encrypted = pdf->crypt != nullptr;
    if (props.encrypted) {
        props.encryption = meta(FZ_META_ENCRYPTION);
        props.encryptionRevision = revision;
        props.permissionsP = p;
        props.ownerAuthenticated = ownerAuthenticated_;
    }
    props.pdfa = pdfaOf(ctx, pdf);
    return props;
}

int Document::pageCount() const {
    int n = 0;
    mu::call(mu_->get(), [&] { n = fz_count_pages(mu_->get(), doc_); });
    return n;
}

RectF Document::pageBox(int index) const {
    fz_context* ctx = mu_->get();
    mu::PageHandle page(ctx);
    fz_rect box{};
    mu::call(ctx, [&] {
        page.reset(fz_load_page(ctx, doc_, index));
        box = fz_bound_page(ctx, page);
    });
    return {box.x0, box.y0, box.x1, box.y1};
}

int Document::pageRotation(int index) const {
    fz_context* ctx = mu_->get();
    pdf_document* pdf = pdf_document_from_fz_document(ctx, doc_);
    if (pdf == nullptr) {
        return 0;
    }
    int rotate = 0;
    mu::call(ctx, [&] {
        pdf_obj* page = pdf_lookup_page_obj(ctx, pdf, index);
        rotate = pdf_to_int(ctx, pdf_dict_get_inheritable(ctx, page, PDF_NAME(Rotate)));
    });
    // Как pdf_page_transform: остаток от 360, к ближайшему кратному 90.
    rotate %= 360;
    if (rotate < 0) {
        rotate += 360;
    }
    return (90 * ((rotate + 45) / 90)) % 360;
}

PageSize Document::pageSize(int index) const {
    const RectF box = pageBox(index);
    return {box.width(), box.height()};
}

Bitmap Document::renderPage(int index, const RenderOptions& options) const {
    return Renderer(*this).renderPage(index, options.dpi, options.annotations);
}

std::string Document::savePdfToMemory() const {
    fz_context* ctx = mu_->get();
    mu::BufferHandle buf(ctx);
    mu::OutputHandle out(ctx);
    mu::call(ctx, [&] {
        pdf_document* pdf = pdf_document_from_fz_document(ctx, doc_);
        if (pdf == nullptr) {
            fz_throw(ctx, FZ_ERROR_ARGUMENT, "not a PDF document");
        }
        buf.reset(fz_new_buffer(ctx, 64 * 1024));
        out.reset(fz_new_output_with_buffer(ctx, buf));
        pdf_write_options opts = pdf_default_write_options;
        pdf_write_document(ctx, pdf, out, &opts);
        fz_close_output(ctx, out);
    });
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

}  // namespace ot
