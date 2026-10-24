#include "core/sign/docmdp.h"

#include <algorithm>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

namespace ot::sign {

namespace {

std::string textOf(fz_context* ctx, pdf_obj* o) {
    const char* s = pdf_to_text_string(ctx, o);
    return s != nullptr ? s : "";
}

// Правило /Lock или /TransformParams FieldMDP (внутри mu::call).
void addLock(fz_context* ctx, pdf_obj* spec, std::vector<FieldLock>& out) {
    if (!pdf_is_dict(ctx, spec)) {
        return;
    }
    pdf_obj* action = pdf_dict_get(ctx, spec, PDF_NAME(Action));
    FieldLock lock;
    if (pdf_name_eq(ctx, action, PDF_NAME(All))) {
        lock.action = FieldLock::Action::All;
    } else if (pdf_name_eq(ctx, action, PDF_NAME(Include))) {
        lock.action = FieldLock::Action::Include;
    } else if (pdf_name_eq(ctx, action, PDF_NAME(Exclude))) {
        lock.action = FieldLock::Action::Exclude;
    } else {
        return;
    }
    pdf_obj* fields = pdf_dict_get(ctx, spec, PDF_NAME(Fields));
    for (int i = 0; i < pdf_array_len(ctx, fields); ++i) {
        lock.fields.push_back(textOf(ctx, pdf_array_get(ctx, fields, i)));
    }
    out.push_back(std::move(lock));
}

MdpPermission fromP(int p) {
    switch (p) {
        case 1:
            return MdpPermission::NoChanges;
        case 3:
            return MdpPermission::Annotations;
        default:
            return MdpPermission::FormFilling;  // 2 и нет /P (ISO 32000-1 12.8.2.2)
    }
}

}  // namespace

MdpPermission signatureDocMdp(fz_context* ctx, pdf_obj* v) {
    MdpPermission out = MdpPermission::None;
    mu::call(ctx, [&] {
        pdf_obj* refs = pdf_dict_get(ctx, v, PDF_NAME(Reference));
        for (int i = 0; i < pdf_array_len(ctx, refs); ++i) {
            pdf_obj* ref = pdf_array_get(ctx, refs, i);
            if (pdf_name_eq(ctx, pdf_dict_get(ctx, ref, PDF_NAME(TransformMethod)), PDF_NAME(DocMDP))) {
                pdf_obj* p = pdf_dict_getp(ctx, ref, "TransformParams/P");
                out = fromP(pdf_is_int(ctx, p) ? pdf_to_int(ctx, p) : 2);
            }
        }
    });
    return out;
}

DocMdp readDocMdp(fz_context* ctx, pdf_document* pdf) {
    DocMdp out;
    pdf_obj* v = nullptr;
    mu::call(ctx, [&] {
        v = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/Perms/DocMDP");
        out.signatureObject = pdf_is_indirect(ctx, v) ? pdf_to_num(ctx, v) : 0;
    });
    bool dict = false;
    mu::call(ctx, [&] { dict = pdf_is_dict(ctx, v) != 0; });
    if (!dict) {
        return out;
    }
    out.permission = signatureDocMdp(ctx, v);
    if (out.permission == MdpPermission::None) {
        out.permission = MdpPermission::FormFilling;  // /Perms /DocMDP без /Reference: P по умолчанию
    }
    // Поле сертифицирующей подписи — то, чьё /V — этот словарь.
    mu::call(ctx, [&] {
        pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
        std::vector<pdf_obj*> stack;
        for (int i = 0; i < pdf_array_len(ctx, fields); ++i) {
            stack.push_back(pdf_array_get(ctx, fields, i));
        }
        int guard = 0;
        while (!stack.empty() && out.field.empty() && ++guard < 100000) {
            pdf_obj* f = stack.back();
            stack.pop_back();
            pdf_obj* fv = pdf_dict_get(ctx, f, PDF_NAME(V));
            if (fv != nullptr && pdf_is_indirect(ctx, fv) && pdf_to_num(ctx, fv) == out.signatureObject) {
                char* name = pdf_load_field_name(ctx, f);
                out.field = name != nullptr ? name : "";
                fz_free(ctx, name);
            }
            pdf_obj* kids = pdf_dict_get(ctx, f, PDF_NAME(Kids));
            for (int i = 0; i < pdf_array_len(ctx, kids); ++i) {
                stack.push_back(pdf_array_get(ctx, kids, i));
            }
        }
    });
    return out;
}

DocMdp readDocMdp(Document& doc) {
    pdf_document* pdf = doc.pdf();
    return pdf == nullptr ? DocMdp{} : readDocMdp(doc.context().get(), pdf);
}

std::vector<FieldLock> fieldLocks(fz_context* ctx, pdf_obj* field, pdf_obj* v) {
    std::vector<FieldLock> out;
    mu::call(ctx, [&] {
        pdf_obj* refs = pdf_dict_get(ctx, v, PDF_NAME(Reference));
        for (int i = 0; i < pdf_array_len(ctx, refs); ++i) {
            pdf_obj* ref = pdf_array_get(ctx, refs, i);
            if (pdf_name_eq(ctx, pdf_dict_get(ctx, ref, PDF_NAME(TransformMethod)), PDF_NAME(FieldMDP))) {
                addLock(ctx, pdf_dict_get(ctx, ref, PDF_NAME(TransformParams)), out);
            }
        }
        addLock(ctx, pdf_dict_get(ctx, field, PDF_NAME(Lock)), out);
    });
    return out;
}

bool isFieldLocked(const std::vector<FieldLock>& locks, std::string_view fullName) {
    for (const FieldLock& l : locks) {
        const bool listed = std::find(l.fields.begin(), l.fields.end(), fullName) != l.fields.end();
        if (l.action == FieldLock::Action::All || (l.action == FieldLock::Action::Include && listed) ||
            (l.action == FieldLock::Action::Exclude && !listed)) {
            return true;
        }
    }
    return false;
}

const char* mdpPermissionName(MdpPermission p) {
    switch (p) {
        case MdpPermission::None:
            return "None";
        case MdpPermission::NoChanges:
            return "NoChanges";
        case MdpPermission::FormFilling:
            return "FormFilling";
        case MdpPermission::Annotations:
            return "Annotations";
    }
    return "?";
}

}  // namespace ot::sign
