#include "core/doc/document.h"

#include <cstdio>
#include <cstring>
#include <cwctype>
#include <stdexcept>
#include <string_view>

#include "core/doc/journal.h"
#include "core/doc/permissions.h"
#include "core/doc/props.h"
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

// Отпечаток xref как у MuPDF (pdf_fingerprint_file с секции 0): журнал подходит только к тому же файлу.
void xrefFingerprint(const pdf_document* pdf, unsigned char digest[16]) {
    fz_md5 state;
    fz_md5_init(&state);
    fz_md5_update_int64(&state, pdf->num_xref_sections);
    for (int i = 0; i < pdf->num_xref_sections; ++i) {
        fz_md5_update_int64(&state, pdf->xref_sections[i].num_objects);
        for (const pdf_xref_subsec* sub = pdf->xref_sections[i].subsec; sub != nullptr; sub = sub->next) {
            fz_md5_update_int64(&state, sub->start);
            fz_md5_update_int64(&state, sub->len);
        }
    }
    fz_md5_final(&state, digest);
}

// Фрагмент журнала «N 0 obj … [stream … endstream] endobj» или «N 0 newobj». pdf_parse_journal_obj MuPDF
// 1.28.5 читает поток фильтром поиска endstream (длина 0), а тот на потоке в памяти обрывает данные длиннее
// ~1 КБ (XMP после Document::setProperties: 1025 байт из 1371 и «expected 'endstream' keyword», 2.4b). Поток
// здесь берётся по /Length фрагмента (pdf_write_journal пишет данные и сразу за ними перевод строки и
// endstream); не сходится — по первому «перевод строки + endstream» после начала данных.
pdf_obj* parseJournalObject(fz_context* ctx, pdf_document* pdf, fz_stream* stm, const std::string& journal,
                            int* num, fz_buffer** buf, int* newobj) {
    *buf = nullptr;
    *newobj = 0;
    const auto at = static_cast<size_t>(fz_tell(ctx, stm));
    const size_t eol = journal.find('\n', at);
    const std::string line = journal.substr(at, eol == std::string::npos ? std::string::npos : eol - at);
    int n = 0;
    int gen = 0;
    char keyword[8] = {};
    if (std::sscanf(line.c_str(), "%d %d %7s", &n, &gen, keyword) == 3 &&
        std::strcmp(keyword, "newobj") == 0) {
        *num = n;
        *newobj = 1;
        fz_seek(ctx, stm, static_cast<int64_t>(eol == std::string::npos ? journal.size() : eol + 1),
                SEEK_SET);
        return nullptr;
    }
    int64_t stmofs = 0;
    pdf_obj* value = pdf_parse_ind_obj(ctx, pdf, stm, num, &gen, &stmofs, nullptr);
    if (stmofs == 0) {
        return value;  // без потока: endobj уже прочитан
    }
    // Всё, что может не сойтись, проверяется до выделения буфера (fz_try — только в mu::call).
    static constexpr std::string_view kEnd = "\nendstream";
    const auto start = static_cast<size_t>(stmofs);
    const int64_t declared = pdf_dict_get_int64(ctx, value, PDF_NAME(Length));
    size_t len = std::string::npos;
    if (declared >= 0 && start + static_cast<size_t>(declared) + kEnd.size() <= journal.size() &&
        std::string_view(journal).substr(start + static_cast<size_t>(declared), kEnd.size()) == kEnd) {
        len = static_cast<size_t>(declared);
    } else if (const size_t end = journal.find(kEnd, start); end != std::string::npos) {
        len = end - start;
    }
    size_t after = std::string::npos;  // за «endobj»
    if (len != std::string::npos) {
        size_t i = start + len + kEnd.size();
        while (i < journal.size() && (journal[i] == ' ' || journal[i] == '\r' || journal[i] == '\n')) {
            ++i;
        }
        if (std::string_view(journal).substr(i, 6) == "endobj") {
            after = i + 6;
        }
    }
    if (after == std::string::npos) {
        pdf_drop_obj(ctx, value);
        fz_throw(ctx, FZ_ERROR_SYNTAX, "journal stream of object %d: expected 'endstream' and 'endobj'",
                 *num);
    }
    *buf = fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(journal.data()) + start,
                                          len);
    fz_seek(ctx, stm, static_cast<int64_t>(after), SEEK_SET);
    return value;
}

// Журнал автосохранения (pdf_write_journal с позицией 0: у фрагментов — значения «после шага») на только что
// открытый исходник. pdf_read_journal в MuPDF 1.28.5 не годится: pdf_deserialise_journal добавляет фрагменты
// в pending_tail, которого при загрузке нет (assert в Debug, «Can't add a journal fragment absent an
// operation» в Release). Поэтому каждый шаг переигрывается операцией, как pdf_redo: запись xref получает
// значение фрагмента, а прежнее значение уходит фрагментом в журнал (pdf_add_journal_fragment). После всех
// шагов — undo до позиции.
void replayJournal(fz_context* ctx, pdf_document* pdf, const std::string& journal, int position) {
    const auto* data = reinterpret_cast<const unsigned char*>(journal.data());
    const size_t size = journal.size();
    pdf_lexbuf* lex = &pdf->lexbuf.base;
    mu::StreamHandle stm(ctx);
    mu::PdfObjHandle header(ctx);
    int version = 0;
    int badFormat = 0;
    mu::call(ctx, [&] {
        pdf_enable_journal(ctx, pdf);
        stm.reset(fz_open_memory(ctx, data, size));
        if (fz_skip_string(ctx, stm, "%!MuPDF-Journal-") != 0) {
            badFormat = 1;
        } else {
            for (;;) {
                const int c = fz_peek_byte(ctx, stm);
                if (c < '0' || c > '9') {
                    break;
                }
                version = version * 10 + c - '0';
                (void)fz_read_byte(ctx, stm);
            }
            fz_skip_space(ctx, stm);
            if (version != 100 || fz_skip_string(ctx, stm, "journal\n") != 0 ||
                pdf_lex(ctx, stm, lex) != PDF_TOK_OPEN_DICT) {
                badFormat = 1;
            } else {
                header.reset(pdf_parse_dict(ctx, pdf, stm, lex));
            }
        }
    });
    if (badFormat != 0) {
        throw JournalMismatch("not a MuPDF journal");
    }
    int64_t fileSize = 0;
    int historyPos = 0;
    const char* fingerprint = nullptr;
    size_t fingerprintLen = 0;
    mu::call(ctx, [&] {
        fileSize = pdf_dict_get_int64(ctx, header, PDF_NAME(FileSize));
        historyPos = pdf_dict_get_int(ctx, header, PDF_NAME(HistoryPos));
        pdf_obj* fp = pdf_dict_get(ctx, header, PDF_NAME(Fingerprint));
        fingerprint = pdf_to_str_buf(ctx, fp);
        fingerprintLen = pdf_to_str_len(ctx, fp);
    });
    unsigned char digest[16];
    xrefFingerprint(pdf, digest);
    if (fingerprintLen != sizeof(digest) || std::memcmp(fingerprint, digest, sizeof(digest)) != 0 ||
        pdf->file_size < fileSize) {
        throw JournalMismatch("journal was written for another file");
    }
    if (historyPos != 0) {
        throw JournalMismatch("journal is not written from the start of history");
    }

    int steps = 0;
    int open = 0;  // шаг (операция) открыт
    for (;;) {
        int kind = 0;  // 1 — новый шаг, 2 — конец журнала, 3 — фрагмент
        const char* title = nullptr;
        size_t titleLen = 0;
        mu::call(ctx, [&] {
            fz_skip_space(ctx, stm);
            if (fz_skip_string(ctx, stm, "entry\n") == 0) {
                if (pdf_lex(ctx, stm, lex) != PDF_TOK_STRING) {
                    fz_throw(ctx, FZ_ERROR_FORMAT, "bad step title in journal");
                }
                title = lex->buffer;
                titleLen = lex->len;
                kind = 1;
            } else if (fz_skip_string(ctx, stm, /* en */ "djournal") == 0) {  // «en» съел «entry\n»
                kind = 2;
            } else {
                kind = 3;
            }
        });
        if (kind == 2) {
            break;
        }
        if (kind == 1) {
            const std::string name(title, titleLen);
            const char* op = name.c_str();
            mu::call(ctx, [&] {
                if (open != 0) {
                    pdf_end_operation(ctx, pdf);
                }
                pdf_begin_operation(ctx, pdf, op);
            });
            open = 1;
            ++steps;
            continue;
        }
        if (open == 0) {
            throw JournalMismatch("badly formed journal: object before the first step");
        }
        mu::call(ctx, [&] {
            int num = 0;
            int newobj = 0;
            fz_buffer* buf = nullptr;
            pdf_obj* value = parseJournalObject(ctx, pdf, stm, journal, &num, &buf, &newobj);
            // Как swap_fragments при redo: запись получает значение фрагмента, фрагмент — прежнее значение.
            pdf_xref_entry* x = pdf_get_incremental_xref_entry(ctx, pdf, num);
            pdf_obj* oldObj = x->obj;
            fz_buffer* oldBuf = x->stm_buf;
            const int oldEmpty = x->type == 0;
            x->obj = value;
            x->stm_buf = buf;
            x->type = newobj != 0 ? 0 : 'n';
            pdf_add_journal_fragment(ctx, pdf, num, oldObj, oldBuf, oldEmpty);
            pdf_purge_object_from_store(ctx, pdf, num);
        });
    }
    if (open != 0) {
        mu::call(ctx, [&] { pdf_end_operation(ctx, pdf); });
    }
    if (steps < position) {
        throw JournalMismatch("journal has fewer steps than the saved position");
    }
    for (int i = steps; i > position; --i) {
        mu::call(ctx, [&] { pdf_undo(ctx, pdf); });
    }
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
    : mu_(std::move(mu)), doc_(doc), memory_(std::move(bytes)), path_(std::move(path)), file_(path_) {}

Document::~Document() {
    release();
    if (!backing_.empty()) {
        std::error_code ec;
        fs::remove(backing_, ec);  // несохранённое во временном файле неудачной замены
    }
}

void Document::release() noexcept {
    undo_.reset();
    if (doc_ != nullptr) {
        fz_drop_document(mu_->get(), doc_);
        doc_ = nullptr;
    }
}

void Document::reopen(const fs::path& file, const std::string& password, const fs::path& documentPath,
                      const fs::path& backing) {
    release();
    memory_.clear();
    ownerAuthenticated_ = false;
    fz_context* ctx = mu_->get();
    const std::wstring wide = file.native();
    fz_document* opened = nullptr;
    mu::StreamHandle stm(ctx);
    mu::call(ctx, [&] {
        stm.reset(fz_open_file_w(ctx, wide.c_str()));
        opened = fz_open_document_with_stream(ctx, ".pdf", stm);
    });
    doc_ = opened;
    path_ = documentPath;
    backing_ = backing;
    file_ = file;
    if (needsPassword() && !authenticate(password)) {
        release();
        throw std::runtime_error("saved file does not open with the document password: " +
                                 paths::toUtf8(file));
    }
    if (!password.empty() && !ownerAuthenticated_ && pdf() != nullptr && pdf()->crypt != nullptr) {
        (void)authenticate(password);  // пароль владельца у файла без пароля пользователя
    }
    (void)pageCount();
    if (isPdf()) {
        undo_ = std::make_unique<UndoStack>(*this);
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
        if (doc->isPdf()) {
            doc->undo_ = std::make_unique<UndoStack>(*doc);  // журнал — сразу после открытия
        }
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

std::unique_ptr<Document> Document::recoverFrom(const fs::path& source, const std::string& journal,
                                                int position, const fs::path& documentPath,
                                                const std::string& password) {
    std::unique_ptr<Document> doc(new Document(MuContext::global().clone(), nullptr, {}, documentPath));
    doc->file_ = source;
    fz_context* ctx = doc->mu_->get();
    const std::wstring wide = source.native();
    mu::StreamHandle stm(ctx);
    mu::call(ctx, [&] {
        stm.reset(fz_open_file_w(ctx, wide.c_str()));
        doc->doc_ = fz_open_document_with_stream(ctx, ".pdf", stm);
    });
    pdf_document* pdf = doc->pdf();
    if (pdf == nullptr) {
        throw std::runtime_error("autosave source is not a PDF: " + paths::toUtf8(source));
    }
    if (doc->needsPassword() && !doc->authenticate(password)) {
        throw std::runtime_error("password required: " + paths::toUtf8(source));
    }
    (void)doc->pageCount();  // как при открытии: ремонт (если нужен) — до журнала
    replayJournal(ctx, pdf, journal, position);
    doc->undo_ = std::make_unique<UndoStack>(*doc);
    return doc;
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
    doc->undo_ = std::make_unique<UndoStack>(*doc);
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
    if (ok != 0 && !password.empty()) {
        password_ = password;
    }
    return ok != 0;
}

bool Document::isPdf() const noexcept { return pdf() != nullptr; }

pdf_document* Document::pdf() const noexcept {
    return doc_ != nullptr ? pdf_document_from_fz_document(mu_->get(), doc_) : nullptr;
}

UndoStack& Document::undoStack() {
    if (!undo_) {
        throw std::logic_error("document has no edit journal (not a PDF)");
    }
    return *undo_;
}

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

bool Document::isPdfX() const {
    pdf_document* pdf = this->pdf();
    if (pdf == nullptr) {
        return false;
    }
    fz_context* ctx = mu_->get();
    int found = 0;
    mu::call(ctx, [&] {
        pdf_obj* intents = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/OutputIntents");
        const int n = pdf_array_len(ctx, intents);
        for (int i = 0; i < n && found == 0; ++i) {
            found = pdf_name_eq(ctx, pdf_dict_get(ctx, pdf_array_get(ctx, intents, i), PDF_NAME(S)),
                                PDF_NAME(GTS_PDFX));
        }
    });
    return found != 0;
}

bool Document::allowed(Perm perm) const { return Permissions::of(*this).allows(perm); }

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

void Document::setProperties(const DocProperties& p) {
    props::InfoFields info = props::readInfo(*this);
    info.title = p.title;
    info.author = p.author;
    info.subject = p.subject;
    info.keywords = p.keywords;
    info.creator = p.creator;
    info.producer = p.producer;
    info.creationDate = p.creationDate;
    props::writeInfo(*this, info);
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

std::string Document::savePdfToMemory() {
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
    if (undo_) {
        undo_->clear();
    }
    unsigned char* data = nullptr;
    const size_t len = fz_buffer_storage(ctx, buf, &data);
    return std::string(reinterpret_cast<const char*>(data), len);
}

}  // namespace ot
