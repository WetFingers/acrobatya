#include "core/sign/changes.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

namespace ot::sign {

namespace {

// Роли объектов (биты).
enum Role : unsigned {
    kDss = 1u << 0,
    kMeta = 1u << 1,
    kSigValue = 1u << 2,
    kField = 1u << 3,   // поле формы или виджет
    kFormAp = 1u << 4,  // вид виджетов, /DR, /XFA
    kAnnot = 1u << 5,   // комментарий и всё под ним
    kPage = 1u << 6,    // словарь страницы
    kAnnots = 1u << 7,  // массив /Annots страницы
    kPageTree = 1u << 8,
    kPageContent = 1u << 9,  // содержимое и ресурсы страницы
    kCatalog = 1u << 10,
    kAcroForm = 1u << 11,
    kOther = 1u << 12,
};
constexpr unsigned kStructural =
    kField | kAnnot | kPage | kAnnots | kPageTree | kCatalog | kAcroForm | kSigValue;

struct ObjInfo {
    unsigned roles = 0;
    int page = -1;
    std::string field;
};

// Обратные и перекрёстные ссылки (родитель, страница, соседи, /Reference /Data — каталог) не обходятся.
bool skipKey(fz_context* ctx, pdf_obj* key) {
    return pdf_name_eq(ctx, key, PDF_NAME(Parent)) || pdf_name_eq(ctx, key, PDF_NAME(P)) ||
           pdf_name_eq(ctx, key, PDF_NAME(Prev)) || pdf_name_eq(ctx, key, PDF_NAME(Next)) ||
           pdf_name_eq(ctx, key, PDF_NAME(First)) || pdf_name_eq(ctx, key, PDF_NAME(Last)) ||
           pdf_name_eq(ctx, key, PDF_NAME(IRT)) || pdf_name_eq(ctx, key, PDF_NAME(Dest)) ||
           pdf_name_eq(ctx, key, PDF_NAME(D)) || std::string_view(pdf_to_name(ctx, key)) == "Pg" ||
           std::string_view(pdf_to_name(ctx, key)) == "Obj" || pdf_name_eq(ctx, key, PDF_NAME(Data));
}

// Карта ролей по достижимости из каталога (всё — внутри mu::call).
class RoleMap {
public:
    RoleMap(fz_context* ctx, pdf_document* pdf) : ctx_(ctx), pdf_(pdf) { build(); }

    const ObjInfo* find(int num) const {
        const auto it = map_.find(num);
        return it == map_.end() ? nullptr : &it->second;
    }
    const std::vector<int>& pageNumbers() const { return pages_; }

private:
    void mark(int num, unsigned role, int page, const std::string& field) {
        ObjInfo& i = map_[num];
        i.roles |= role;
        if (i.page < 0) {
            i.page = page;
        }
        if (i.field.empty()) {
            i.field = field;
        }
    }

    // Обход подграфа: каждый косвенный объект помечается ролью; skipStructural — не входить в объекты со
    // структурной ролью (страницы и комментарии из дерева структуры, закладок).
    void walk(pdf_obj* start, unsigned role, int page, const std::string& field, bool skipStructural = false,
              bool skipAnnots = false) {
        std::vector<pdf_obj*> stack{start};
        while (!stack.empty()) {
            pdf_obj* o = stack.back();
            stack.pop_back();
            if (o == nullptr) {
                continue;
            }
            if (pdf_is_indirect(ctx_, o)) {
                const int num = pdf_to_num(ctx_, o);
                if (num <= 0 || !visited_.insert({num, role}).second) {
                    continue;
                }
                if (skipStructural) {
                    const ObjInfo* known = find(num);
                    if (known != nullptr && (known->roles & kStructural) != 0) {
                        continue;
                    }
                }
                mark(num, role, page, field);
                o = pdf_resolve_indirect(ctx_, o);
                if (o == nullptr) {
                    continue;
                }
            }
            if (pdf_is_dict(ctx_, o)) {
                const int n = pdf_dict_len(ctx_, o);
                for (int i = 0; i < n; ++i) {
                    pdf_obj* key = pdf_dict_get_key(ctx_, o, i);
                    if (skipKey(ctx_, key) || (skipAnnots && pdf_name_eq(ctx_, key, PDF_NAME(Annots)))) {
                        continue;
                    }
                    stack.push_back(pdf_dict_get_val(ctx_, o, i));
                }
            } else if (pdf_is_array(ctx_, o)) {
                const int n = pdf_array_len(ctx_, o);
                for (int i = 0; i < n; ++i) {
                    stack.push_back(pdf_array_get(ctx_, o, i));
                }
            }
        }
    }

    void markOne(pdf_obj* o, unsigned role, int page, const std::string& field) {
        if (pdf_is_indirect(ctx_, o)) {
            mark(pdf_to_num(ctx_, o), role, page, field);
        }
    }

    std::string fieldName(pdf_obj* f) {
        char* name = pdf_load_field_name(ctx_, f);
        std::string out = name != nullptr ? name : "";
        fz_free(ctx_, name);
        return out;
    }

    void walkField(pdf_obj* f, int depth) {
        if (depth > 64 || f == nullptr) {
            return;
        }
        const std::string name = fieldName(f);
        markOne(f, kField, -1, name);
        pdf_obj* ft = pdf_dict_get_inheritable(ctx_, f, PDF_NAME(FT));
        pdf_obj* v = pdf_dict_get(ctx_, f, PDF_NAME(V));
        if (pdf_name_eq(ctx_, ft, PDF_NAME(Sig))) {
            walk(v, kSigValue, -1, name);
        } else if (pdf_is_indirect(ctx_, v)) {
            walk(v, kField, -1, name);
        }
        walk(pdf_dict_get(ctx_, f, PDF_NAME(AP)), kFormAp, -1, name);
        walk(pdf_dict_get(ctx_, f, PDF_NAME(MK)), kFormAp, -1, name);
        walk(pdf_dict_get(ctx_, f, PDF_NAME(Lock)), kField, -1, name);
        pdf_obj* kids = pdf_dict_get(ctx_, f, PDF_NAME(Kids));
        markOne(kids, kField, -1, name);
        for (int i = 0; i < pdf_array_len(ctx_, kids); ++i) {
            walkField(pdf_array_get(ctx_, kids, i), depth + 1);
        }
    }

    void walkPageTree(pdf_obj* node, int depth) {
        if (depth > 64 || node == nullptr) {
            return;
        }
        if (pdf_name_eq(ctx_, pdf_dict_get(ctx_, node, PDF_NAME(Type)), PDF_NAME(Pages))) {
            markOne(node, kPageTree, -1, {});
            pdf_obj* kids = pdf_dict_get(ctx_, node, PDF_NAME(Kids));
            markOne(kids, kPageTree, -1, {});
            for (int i = 0; i < pdf_array_len(ctx_, kids); ++i) {
                walkPageTree(pdf_array_get(ctx_, kids, i), depth + 1);
            }
        }
    }

    void build() {
        pdf_obj* trailer = pdf_trailer(ctx_, pdf_);
        walk(pdf_dict_get(ctx_, trailer, PDF_NAME(Info)), kMeta, -1, {});
        pdf_obj* rootRef = pdf_dict_get(ctx_, trailer, PDF_NAME(Root));
        markOne(rootRef, kCatalog, -1, {});
        pdf_obj* root = pdf_resolve_indirect(ctx_, rootRef);
        walk(pdf_dict_get(ctx_, root, PDF_NAME(Metadata)), kMeta, -1, {});
        walk(pdf_dict_gets(ctx_, root, "DSS"), kDss, -1, {});
        walk(pdf_dict_gets(ctx_, root, "Extensions"), kMeta, -1, {});

        // Страницы: словарь, /Annots, комментарии и виджеты, содержимое и ресурсы.
        walkPageTree(pdf_dict_get(ctx_, root, PDF_NAME(Pages)), 0);
        const int count = pdf_count_pages(ctx_, pdf_);
        for (int i = 0; i < count; ++i) {
            pdf_obj* page = pdf_lookup_page_obj(ctx_, pdf_, i);
            pages_.push_back(pdf_to_num(ctx_, page));
            markOne(page, kPage, i, {});
            pdf_obj* annots = pdf_dict_get(ctx_, page, PDF_NAME(Annots));
            markOne(annots, kAnnots, i, {});
            for (int k = 0; k < pdf_array_len(ctx_, annots); ++k) {
                pdf_obj* a = pdf_array_get(ctx_, annots, k);
                if (pdf_name_eq(ctx_, pdf_dict_get(ctx_, a, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
                    markOne(a, kField, i, fieldName(a));
                    walk(pdf_dict_get(ctx_, a, PDF_NAME(AP)), kFormAp, i, fieldName(a));
                } else {
                    walk(a, kAnnot, i, {});
                }
            }
        }

        // Форма: /AcroForm, поля, значения подписей, вид.
        pdf_obj* formRef = pdf_dict_get(ctx_, root, PDF_NAME(AcroForm));
        markOne(formRef, kAcroForm, -1, {});
        pdf_obj* form = pdf_resolve_indirect(ctx_, formRef);
        walk(pdf_dict_get(ctx_, form, PDF_NAME(DR)), kFormAp, -1, {});
        walk(pdf_dict_get(ctx_, form, PDF_NAME(XFA)), kFormAp, -1, {});
        pdf_obj* fields = pdf_dict_get(ctx_, form, PDF_NAME(Fields));
        markOne(fields, kAcroForm, -1, {});
        for (int i = 0; i < pdf_array_len(ctx_, fields); ++i) {
            walkField(pdf_array_get(ctx_, fields, i), 0);
        }

        // Содержимое страниц — после форм: общие с видом ресурсы — строже.
        for (int i = 0; i < count; ++i) {
            walk(pdf_lookup_page_obj(ctx_, pdf_, i), kPageContent, i, {}, false, true);
        }
        // Прочее из каталога, кроме разобранного; в страницы и комментарии (структура, закладки) не входить.
        const int n = pdf_dict_len(ctx_, root);
        for (int k = 0; k < n; ++k) {
            pdf_obj* key = pdf_dict_get_key(ctx_, root, k);
            if (pdf_name_eq(ctx_, key, PDF_NAME(Pages)) || pdf_name_eq(ctx_, key, PDF_NAME(AcroForm)) ||
                pdf_name_eq(ctx_, key, PDF_NAME(Metadata)) ||
                std::string_view(pdf_to_name(ctx_, key)) == "Extensions" ||
                pdf_name_eq(ctx_, key, PDF_NAME(Type)) || pdf_name_eq(ctx_, key, PDF_NAME(Version)) ||
                std::string_view(pdf_to_name(ctx_, key)) == "DSS") {
                continue;
            }
            walk(pdf_dict_get_val(ctx_, root, k), kOther, -1, {}, true);
        }
    }

    fz_context* ctx_;
    pdf_document* pdf_;
    std::unordered_map<int, ObjInfo> map_;
    std::set<std::pair<int, unsigned>> visited_;
    std::vector<int> pages_;
};

// Значения равны: словари — по ключам (порядок не важен), массивы — поэлементно, ссылки — по номеру.
bool same(fz_context* ctx, pdf_obj* a, pdf_obj* b, int depth = 0) {
    if (a == b) {
        return true;
    }
    if (a == nullptr || b == nullptr || depth > 100) {
        return a == b;
    }
    if (pdf_is_indirect(ctx, a) || pdf_is_indirect(ctx, b)) {
        return pdf_is_indirect(ctx, a) && pdf_is_indirect(ctx, b) && pdf_to_num(ctx, a) == pdf_to_num(ctx, b);
    }
    if (pdf_is_dict(ctx, a)) {
        if (!pdf_is_dict(ctx, b) || pdf_dict_len(ctx, a) != pdf_dict_len(ctx, b)) {
            return false;
        }
        const int n = pdf_dict_len(ctx, a);
        for (int i = 0; i < n; ++i) {
            pdf_obj* key = pdf_dict_get_key(ctx, a, i);
            if (!same(ctx, pdf_dict_get_val(ctx, a, i), pdf_dict_get(ctx, b, key), depth + 1)) {
                return false;
            }
        }
        return true;
    }
    if (pdf_is_array(ctx, a)) {
        if (!pdf_is_array(ctx, b) || pdf_array_len(ctx, a) != pdf_array_len(ctx, b)) {
            return false;
        }
        const int n = pdf_array_len(ctx, a);
        for (int i = 0; i < n; ++i) {
            if (!same(ctx, pdf_array_get(ctx, a, i), pdf_array_get(ctx, b, i), depth + 1)) {
                return false;
            }
        }
        return true;
    }
    return pdf_objcmp(ctx, a, b) == 0;
}

std::vector<int> refNumbers(fz_context* ctx, pdf_obj* array) {
    std::vector<int> out;
    for (int i = 0; i < pdf_array_len(ctx, array); ++i) {
        pdf_obj* o = pdf_array_get(ctx, array, i);
        if (pdf_is_indirect(ctx, o)) {
            out.push_back(pdf_to_num(ctx, o));
        }
    }
    return out;
}

ChangeLevel levelOf(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::Dss:
        case ChangeKind::DocTimestamp:
        case ChangeKind::Metadata:
            return ChangeLevel::LtvOnly;
        case ChangeKind::Signature:
        case ChangeKind::SignatureField:
        case ChangeKind::FormField:
            return ChangeLevel::FormFilling;
        case ChangeKind::Annotation:
            return ChangeLevel::Annotations;
        case ChangeKind::LockedField:
        case ChangeKind::Page:
        case ChangeKind::FieldStructure:
        case ChangeKind::Other:
            return ChangeLevel::Other;
    }
    return ChangeLevel::Other;
}

class Analyzer {
public:
    Analyzer(fz_context* ctx, pdf_document* oldDoc, pdf_document* newDoc, const ChangeRules& rules)
        : ctx_(ctx),
          old_(oldDoc),
          new_(newDoc),
          rules_(rules),
          oldRoles_(ctx, oldDoc),
          newRoles_(ctx, newDoc) {}

    void run(ChangeReport& report) {
        report_ = &report;
        const int oldVersions = pdf_count_versions(ctx_, old_);
        const int newVersions = pdf_count_versions(ctx_, new_);
        const int after = newVersions - oldVersions;
        const bool all = after <= 0 || pdf_was_repaired(ctx_, new_) != 0;
        const int len = std::max(pdf_xref_len(ctx_, old_), pdf_xref_len(ctx_, new_));
        for (int num = 1; num < len; ++num) {
            if (!all) {
                pdf_obj* ref = pdf_new_indirect(ctx_, new_, num, 0);
                const int v = pdf_find_version_for_obj(ctx_, new_, ref);
                pdf_drop_obj(ctx_, ref);
                if (v < 0 || v >= after) {
                    continue;  // определён в подписанной ревизии или раньше
                }
            }
            compareObject(num);
        }
    }

private:
    void add(ChangeKind kind, int object, int page, std::string field, std::string detail) {
        for (const Change& c : report_->changes) {
            if (c.kind == kind && c.object == object && c.field == field) {
                return;
            }
        }
        Change c;
        c.kind = kind;
        c.object = object;
        c.page = page;
        c.field = std::move(field);
        c.detail = std::move(detail);
        report_->changes.push_back(std::move(c));
    }

    pdf_obj* load(pdf_document* doc, int num) {
        if (num >= pdf_xref_len(ctx_, doc) || !pdf_object_exists(ctx_, doc, num)) {
            return nullptr;
        }
        pdf_obj* ref = pdf_new_indirect(ctx_, doc, num, 0);
        pdf_obj* obj = pdf_resolve_indirect(ctx_, ref);
        pdf_drop_obj(ctx_, ref);
        return pdf_is_null(ctx_, obj) ? nullptr : obj;
    }

    bool sameStream(int num) {
        mu::BufferHandle a(ctx_);
        mu::BufferHandle b(ctx_);
        a.reset(pdf_load_raw_stream_number(ctx_, old_, num));
        b.reset(pdf_load_raw_stream_number(ctx_, new_, num));
        unsigned char* da = nullptr;
        unsigned char* db = nullptr;
        const std::size_t la = fz_buffer_storage(ctx_, a, &da);
        const std::size_t lb = fz_buffer_storage(ctx_, b, &db);
        return la == lb && (la == 0 || std::equal(da, da + la, db));
    }

    void compareObject(int num) {
        pdf_obj* o = load(old_, num);
        pdf_obj* n = load(new_, num);
        if (o == nullptr && n == nullptr) {
            return;
        }
        if (o != nullptr && n != nullptr) {
            const bool streams =
                pdf_obj_num_is_stream(ctx_, old_, num) && pdf_obj_num_is_stream(ctx_, new_, num);
            if (same(ctx_, o, n) && (!streams || sameStream(num))) {
                return;  // переписан без изменений
            }
        }
        const ObjInfo* info = newRoles_.find(num);
        if (n == nullptr || info == nullptr) {
            info = oldRoles_.find(num);
        }
        if (info == nullptr) {
            return;  // сирота: не виден ни в одной ревизии
        }
        classify(num, *info, o, n);
    }

    std::string fieldTypeName(pdf_obj* f) {
        pdf_obj* ft = pdf_dict_get_inheritable(ctx_, f, PDF_NAME(FT));
        return ft != nullptr ? pdf_to_name(ctx_, ft) : "";
    }

    bool isDocTimestampValue(pdf_obj* v) {
        return std::string_view(pdf_to_name(ctx_, pdf_dict_get(ctx_, v, PDF_NAME(Type)))) == "DocTimeStamp" ||
               std::string_view(pdf_to_name(ctx_, pdf_dict_get(ctx_, v, PDF_NAME(SubFilter)))) ==
                   "ETSI.RFC3161";
    }

    // Новое поле или виджет (о поле — по /AcroForm, о виджете — по /Annots).
    void newField(int num, pdf_obj* f, int page, const std::string& name) {
        if (fieldTypeName(f) == "Sig") {
            pdf_obj* v = pdf_dict_get(ctx_, f, PDF_NAME(V));
            if (v == nullptr) {
                add(ChangeKind::SignatureField, num, page, name, "signature field added");
            } else if (isDocTimestampValue(v)) {
                add(ChangeKind::DocTimestamp, num, page, name, "document time-stamp added");
            } else {
                add(ChangeKind::Signature, num, page, name, "signature added");
            }
        } else {
            add(ChangeKind::FieldStructure, num, page, name, "form field added");
        }
    }

    void classify(int num, const ObjInfo& info, pdf_obj* o, pdf_obj* n) {
        const unsigned r = info.roles;
        const std::string what = o == nullptr ? "added" : n == nullptr ? "removed" : "changed";
        if ((r & kCatalog) != 0) {
            catalogDiff(num, o, n);
        } else if ((r & kPage) != 0) {
            pageDiff(num, info.page, o, n);
        } else if ((r & kAnnots) != 0) {
            annotsDiff(num, info.page, o, n);
        } else if ((r & kAcroForm) != 0) {
            acroFormDiff(num, o, n);
        } else if ((r & (kPageTree | kPageContent)) != 0) {
            add(ChangeKind::Page, num, info.page, {},
                (r & kPageTree) != 0 ? "page tree " + what : "page content or resources " + what);
        } else if ((r & kOther) != 0) {
            add(ChangeKind::Other, num, -1, {}, "document object " + std::to_string(num) + " " + what);
        } else if ((r & kField) != 0) {
            if (o == nullptr || n == nullptr) {
                if (n != nullptr) {
                    newField(num, n, info.page, info.field);
                } else {
                    add(ChangeKind::FieldStructure, num, info.page, info.field, "form field removed");
                }
            } else {
                fieldDiff(num, info, o, n);
            }
        } else if ((r & kSigValue) != 0) {
            if (o == nullptr && n != nullptr) {
                add(isDocTimestampValue(n) ? ChangeKind::DocTimestamp : ChangeKind::Signature, num, -1,
                    info.field, isDocTimestampValue(n) ? "document time-stamp value" : "signature value");
            } else {
                add(ChangeKind::Other, num, -1, info.field, "signature dictionary " + what);
            }
        } else if ((r & kAnnot) != 0) {
            add(ChangeKind::Annotation, num, info.page, {}, "annotation " + what);
        } else if ((r & kFormAp) != 0) {
            formFieldChange(num, info.page, info.field, "field appearance " + what);
        } else if ((r & kDss) != 0) {
            add(ChangeKind::Dss, num, -1, {}, "validation data (DSS) " + what);
        } else if ((r & kMeta) != 0) {
            add(ChangeKind::Metadata, num, -1, {}, "metadata " + what);
        }
    }

    void formFieldChange(int num, int page, const std::string& field, const std::string& detail) {
        if (!field.empty() && isFieldLocked(rules_.locks, field)) {
            add(ChangeKind::LockedField, num, page, field, "locked field: " + detail);
        } else {
            add(ChangeKind::FormField, num, page, field, detail);
        }
    }

    void catalogDiff(int num, pdf_obj* o, pdf_obj* n) {
        if (o == nullptr || n == nullptr) {
            add(ChangeKind::Other, num, -1, {}, "document catalog replaced");
            return;
        }
        std::set<std::string> keys;
        for (pdf_obj* d : {o, n}) {
            for (int i = 0; i < pdf_dict_len(ctx_, d); ++i) {
                keys.insert(pdf_to_name(ctx_, pdf_dict_get_key(ctx_, d, i)));
            }
        }
        for (const std::string& k : keys) {
            pdf_obj* a = pdf_dict_gets(ctx_, o, k.c_str());
            pdf_obj* b = pdf_dict_gets(ctx_, n, k.c_str());
            if (same(ctx_, a, b)) {
                continue;
            }
            if (k == "DSS") {
                add(ChangeKind::Dss, num, -1, {}, "/DSS added or replaced");
            } else if (k == "Extensions" || k == "Version" || k == "Metadata") {
                add(ChangeKind::Metadata, num, -1, {}, "catalog /" + k + " changed");
            } else if (k == "AcroForm") {
                if (pdf_is_indirect(ctx_, a) && pdf_is_indirect(ctx_, b) &&
                    pdf_to_num(ctx_, a) == pdf_to_num(ctx_, b)) {
                    continue;
                }
                acroFormDiff(num, pdf_resolve_indirect(ctx_, a), pdf_resolve_indirect(ctx_, b));
            } else {
                add(ChangeKind::Other, num, -1, {}, "catalog /" + k + " changed");
            }
        }
    }

    void acroFormDiff(int num, pdf_obj* o, pdf_obj* n) {
        std::set<std::string> keys;
        for (pdf_obj* d : {o, n}) {
            for (int i = 0; d != nullptr && i < pdf_dict_len(ctx_, d); ++i) {
                keys.insert(pdf_to_name(ctx_, pdf_dict_get_key(ctx_, d, i)));
            }
        }
        for (const std::string& k : keys) {
            pdf_obj* a = o != nullptr ? pdf_dict_gets(ctx_, o, k.c_str()) : nullptr;
            pdf_obj* b = n != nullptr ? pdf_dict_gets(ctx_, n, k.c_str()) : nullptr;
            if (same(ctx_, a, b)) {
                continue;
            }
            if (k == "Fields") {
                fieldsDiff(num, pdf_resolve_indirect(ctx_, a), pdf_resolve_indirect(ctx_, b));
            } else if (k == "SigFlags" || k == "NeedAppearances" || k == "DA" || k == "DR" || k == "XFA" ||
                       k == "Q") {
                add(ChangeKind::FormField, num, -1, {}, "form /" + k + " changed");
            } else {
                add(ChangeKind::Other, num, -1, {}, "form /" + k + " changed");
            }
        }
    }

    void fieldsDiff(int num, pdf_obj* o, pdf_obj* n) {
        const std::vector<int> before = refNumbers(ctx_, o);
        const std::vector<int> after = refNumbers(ctx_, n);
        for (const int f : after) {
            if (std::find(before.begin(), before.end(), f) == before.end()) {
                pdf_obj* field = load(new_, f);
                const ObjInfo* info = newRoles_.find(f);
                newField(f, field, info != nullptr ? info->page : -1,
                         info != nullptr ? info->field : std::string());
            }
        }
        for (const int f : before) {
            if (std::find(after.begin(), after.end(), f) == after.end()) {
                const ObjInfo* info = oldRoles_.find(f);
                add(ChangeKind::FieldStructure, f, -1, info != nullptr ? info->field : std::string(),
                    "form field removed");
            }
        }
        (void)num;
    }

    void fieldDiff(int num, const ObjInfo& info, pdf_obj* o, pdf_obj* n) {
        const bool sig = fieldTypeName(n) == "Sig";
        std::set<std::string> keys;
        for (pdf_obj* d : {o, n}) {
            for (int i = 0; i < pdf_dict_len(ctx_, d); ++i) {
                keys.insert(pdf_to_name(ctx_, pdf_dict_get_key(ctx_, d, i)));
            }
        }
        for (const std::string& k : keys) {
            pdf_obj* a = pdf_dict_gets(ctx_, o, k.c_str());
            pdf_obj* b = pdf_dict_gets(ctx_, n, k.c_str());
            if (same(ctx_, a, b)) {
                continue;
            }
            if (k == "V" && sig) {
                if (a == nullptr) {
                    const bool dts = isDocTimestampValue(b);
                    if (!dts && isFieldLocked(rules_.locks, info.field)) {
                        add(ChangeKind::LockedField, num, info.page, info.field,
                            "locked signature field signed");
                    } else {
                        add(dts ? ChangeKind::DocTimestamp : ChangeKind::Signature, num, info.page,
                            info.field, dts ? "document time-stamp added" : "signature field signed");
                    }
                } else {
                    add(ChangeKind::Other, num, info.page, info.field, "signed field value replaced");
                }
            } else if (k == "V" || k == "AS" || k == "AP" || k == "MK" || k == "DA" || k == "Q" ||
                       k == "RV" || k == "M") {
                formFieldChange(num, info.page, info.field, "/" + k + " changed");
            } else if (k == "Ff") {
                const int before = pdf_to_int(ctx_, a);
                const int after = pdf_to_int(ctx_, b);
                if ((before | 1) == after) {
                    add(ChangeKind::FormField, num, info.page, info.field, "field made read-only");
                } else {
                    add(ChangeKind::FieldStructure, num, info.page, info.field, "field flags changed");
                }
            } else if (k == "Lock" || k == "SV" || k == "StructParent" || k == "P") {
                add(ChangeKind::FormField, num, info.page, info.field, "/" + k + " changed");
            } else if (k == "Kids") {
                const std::vector<int> before = refNumbers(ctx_, a);
                const std::vector<int> after = refNumbers(ctx_, b);
                bool onlyAdded = std::all_of(before.begin(), before.end(), [&](int x) {
                    return std::find(after.begin(), after.end(), x) != after.end();
                });
                for (const int kid : after) {
                    if (std::find(before.begin(), before.end(), kid) == before.end()) {
                        pdf_obj* f = load(new_, kid);
                        const ObjInfo* ki = newRoles_.find(kid);
                        newField(kid, f, ki != nullptr ? ki->page : -1,
                                 ki != nullptr ? ki->field : info.field);
                    }
                }
                if (!onlyAdded) {
                    add(ChangeKind::FieldStructure, num, info.page, info.field, "field widgets removed");
                }
            } else {
                add(ChangeKind::FieldStructure, num, info.page, info.field, "field /" + k + " changed");
            }
        }
    }

    void pageDiff(int num, int page, pdf_obj* o, pdf_obj* n) {
        if (o == nullptr || n == nullptr) {
            add(ChangeKind::Page, num, page, {}, o == nullptr ? "page added" : "page removed");
            return;
        }
        std::set<std::string> keys;
        for (pdf_obj* d : {o, n}) {
            for (int i = 0; i < pdf_dict_len(ctx_, d); ++i) {
                keys.insert(pdf_to_name(ctx_, pdf_dict_get_key(ctx_, d, i)));
            }
        }
        for (const std::string& k : keys) {
            pdf_obj* a = pdf_dict_gets(ctx_, o, k.c_str());
            pdf_obj* b = pdf_dict_gets(ctx_, n, k.c_str());
            if (same(ctx_, a, b)) {
                continue;
            }
            if (k == "Annots") {
                if (pdf_is_indirect(ctx_, a) && pdf_is_indirect(ctx_, b) &&
                    pdf_to_num(ctx_, a) == pdf_to_num(ctx_, b)) {
                    continue;  // сам массив — свой объект
                }
                annotsDiff(num, page, pdf_resolve_indirect(ctx_, a), pdf_resolve_indirect(ctx_, b));
            } else {
                add(ChangeKind::Page, num, page, {}, "page /" + k + " changed");
            }
        }
    }

    void annotsDiff(int num, int page, pdf_obj* o, pdf_obj* n) {
        const std::vector<int> before = refNumbers(ctx_, o);
        const std::vector<int> after = refNumbers(ctx_, n);
        for (const int a : after) {
            if (std::find(before.begin(), before.end(), a) != before.end()) {
                continue;
            }
            pdf_obj* annot = load(new_, a);
            if (pdf_name_eq(ctx_, pdf_dict_get(ctx_, annot, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
                const ObjInfo* info = newRoles_.find(a);
                newField(a, annot, page, info != nullptr ? info->field : std::string());
            } else {
                add(ChangeKind::Annotation, a, page, {}, "annotation added");
            }
        }
        for (const int a : before) {
            if (std::find(after.begin(), after.end(), a) != after.end()) {
                continue;
            }
            pdf_obj* annot = load(old_, a);
            if (pdf_name_eq(ctx_, pdf_dict_get(ctx_, annot, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
                add(ChangeKind::FieldStructure, a, page, {}, "widget removed");
            } else {
                add(ChangeKind::Annotation, a, page, {}, "annotation removed");
            }
        }
        (void)num;
    }

    fz_context* ctx_;
    pdf_document* old_;
    pdf_document* new_;
    const ChangeRules& rules_;
    RoleMap oldRoles_;
    RoleMap newRoles_;
    ChangeReport* report_ = nullptr;
};

}  // namespace

const char* changeKindName(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::Dss:
            return "Dss";
        case ChangeKind::DocTimestamp:
            return "DocTimestamp";
        case ChangeKind::Metadata:
            return "Metadata";
        case ChangeKind::Signature:
            return "Signature";
        case ChangeKind::SignatureField:
            return "SignatureField";
        case ChangeKind::FormField:
            return "FormField";
        case ChangeKind::LockedField:
            return "LockedField";
        case ChangeKind::Annotation:
            return "Annotation";
        case ChangeKind::Page:
            return "Page";
        case ChangeKind::FieldStructure:
            return "FieldStructure";
        case ChangeKind::Other:
            return "Other";
    }
    return "?";
}

const char* changeLevelName(ChangeLevel level) {
    switch (level) {
        case ChangeLevel::None:
            return "None";
        case ChangeLevel::LtvOnly:
            return "LtvOnly";
        case ChangeLevel::FormFilling:
            return "FormFilling";
        case ChangeLevel::Annotations:
            return "Annotations";
        case ChangeLevel::Other:
            return "Other";
    }
    return "?";
}

ChangeLevel permittedLevel(MdpPermission docMdp) {
    switch (docMdp) {
        case MdpPermission::NoChanges:
            return ChangeLevel::LtvOnly;
        case MdpPermission::FormFilling:
            return ChangeLevel::FormFilling;
        case MdpPermission::Annotations:
        case MdpPermission::None:
            return ChangeLevel::Annotations;
    }
    return ChangeLevel::Annotations;
}

ChangeReport analyzeChanges(Document& doc, std::int64_t signedEnd, const ChangeRules& rules) {
    ChangeReport report;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return report;
    }
    fz_context* ctx = doc.context().get();
    std::int64_t size = 0;
    mu::call(ctx, [&] { size = pdf->file_size; });
    if (signedEnd <= 0 || signedEnd > size) {
        throw std::invalid_argument("signed revision end " + std::to_string(signedEnd) +
                                    " is outside the file");
    }
    // Подписанная ревизия — байты [0, signedEnd) потока документа во временный документ той же семьи.
    std::string bytes(static_cast<std::size_t>(signedEnd), '\0');
    std::size_t got = 0;
    mu::call(ctx, [&] {
        fz_seek(ctx, pdf->file, 0, SEEK_SET);
        while (got < bytes.size()) {
            const std::size_t n = fz_read(
                ctx, pdf->file, reinterpret_cast<unsigned char*>(bytes.data()) + got, bytes.size() - got);
            if (n == 0) {
                break;
            }
            got += n;
        }
    });
    if (got != bytes.size()) {
        throw std::runtime_error("signed revision is truncated");
    }
    OpenResult old = Document::openMemory(doc.context(), std::move(bytes));
    if (!old.ok || !old.document || old.document->pdf() == nullptr) {
        throw std::runtime_error("signed revision does not open: " + old.error.message);
    }
    mu::call(ctx, [&] {
        Analyzer analyzer(ctx, old.document->pdf(), pdf, rules);
        analyzer.run(report);
    });
    const ChangeLevel allowed = permittedLevel(rules.docMdp);
    for (Change& c : report.changes) {
        const ChangeLevel l = levelOf(c.kind);
        c.allowed = c.kind != ChangeKind::LockedField && static_cast<int>(l) <= static_cast<int>(allowed);
        report.allowed = report.allowed && c.allowed;
        if (static_cast<int>(l) > static_cast<int>(report.level)) {
            report.level = l;
        }
    }
    report.modified = !report.changes.empty();
    spdlog::debug("changes: {} after byte {}: {} ({} changes)", report.allowed ? "allowed" : "not allowed",
                  signedEnd, changeLevelName(report.level), report.changes.size());
    return report;
}

}  // namespace ot::sign
