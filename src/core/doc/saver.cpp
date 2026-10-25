#include "core/doc/saver.h"

#include <windows.h>
#include <io.h>
#include <spdlog/spdlog.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <random>
#include <stdexcept>
#include <system_error>

#include "core/doc/autosave.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/paths.h"

namespace ot {

namespace fs = std::filesystem;

namespace {

SaveReport refuse(SaveReport r, SaveError error, std::string message) {
    r.error = error;
    r.message = std::move(message);
    return r;
}

// Временный файл в папке цели (замена MoveFileExW — в пределах тома): ".~<имя>.<8 hex>.tmp".
fs::path tempPathFor(const fs::path& target) {
    std::random_device rd;
    wchar_t suffix[16];
    swprintf_s(suffix, L".%08x.tmp", rd());
    return target.parent_path() / (L".~" + target.filename().wstring() + suffix);
}

void removeQuietly(const fs::path& p) {
    if (p.empty()) {
        return;
    }
    std::error_code ec;
    fs::remove(p, ec);
}

std::string lastErrorText(DWORD code) { return std::system_category().message(static_cast<int>(code)); }

// Трейлер в журнал не входит: /Info, созданный правкой и отменённый, остаётся ссылкой на свободный объект —
// убрать её до записи (иначе в файле ссылка в никуда).
void dropDanglingTrailerRefs(Document& doc) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    mu::call(ctx, [&] {
        pdf_obj* trailer = pdf_trailer(ctx, pdf);
        pdf_obj* info = pdf_dict_get(ctx, trailer, PDF_NAME(Info));
        if (pdf_is_indirect(ctx, info) && !pdf_object_exists(ctx, pdf, pdf_to_num(ctx, info))) {
            pdf_dict_del(ctx, trailer, PDF_NAME(Info));
        }
    });
}

void fillWriteOptions(pdf_write_options& opts, SaveMode mode, const SaveOptions& options) {
    opts = pdf_default_write_options;
    switch (mode) {
        case SaveMode::Incremental:
            opts.do_incremental = 1;  // с garbage несовместим
            break;
        case SaveMode::Full:
            opts.do_garbage = 2;  // собрать мусор и перенумеровать
            opts.do_compress = 1;
            break;
        case SaveMode::FullOptimized:
        case SaveMode::Auto:
            opts.do_garbage = 3;  // ещё и одинаковые объекты — в один
            opts.do_compress = 1;
            opts.do_compress_images = 1;
            opts.do_compress_fonts = 1;
            opts.do_use_objstms = 1;
            break;
    }
    if (mode != SaveMode::Incremental) {
        if (options.objectStreams) {
            opts.do_use_objstms = *options.objectStreams ? 1 : 0;
        }
        if (options.compressImages) {
            opts.do_compress_images = *options.compressImages ? 1 : 0;
        }
    }
    opts.do_linear = 0;  // никогда: линеаризация — qpdf поверх записи
    if (options.encryption) {
        const SaveEncryption& e = *options.encryption;
        switch (e.method) {
            case SaveEncryption::Method::None:
                opts.do_encrypt = PDF_ENCRYPT_NONE;
                break;
            case SaveEncryption::Method::Aes128:
                opts.do_encrypt = PDF_ENCRYPT_AES_128;
                break;
            case SaveEncryption::Method::Aes256:
                opts.do_encrypt = PDF_ENCRYPT_AES_256;
                break;
        }
        strncpy_s(opts.upwd_utf8, sizeof(opts.upwd_utf8), e.userPassword.c_str(), _TRUNCATE);
        strncpy_s(opts.opwd_utf8, sizeof(opts.opwd_utf8), e.ownerPassword.c_str(), _TRUNCATE);
        opts.permissions = e.permissions;
    }
}

// pdf_write_document в файл через _wfopen (путь UTF-16), с fsync перед закрытием. Файл открыт на чтение и
// запись: при инкременте MuPDF сверяет уже записанное (fz_stream_from_output), при подписи (этап 4) —
// обязательно.
void writePdf(Document& doc, const fs::path& file, SaveMode mode, const SaveOptions& options) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    pdf_write_options opts;
    fillWriteOptions(opts, mode, options);
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"w+b") != 0 || f == nullptr) {
        throw std::runtime_error("cannot create " + paths::toUtf8(file) + ": " + std::strerror(errno));
    }
    mu::OutputHandle out(ctx);
    int owned = 0;  // FILE* перешёл к fz_output (закроет его drop)
    try {
        mu::call(ctx, [&] {
            out.reset(fz_new_output_with_file_ptr(ctx, f));
            owned = 1;
            pdf_write_document(ctx, pdf, out, &opts);
            fz_close_output(ctx, out);
        });
    } catch (...) {
        if (owned == 0) {
            (void)fclose(f);
        }
        throw;
    }
    if (_commit(_fileno(f)) != 0) {
        spdlog::warn("saver: flushing {} failed: {}", paths::toUtf8(file), std::strerror(errno));
    }
    out.reset();  // fclose
}

}  // namespace

const char* Saver::modeName(SaveMode mode) noexcept {
    switch (mode) {
        case SaveMode::Auto:
            return "auto";
        case SaveMode::Incremental:
            return "incremental";
        case SaveMode::Full:
            return "full";
        case SaveMode::FullOptimized:
            return "full-optimized";
    }
    return "?";
}

const char* Saver::errorName(SaveError error) noexcept {
    switch (error) {
        case SaveError::None:
            return "none";
        case SaveError::NotPdf:
            return "not-pdf";
        case SaveError::NoPath:
            return "no-path";
        case SaveError::WouldBreakSignatures:
            return "would-break-signatures";
        case SaveError::IncrementalNotPossible:
            return "incremental-not-possible";
        case SaveError::WriteFailed:
            return "write-failed";
        case SaveError::LinearizeFailed:
            return "linearize-failed";
        case SaveError::ReplaceFailed:
            return "replace-failed";
        case SaveError::ReopenFailed:
            return "reopen-failed";
        case SaveError::SameAsDocument:
            return "same-as-document";
    }
    return "?";
}

SaveReport Saver::decide(Document& doc, const SaveOptions& options) {
    SaveReport r;
    r.mode = options.mode;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return refuse(r, SaveError::NotPdf, "only PDF documents are saved; others are exported");
    }
    fz_context* ctx = doc.context().get();
    int canIncremental = 0;
    mu::call(ctx, [&] { canIncremental = pdf_can_be_saved_incrementally(ctx, pdf); });
    const bool newDocument = pdf->file == nullptr;  // pdf_create_document: дописывать не к чему
    const bool encryption = options.encryption.has_value();
    const bool appendable = canIncremental != 0 && !newDocument && !encryption;
    const bool heavy = doc.journaled() && doc.undoStack().hasHeavyChanges();

    if (doc.isSigned() && !options.allowBreakingSignatures) {
        // Правило 1: подписанный — только инкремент.
        r.mode = SaveMode::Incremental;
        if (heavy) {
            return refuse(r, SaveError::WouldBreakSignatures, "heavy edits rewrite the signed file");
        }
        if (!appendable) {
            return refuse(r, SaveError::WouldBreakSignatures,
                          encryption ? "changing encryption rewrites the signed file"
                                     : "the signed file cannot be appended (repaired or redacted)");
        }
        if (options.mode == SaveMode::Full || options.mode == SaveMode::FullOptimized) {
            return refuse(r, SaveError::WouldBreakSignatures, "full write of a signed file");
        }
        if (options.linearize) {
            return refuse(r, SaveError::WouldBreakSignatures, "linearization rewrites the signed file");
        }
        return r;
    }
    switch (options.mode) {
        case SaveMode::Incremental:
            if (!appendable) {
                return refuse(r, SaveError::IncrementalNotPossible,
                              newDocument  ? "new document has no file to append to"
                              : encryption ? "changing encryption needs a full write"
                                           : "repaired or redacted file cannot be appended");
            }
            break;
        case SaveMode::Full:
        case SaveMode::FullOptimized:
            break;
        case SaveMode::Auto:
            // Правила 3, 4, 2.
            r.mode = heavy ? SaveMode::FullOptimized : (appendable ? SaveMode::Incremental : SaveMode::Full);
            break;
    }
    return r;
}

SaveReport Saver::save(Document& doc, const SaveOptions& options) {
    if (doc.path().empty()) {
        SaveReport r = decide(doc, options);
        return r.ok() ? refuse(r, SaveError::NoPath, "document has no file: use Save As") : r;
    }
    return save(doc, doc.path(), options);
}

SaveReport Saver::save(Document& doc, const fs::path& target, const SaveOptions& options) {
    SaveReport r = decide(doc, options);
    if (!r.ok()) {
        return r;
    }
    if (target.empty()) {
        return refuse(r, SaveError::NoPath, "no target path");
    }
    std::error_code ec;
    const fs::path absTarget = fs::absolute(target, ec);
    if (ec) {
        return refuse(r, SaveError::WriteFailed, "bad target path: " + ec.message());
    }
    const fs::path documentPath = doc.path();
    const fs::path oldBacking = doc.backingFile();
    std::string password = doc.password();
    if (options.encryption) {
        password = !options.encryption->ownerPassword.empty() ? options.encryption->ownerPassword
                                                              : options.encryption->userPassword;
    }

    fs::path temp = tempPathFor(absTarget);
    try {
        dropDanglingTrailerRefs(doc);
        if (options.prepare && r.mode != SaveMode::Incremental) {
            options.prepare(doc);
        }
        if (options.subsetFonts && r.mode != SaveMode::Incremental) {
            // Шаг журнала: при неудачной записи документ остаётся с подмножествами (undo вернёт шрифты).
            fz_context* ctx = doc.context().get();
            pdf_document* pdf = doc.pdf();
            Journal::Operation op(doc, "Subset fonts");
            mu::call(ctx, [&] { pdf_subset_fonts(ctx, pdf, 0, nullptr); });
            op.markHeavy();
            op.commit();
        }
        writePdf(doc, temp, r.mode, options);
    } catch (const std::exception& e) {
        removeQuietly(temp);
        // Запись могла уже перестроить xref документа — прежний журнал к нему не применим.
        if (doc.journaled()) {
            try {
                doc.undoStack().clear();
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): история и так потеряна
            }
        }
        spdlog::error("saver: writing {} failed: {}", paths::toUtf8(absTarget), e.what());
        return refuse(r, SaveError::WriteFailed, e.what());
    }

    if (options.linearize) {
        const QpdfTool qpdf;
        const fs::path linear = tempPathFor(absTarget);
        const ToolResult lr = qpdf.available() ? qpdf.linearize(temp, linear, password) : ToolResult{};
        // qpdf: 3 — предупреждения, файл записан.
        if (!qpdf.available() || (lr.exitCode != 0 && lr.exitCode != 3) || !fs::exists(linear, ec)) {
            removeQuietly(linear);
            removeQuietly(temp);
            if (doc.journaled()) {
                doc.undoStack().clear();
            }
            return refuse(r, SaveError::LinearizeFailed,
                          qpdf.available() ? "qpdf --linearize: " + lr.output : "qpdf.exe not found");
        }
        removeQuietly(temp);
        temp = linear;
    }

    // Запись изменила xref документа в памяти — дальше он открывается из сохранённого файла. Файл документа
    // отпускается до замены: _wfopen не даёт FILE_SHARE_DELETE, иначе MoveFileExW в свой путь не пройдёт.
    doc.release();
    if (MoveFileExW(temp.c_str(), absTarget.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
        0) {
        const DWORD code = GetLastError();
        r = refuse(r, SaveError::ReplaceFailed,
                   "cannot replace " + paths::toUtf8(absTarget) + ": " + lastErrorText(code));
        spdlog::warn("saver: {}; the document stays in {}", r.message, paths::toUtf8(temp));
        try {
            // Правки не теряются: документ — из временного файла, путь прежний.
            doc.reopen(temp, password, documentPath, temp);
            if (!oldBacking.empty() && oldBacking != temp) {
                removeQuietly(oldBacking);
            }
        } catch (const std::exception& e) {
            r.error = SaveError::ReopenFailed;
            r.message += std::string("; reopen failed: ") + e.what();
        }
        return r;
    }
    try {
        doc.reopen(absTarget, password, absTarget);
    } catch (const std::exception& e) {
        return refuse(r, SaveError::ReopenFailed, std::string("saved file does not reopen: ") + e.what());
    }
    if (!oldBacking.empty()) {
        removeQuietly(oldBacking);
    }
    r.bytes = fs::file_size(absTarget, ec);
    spdlog::info("saver: {} written {} ({} bytes)", paths::toUtf8(absTarget), modeName(r.mode), r.bytes);
    return r;
}

void Saver::revert(Document& doc) {
    if (doc.filePath().empty()) {
        throw std::logic_error("revert: the document has no file");
    }
    const fs::path file = doc.filePath();
    const fs::path documentPath = doc.path();
    const fs::path backing = doc.backingFile();
    const std::string password = doc.password();
    doc.reopen(file, password, documentPath, backing);
}

SaveReport Saver::saveCopy(Document& doc, const fs::path& target, const SaveOptions& options) {
    SaveReport r = decide(doc, options);
    if (!r.ok()) {
        return r;
    }
    if (target.empty()) {
        return refuse(r, SaveError::NoPath, "no target path");
    }
    const fs::path source = doc.filePath().empty() ? doc.path() : doc.filePath();
    if (source.empty() || doc.path().empty()) {
        return refuse(r, SaveError::NoPath, "document has no file to copy from: use Save As");
    }
    std::error_code ec;
    const fs::path absTarget = fs::absolute(target, ec);
    if (ec) {
        return refuse(r, SaveError::WriteFailed, "bad target path: " + ec.message());
    }
    for (const fs::path& own : {doc.path(), source}) {
        if (fs::equivalent(absTarget, own, ec) || absTarget == fs::absolute(own, ec)) {
            return refuse(r, SaveError::SameAsDocument, "a copy over the document's own file is Save");
        }
    }

    // Временная папка снимка: index.json и журнал (копия исходника не нужна — исходный файл открыт и цел).
    std::random_device rd;
    wchar_t name[48];
    swprintf_s(name, L"AcroBatya-copy-%lu-%08x", GetCurrentProcessId(), rd());
    const fs::path work = fs::temp_directory_path(ec) / name;
    struct RemoveDir {
        fs::path dir;
        ~RemoveDir() {
            std::error_code e;
            fs::remove_all(dir, e);
        }
    } cleanup{work};

    std::unique_ptr<Document> twin;
    try {
        Autosave snapshot(work, 0);
        std::string error;
        const AutosaveStatus status =
            doc.journaled() ? snapshot.save(doc, &error) : AutosaveStatus::NothingToSave;
        if (status == AutosaveStatus::Saved) {
            const std::optional<AutosaveEntry> entry = snapshot.find(doc.path());
            if (!entry) {
                return refuse(r, SaveError::WriteFailed, "edits snapshot has no entry");
            }
            RecoverResult recovered = snapshot.recover(*entry, doc.password());
            if (!recovered.ok()) {
                return refuse(r, SaveError::WriteFailed,
                              "edits snapshot does not replay: " + recovered.message);
            }
            twin = std::move(recovered.document);
        } else if (status == AutosaveStatus::NothingToSave) {
            twin = Document::openOrThrow(source, doc.password());
        } else {
            return refuse(r, SaveError::WriteFailed, "cannot snapshot edits: " + error);
        }
    } catch (const std::exception& e) {
        return refuse(r, SaveError::WriteFailed, std::string("copy: ") + e.what());
    }
    SaveOptions exact = options;
    exact.mode = r.mode;  // режим самого документа (тяжёлые правки второго документа решают так же)
    exact.allowBreakingSignatures = true;  // подписанному decide уже ответил выше
    if (options.flattenAnnotations) {
        // Плоская копия: аннотации — в содержимое страниц копии (виджеты полей остаются), запись полная.
        try {
            fz_context* tctx = twin->context().get();
            pdf_document* tpdf = twin->pdf();
            Journal::Operation op(*twin, "Flatten annotations");
            mu::call(tctx, [&] { pdf_bake_document(tctx, tpdf, 1, 0); });
            op.invalidateAll();
            op.markHeavy();
            op.commit();
        } catch (const std::exception& e) {
            return refuse(r, SaveError::WriteFailed, std::string("flatten: ") + e.what());
        }
        exact.mode = SaveMode::Full;
    }
    SaveReport written = save(*twin, absTarget, exact);
    written.mode = r.mode;
    if (written.ok()) {
        spdlog::info("saver: copy of {} written to {} ({} bytes)", paths::toUtf8(doc.path()),
                     paths::toUtf8(absTarget), written.bytes);
    }
    return written;
}

}  // namespace ot
