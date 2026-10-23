#include "core/doc/revisions.h"

#include <algorithm>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/paths.h"

namespace ot::revisions {

namespace fs = std::filesystem;

namespace {

// Подписанные поля: /FT /Sig (наследуемый) и собственный /V — виджеты-потомки поля не считаются повторно.
struct SigScan {
    pdf_obj** out = nullptr;
    int capacity = 0;
    int count = 0;
};

void collectSignedField(fz_context* ctx, pdf_obj* field, void* arg, pdf_obj** inherited) {
    auto* scan = static_cast<SigScan*>(arg);
    pdf_obj* ft = inherited[0];
    if (ft == nullptr || !pdf_name_eq(ctx, ft, PDF_NAME(Sig)) ||
        !pdf_is_dict(ctx, pdf_dict_get(ctx, field, PDF_NAME(V)))) {
        return;
    }
    if (scan->count < scan->capacity) {
        scan->out[scan->count] = field;
    }
    ++scan->count;
}

std::vector<pdf_obj*> signedFields(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    pdf_obj* names[2] = {PDF_NAME(FT), nullptr};
    pdf_obj* values[2] = {nullptr, nullptr};
    SigScan scan;
    auto walk = [&] {
        mu::call(ctx, [&] {
            pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
            if (pdf_is_array(ctx, fields)) {
                pdf_walk_tree(ctx, fields, PDF_NAME(Kids), collectSignedField, nullptr, &scan, names, values);
            }
        });
    };
    walk();  // счёт
    std::vector<pdf_obj*> out(static_cast<std::size_t>(scan.count));
    scan = SigScan{out.data(), static_cast<int>(out.size()), 0};
    walk();  // заполнение (подписанные объекты живут в xref, пока жив документ)
    out.resize(static_cast<std::size_t>(std::min<int>(scan.count, static_cast<int>(out.size()))));
    return out;
}

std::int64_t rangeEndOf(fz_context* ctx, pdf_document* pdf, pdf_obj* field) {
    int n = 0;
    mu::call(ctx, [&] { n = pdf_signature_byte_range(ctx, pdf, field, nullptr); });
    std::vector<fz_range> ranges(static_cast<std::size_t>(std::max(n, 0)));
    fz_range* data = ranges.data();
    if (n > 0) {
        mu::call(ctx, [&] { (void)pdf_signature_byte_range(ctx, pdf, field, data); });
    }
    std::int64_t end = 0;
    for (const fz_range& r : ranges) {
        end = std::max<std::int64_t>(end, r.offset + static_cast<std::int64_t>(r.length));
    }
    return end;
}

}  // namespace

int versionCount(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return 0;
    }
    fz_context* ctx = doc.context().get();
    int n = 0;
    mu::call(ctx, [&] { n = pdf_count_versions(ctx, pdf); });
    return n;
}

int lastValidVersion(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return 0;
    }
    fz_context* ctx = doc.context().get();
    int n = 0;
    mu::call(ctx, [&] { n = pdf_validate_change_history(ctx, pdf); });
    return n;
}

std::vector<SignatureInfo> signatures(Document& doc) {
    std::vector<SignatureInfo> out;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return out;
    }
    fz_context* ctx = doc.context().get();
    const int versions = versionCount(doc);
    const std::vector<pdf_obj*> fields = signedFields(doc);
    for (std::size_t i = 0; i < fields.size(); ++i) {
        pdf_obj* field = fields[i];
        SignatureInfo info;
        info.index = static_cast<int>(i);
        info.rangeEnd = rangeEndOf(ctx, pdf, field);
        info.coversWholeFile = info.rangeEnd == pdf->file_size;
        char* name = nullptr;
        int foundIn = 0;
        int changed = 0;
        mu::call(ctx, [&] {
            name = pdf_load_field_name(ctx, field);
            foundIn = pdf_find_version_for_obj(ctx, pdf, pdf_dict_get(ctx, field, PDF_NAME(V)));
            changed = pdf_validate_signature(ctx, pdf, field);
        });
        info.field = name != nullptr ? name : "";
        fz_free(ctx, name);
        info.version = versions - foundIn;  // 0 — последняя версия
        info.changedSince = changed;
        out.push_back(std::move(info));
    }
    return out;
}

std::string signedRevision(Document& doc, int sigIndex) {
    const std::vector<pdf_obj*> fields = signedFields(doc);
    if (sigIndex < 0 || sigIndex >= static_cast<int>(fields.size())) {
        throw std::out_of_range("no signature #" + std::to_string(sigIndex));
    }
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    const std::int64_t end = rangeEndOf(ctx, pdf, fields[static_cast<std::size_t>(sigIndex)]);
    std::string bytes(static_cast<std::size_t>(end), '\0');
    auto* data = reinterpret_cast<unsigned char*>(bytes.data());
    const size_t want = bytes.size();
    size_t got = 0;
    // Поток документа (файл или память): MuPDF всегда позиционирует его перед чтением сам.
    mu::call(ctx, [&] {
        fz_seek(ctx, pdf->file, 0, SEEK_SET);
        while (got < want) {
            const size_t n = fz_read(ctx, pdf->file, data + got, want - got);
            if (n == 0) {
                break;
            }
            got += n;
        }
    });
    if (got != want) {
        throw std::runtime_error("signed revision is truncated: " + std::to_string(got) + " of " +
                                 std::to_string(want) + " bytes");
    }
    return bytes;
}

fs::path signedRevisionBytes(Document& doc, int sigIndex, const fs::path& dir) {
    const std::string bytes = signedRevision(doc, sigIndex);
    const fs::path target = dir.empty() ? fs::temp_directory_path() / L"AcroBatya" : dir;
    fs::create_directories(target);
    const std::wstring stem = doc.path().empty() ? std::wstring(L"document") : doc.path().stem().wstring();
    const fs::path file = target / (stem + L".signed-" + std::to_wstring(sigIndex + 1) + L".pdf");
    paths::writeFile(file, bytes);
    return file;
}

}  // namespace ot::revisions
