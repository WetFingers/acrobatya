#include "core/doc/permissions.h"

#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot {

namespace {

bool bit(int p, int n) noexcept { return (static_cast<unsigned>(p) & (1u << (n - 1))) != 0; }

}  // namespace

Permissions Permissions::fromP(int p, int revision) noexcept {
    Permissions r;
    r.print = bit(p, kPermBitPrint);
    r.edit = bit(p, kPermBitModify);
    r.copy = bit(p, kPermBitCopy);
    r.annotate = bit(p, kPermBitAnnotate);
    if (revision >= 3) {
        r.fillForms = r.annotate || bit(p, kPermBitFillForms);
        r.accessibility = bit(p, kPermBitAccessibility);
        r.assemble = r.edit || bit(p, kPermBitAssemble);
        r.printHighRes = r.print && bit(p, kPermBitPrintHighRes);
    } else {
        r.fillForms = r.annotate;
        r.accessibility = r.copy;
        r.assemble = r.edit;
        r.printHighRes = r.print;
    }
    return r;
}

Permissions Permissions::of(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr || pdf->crypt == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    int p = -4;
    int revision = 0;
    // pdf_document_permissions: открытый паролем владельца — все биты (0xFFFFFFFC).
    mu::call(ctx, [&] {
        p = pdf_document_permissions(ctx, pdf);
        revision = pdf_crypt_revision(ctx, pdf->crypt);
    });
    return fromP(p, revision);
}

bool Permissions::allows(Perm perm) const noexcept {
    switch (perm) {
        case Perm::Print:
            return print;
        case Perm::PrintHighRes:
            return printHighRes;
        case Perm::Copy:
            return copy;
        case Perm::Edit:
            return edit;
        case Perm::Annotate:
            return annotate;
        case Perm::FillForms:
            return fillForms;
        case Perm::Accessibility:
            return accessibility;
        case Perm::Assemble:
            return assemble;
    }
    return true;
}

bool Permissions::restricted() const noexcept {
    for (Perm p : kAllPerms) {
        if (!allows(p)) {
            return true;
        }
    }
    return false;
}

std::string_view permName(Perm perm) noexcept {
    switch (perm) {
        case Perm::Print:
            return "print";
        case Perm::PrintHighRes:
            return "printHighRes";
        case Perm::Copy:
            return "copy";
        case Perm::Edit:
            return "edit";
        case Perm::Annotate:
            return "annotate";
        case Perm::FillForms:
            return "fillForms";
        case Perm::Accessibility:
            return "accessibility";
        case Perm::Assemble:
            return "assemble";
    }
    return "unknown";
}

}  // namespace ot
