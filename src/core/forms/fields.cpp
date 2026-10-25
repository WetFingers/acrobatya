// Модель полей формы (см. fields.h).
#include "core/forms/fields.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/forms/forms_internal.h"
#include "core/forms/js_bridge.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/utf8.h"

namespace ot::forms {

const char* fieldTypeName(FieldType type) noexcept {
    switch (type) {
        case FieldType::PushButton:
            return "button";
        case FieldType::CheckBox:
            return "checkbox";
        case FieldType::RadioButton:
            return "radiobutton";
        case FieldType::Text:
            return "text";
        case FieldType::ComboBox:
            return "combobox";
        case FieldType::ListBox:
            return "listbox";
        case FieldType::Signature:
            return "signature";
        case FieldType::Unknown:
            break;
    }
    return "unknown";
}

const char* actionKindName(ActionKind kind) noexcept {
    switch (kind) {
        case ActionKind::ResetForm:
            return "ResetForm";
        case ActionKind::SubmitForm:
            return "SubmitForm";
        case ActionKind::ImportData:
            return "ImportData";
        case ActionKind::JavaScript:
            return "JavaScript";
        case ActionKind::GoTo:
            return "GoTo";
        case ActionKind::GoToR:
            return "GoToR";
        case ActionKind::Uri:
            return "URI";
        case ActionKind::Named:
            return "Named";
        case ActionKind::Launch:
            return "Launch";
        case ActionKind::Other:
            return "Other";
        case ActionKind::None:
            break;
    }
    return "none";
}

bool Field::checked() const {
    if (type != FieldType::CheckBox && type != FieldType::RadioButton) {
        return false;
    }
    return std::any_of(widgets.begin(), widgets.end(), [](const Widget& w) { return w.on; });
}

const Field* Form::find(std::string_view name) const {
    for (const Field& f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

const Field* Form::fieldOf(int page, int objnum) const {
    for (const Field& f : fields) {
        for (const Widget& w : f.widgets) {
            if (w.objnum == objnum && w.page == page) {
                return &f;
            }
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------- внутреннее

namespace detail {

pdf_document* pdfOf(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        throw std::logic_error("forms: not a PDF document");
    }
    return pdf;
}

std::string textOf(fz_context* ctx, pdf_obj* obj) {
    if (obj == nullptr) {
        return {};
    }
    const char* s = nullptr;
    char* owned = nullptr;
    mu::call(ctx, [&] {
        if (pdf_is_name(ctx, obj)) {
            s = pdf_to_name(ctx, obj);
        } else if (pdf_is_string(ctx, obj)) {
            s = pdf_to_text_string(ctx, obj);
        } else if (pdf_is_stream(ctx, obj)) {
            owned = pdf_load_stream_or_string_as_utf8(ctx, obj);
        }
    });
    std::string out = owned != nullptr ? owned : (s != nullptr ? s : "");
    if (owned != nullptr) {
        fz_free(ctx, owned);
    }
    return out;
}

WidgetPages::WidgetPages(Document& doc) : doc_(doc), ctx_(doc.context().get()), pdf_(pdfOf(doc)) {
    fz_context* ctx = ctx_;
    pdf_document* pdf = pdf_;
    int count = 0;
    mu::call(ctx, [&] { count = pdf_count_pages(ctx, pdf); });
    for (int p = 0; p < count; ++p) {
        int has = 0;
        mu::call(ctx, [&] {
            pdf_obj* annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, p), PDF_NAME(Annots));
            const int n = pdf_array_len(ctx, annots);
            for (int i = 0; i < n && has == 0; ++i) {
                has = pdf_name_eq(ctx, pdf_dict_get(ctx, pdf_array_get(ctx, annots, i), PDF_NAME(Subtype)),
                                  PDF_NAME(Widget));
            }
        });
        if (has == 0) {
            continue;
        }
        mu::PageHandle page(ctx);
        fz_document* fz = doc.handle();
        mu::call(ctx, [&] { page.reset(fz_load_page(ctx, fz, p)); });
        pages_.emplace_back(p, std::move(page));
    }
}

std::pair<pdf_annot*, int> WidgetPages::find(int objnum) const {
    fz_context* ctx = ctx_;
    for (const auto& [index, handle] : pages_) {
        pdf_page* page = pdf_page_from_fz_page(ctx, handle.get());
        pdf_annot* found = nullptr;
        mu::call(ctx, [&] {
            for (pdf_annot* w = pdf_first_widget(ctx, page); w != nullptr && found == nullptr;
                 w = pdf_next_widget(ctx, w)) {
                if (pdf_to_num(ctx, pdf_annot_obj(ctx, w)) == objnum) {
                    found = w;
                }
            }
        });
        if (found != nullptr) {
            return {found, index};
        }
    }
    return {nullptr, -1};
}

std::vector<int> WidgetPages::update() {
    fz_context* ctx = ctx_;
    std::vector<std::pair<int, pdf_annot*>> changed;
    // Как pdf_update_page, но «вид сменился» собирается по всем страницам: pdf_update_page сам снимает этот
    // признак у своих аннотаций, а синтез идёт сразу для всех открытых страниц.
    pdf_document* pdf = pdf_;
    mu::call(ctx, [&] {
        if (pdf->recalculate != 0 && pdf->js != nullptr) {
            pdf_calculate_form(ctx, pdf);
        }
    });
    for (const auto& [index, handle] : pages_) {
        pdf_page* page = pdf_page_from_fz_page(ctx, handle.get());
        const int p = index;
        mu::call(ctx, [&] {
            for (pdf_annot* a = pdf_first_annot(ctx, page); a != nullptr; a = pdf_next_annot(ctx, a)) {
                if (pdf_update_annot(ctx, a) != 0) {
                    changed.emplace_back(p, a);
                }
            }
            for (pdf_annot* w = pdf_first_widget(ctx, page); w != nullptr; w = pdf_next_widget(ctx, w)) {
                if (pdf_update_annot(ctx, w) != 0) {
                    changed.emplace_back(p, w);
                }
            }
        });
    }
    std::set<int> pages;
    for (const auto& [p, a] : changed) {
        pages.insert(p);
        int isWidget = 0;
        mu::call(ctx, [&] { isWidget = pdf_annot_type(ctx, a) == PDF_ANNOT_WIDGET; });
        if (isWidget != 0 && writeOwnTextAppearance(doc_, a)) {
            mu::call(ctx, [&] {
                pdf_update_annot(ctx, a);
            });  // снять «вид сменился» — следующая правка его не повторит
        }
    }
    return {pages.begin(), pages.end()};
}

}  // namespace detail

// ---------------------------------------------------------------- чтение

namespace fields_impl {

using detail::textOf;

struct PageMap {
    std::unordered_map<int, int> pageOf;  // номер объекта аннотации → страница
    std::vector<fz_matrix> ctm;
};

PageMap pageMap(fz_context* ctx, pdf_document* pdf) {
    PageMap map;
    int count = 0;
    mu::call(ctx, [&] { count = pdf_count_pages(ctx, pdf); });
    map.ctm.resize(static_cast<std::size_t>(count), fz_identity);
    std::vector<int> nums;
    for (int p = 0; p < count; ++p) {
        nums.clear();
        fz_matrix m = fz_identity;
        mu::call(ctx, [&] {
            pdf_obj* page = pdf_lookup_page_obj(ctx, pdf, p);
            fz_rect box;
            pdf_page_obj_transform(ctx, page, &box, &m);
            pdf_obj* annots = pdf_dict_get(ctx, page, PDF_NAME(Annots));
            const int n = pdf_array_len(ctx, annots);
            for (int i = 0; i < n; ++i) {
                nums.push_back(pdf_to_num(ctx, pdf_array_get(ctx, annots, i)));
            }
        });
        map.ctm[static_cast<std::size_t>(p)] = m;
        for (const int num : nums) {
            if (num > 0) {
                map.pageOf.emplace(num, p);
            }
        }
    }
    return map;
}

struct RawField {
    pdf_obj* node = nullptr;
    std::vector<pdf_obj*> widgets;
};

// Обход дерева полей: концевое поле — без /Kids (оно же виджет) или с детьми-виджетами (без /T); дети с /T —
// поля. onlyNamed — у смешанного родителя обходить только детей с /T.
void walk(fz_context* ctx, pdf_obj* array, std::vector<RawField>& out, std::unordered_set<int>& seen,
          int depth, bool onlyNamed) {
    if (depth > 32) {
        return;
    }
    int n = 0;
    mu::call(ctx, [&] { n = pdf_array_len(ctx, array); });
    for (int i = 0; i < n; ++i) {
        pdf_obj* node = nullptr;
        pdf_obj* kids = nullptr;
        int isDict = 0;
        int num = 0;
        int named = 0;
        int widget = 0;
        mu::call(ctx, [&] {
            node = pdf_array_get(ctx, array, i);
            isDict = pdf_is_dict(ctx, node);
            num = pdf_to_num(ctx, node);
            kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
            named = pdf_dict_get(ctx, node, PDF_NAME(T)) != nullptr;
            widget = pdf_name_eq(ctx, pdf_dict_get(ctx, node, PDF_NAME(Subtype)), PDF_NAME(Widget));
        });
        if (isDict == 0 || (onlyNamed && named == 0)) {
            continue;
        }
        if (num > 0 && !seen.insert(num).second) {
            continue;  // цикл или повтор в /Fields
        }
        if (kids == nullptr) {
            RawField f;
            f.node = node;
            if (widget != 0) {
                f.widgets.push_back(node);
            }
            out.push_back(std::move(f));
            continue;
        }
        int kn = 0;
        mu::call(ctx, [&] { kn = pdf_array_len(ctx, kids); });
        std::vector<pdf_obj*> widgets;
        bool fieldKids = false;
        for (int k = 0; k < kn; ++k) {
            pdf_obj* kid = nullptr;
            int kidDict = 0;
            int kidNamed = 0;
            mu::call(ctx, [&] {
                kid = pdf_array_get(ctx, kids, k);
                kidDict = pdf_is_dict(ctx, kid);
                kidNamed = pdf_dict_get(ctx, kid, PDF_NAME(T)) != nullptr;
            });
            if (kidDict == 0) {
                continue;
            }
            if (kidNamed != 0) {
                fieldKids = true;
            } else {
                widgets.push_back(kid);
            }
        }
        if (!widgets.empty() || !fieldKids) {
            RawField f;
            f.node = node;
            f.widgets = std::move(widgets);
            out.push_back(std::move(f));
        }
        if (fieldKids) {
            walk(ctx, kids, out, seen, depth + 1, true);
        }
    }
}

FieldType typeFrom(int muType) {
    switch (muType) {
        case PDF_WIDGET_TYPE_BUTTON:
            return FieldType::PushButton;
        case PDF_WIDGET_TYPE_CHECKBOX:
            return FieldType::CheckBox;
        case PDF_WIDGET_TYPE_RADIOBUTTON:
            return FieldType::RadioButton;
        case PDF_WIDGET_TYPE_TEXT:
            return FieldType::Text;
        case PDF_WIDGET_TYPE_COMBOBOX:
            return FieldType::ComboBox;
        case PDF_WIDGET_TYPE_LISTBOX:
            return FieldType::ListBox;
        case PDF_WIDGET_TYPE_SIGNATURE:
            return FieldType::Signature;
        default:
            return FieldType::Unknown;
    }
}

TextFormat formatFrom(const std::string& script) {
    if (script.find("AFNumber_Format") != std::string::npos) {
        return TextFormat::Number;
    }
    if (script.find("AFPercent_Format") != std::string::npos) {
        return TextFormat::Percent;
    }
    if (script.find("AFDate_Format") != std::string::npos) {
        return TextFormat::Date;
    }
    if (script.find("AFTime_Format") != std::string::npos) {
        return TextFormat::Time;
    }
    if (script.find("AFSpecial_Format") != std::string::npos) {
        return TextFormat::Special;
    }
    return TextFormat::None;
}

std::string fieldName(fz_context* ctx, pdf_obj* node) {
    char* name = nullptr;
    mu::call(ctx, [&] { name = pdf_load_field_name(ctx, node); });
    std::string out = name != nullptr ? name : "";
    fz_free(ctx, name);
    return out;
}

// Имена /Fields действия (строки или ссылки на поля).
std::vector<std::string> fieldNames(fz_context* ctx, pdf_obj* array) {
    std::vector<std::string> out;
    int n = 0;
    mu::call(ctx, [&] { n = pdf_array_len(ctx, array); });
    for (int i = 0; i < n; ++i) {
        pdf_obj* item = nullptr;
        int isString = 0;
        mu::call(ctx, [&] {
            item = pdf_array_get(ctx, array, i);
            isString = pdf_is_string(ctx, item);
        });
        out.push_back(isString != 0 ? textOf(ctx, item) : fieldName(ctx, item));
    }
    return out;
}

// Файл спецификации /F (строка или словарь /UF, /F).
std::string fileSpec(fz_context* ctx, pdf_obj* spec) {
    pdf_obj* text = spec;
    mu::call(ctx, [&] {
        if (pdf_is_dict(ctx, spec)) {
            text = pdf_dict_get(ctx, spec, PDF_NAME(UF));
            if (text == nullptr) {
                text = pdf_dict_get(ctx, spec, PDF_NAME(F));
            }
        }
    });
    return textOf(ctx, text);
}

FieldAction parseAction(fz_context* ctx, pdf_document* pdf, pdf_obj* action) {
    FieldAction a;
    if (action == nullptr) {
        return a;
    }
    const char* s = nullptr;
    pdf_obj* uri = nullptr;
    pdf_obj* file = nullptr;
    pdf_obj* fields = nullptr;
    pdf_obj* js = nullptr;
    pdf_obj* dest = nullptr;
    pdf_obj* named = nullptr;
    int flags = 0;
    int next = 0;
    mu::call(ctx, [&] {
        if (pdf_is_array(ctx, action)) {
            next = pdf_array_len(ctx, action) > 1;
            action = pdf_array_get(ctx, action, 0);
        }
        s = pdf_dict_get_name(ctx, action, PDF_NAME(S));
        uri = pdf_dict_get(ctx, action, PDF_NAME(URI));
        file = pdf_dict_get(ctx, action, PDF_NAME(F));
        fields = pdf_dict_get(ctx, action, PDF_NAME(Fields));
        js = pdf_dict_get(ctx, action, PDF_NAME(JS));
        dest = pdf_dict_get(ctx, action, PDF_NAME(D));
        named = pdf_dict_get(ctx, action, PDF_NAME(N));
        flags = pdf_dict_get_int(ctx, action, PDF_NAME(Flags));
        next = next != 0 || pdf_dict_get(ctx, action, PDF_NAME(Next)) != nullptr;
    });
    const std::string kind = s != nullptr ? s : "";
    a.flags = flags;
    a.hasNext = next != 0;
    if (kind == "ResetForm") {
        a.kind = ActionKind::ResetForm;
        a.fields = fieldNames(ctx, fields);
    } else if (kind == "SubmitForm") {
        a.kind = ActionKind::SubmitForm;
        a.target = fileSpec(ctx, file);
        a.fields = fieldNames(ctx, fields);
    } else if (kind == "ImportData") {
        a.kind = ActionKind::ImportData;
        a.target = fileSpec(ctx, file);
    } else if (kind == "JavaScript") {
        a.kind = ActionKind::JavaScript;
        a.script = textOf(ctx, js);
    } else if (kind == "URI") {
        a.kind = ActionKind::Uri;
        a.target = textOf(ctx, uri);
    } else if (kind == "Named") {
        a.kind = ActionKind::Named;
        a.target = textOf(ctx, named);
    } else if (kind == "GoToR") {
        a.kind = ActionKind::GoToR;
        a.target = fileSpec(ctx, file);
    } else if (kind == "Launch") {
        a.kind = ActionKind::Launch;
        a.target = fileSpec(ctx, file);
    } else if (kind == "GoTo") {
        a.kind = ActionKind::GoTo;
        int page = -1;
        mu::call(ctx, [&] {
            pdf_obj* d = dest;
            if (pdf_is_name(ctx, d) || pdf_is_string(ctx, d)) {
                d = pdf_lookup_dest(ctx, pdf, d);
            }
            if (pdf_is_dict(ctx, d)) {
                d = pdf_dict_get(ctx, d, PDF_NAME(D));
            }
            pdf_obj* target = pdf_array_get(ctx, d, 0);
            if (pdf_is_int(ctx, target)) {
                page = pdf_to_int(ctx, target);
            } else if (target != nullptr) {
                page = pdf_lookup_page_number(ctx, pdf, target);
            }
        });
        a.page = page;
    } else if (!kind.empty()) {
        a.kind = ActionKind::Other;
        a.target = kind;
    }
    return a;
}

std::string scriptOf(fz_context* ctx, pdf_obj* aa, pdf_obj* key) {
    pdf_obj* js = nullptr;
    mu::call(ctx, [&] { js = pdf_dict_get(ctx, pdf_dict_get(ctx, aa, key), PDF_NAME(JS)); });
    return textOf(ctx, js);
}

Field makeField(fz_context* ctx, pdf_document* pdf, const RawField& raw, const PageMap& map, pdf_obj* formDa,
                int formQ) {
    Field f;
    f.name = fieldName(ctx, raw.node);
    pdf_obj* first = raw.widgets.empty() ? raw.node : raw.widgets.front();
    int muType = 0;
    int flags = 0;
    int maxLen = 0;
    int quadding = formQ;
    int isSigned = 0;
    pdf_obj* v = nullptr;
    pdf_obj* dv = nullptr;
    pdf_obj* tu = nullptr;
    pdf_obj* tm = nullptr;
    pdf_obj* opt = nullptr;
    pdf_obj* aa = nullptr;
    pdf_obj* action = nullptr;
    pdf_obj* da = nullptr;
    int vArray = 0;
    int optCount = 0;
    mu::call(ctx, [&] {
        muType = pdf_field_type(ctx, raw.node);
        flags = pdf_field_flags(ctx, raw.node);
        maxLen = pdf_dict_get_inheritable_int(ctx, raw.node, PDF_NAME(MaxLen));
        pdf_obj* q = pdf_dict_get_inheritable(ctx, first, PDF_NAME(Q));
        if (q != nullptr) {
            quadding = pdf_to_int(ctx, q);
        }
        v = pdf_dict_get_inheritable(ctx, raw.node, PDF_NAME(V));
        dv = pdf_dict_get_inheritable(ctx, raw.node, PDF_NAME(DV));
        tu = pdf_dict_get(ctx, raw.node, PDF_NAME(TU));
        tm = pdf_dict_gets(ctx, raw.node, "TM");
        opt = pdf_dict_get_inheritable(ctx, raw.node, PDF_NAME(Opt));
        optCount = pdf_array_len(ctx, opt);
        aa = pdf_dict_get(ctx, raw.node, PDF_NAME(AA));
        if (aa == nullptr) {
            aa = pdf_dict_get(ctx, first, PDF_NAME(AA));
        }
        action = pdf_dict_get(ctx, first, PDF_NAME(A));
        if (action == nullptr) {
            action = pdf_dict_get(ctx, raw.node, PDF_NAME(A));
        }
        da = pdf_dict_get_inheritable(ctx, first, PDF_NAME(DA));
        if (da == nullptr) {
            da = formDa;
        }
        vArray = pdf_is_array(ctx, v);
        if (muType == PDF_WIDGET_TYPE_SIGNATURE) {
            isSigned = pdf_signature_is_signed(ctx, pdf, raw.node);
        }
    });
    f.type = typeFrom(muType);
    f.flags = flags;
    f.maxLen = maxLen;
    f.isSigned = isSigned != 0;
    f.tooltip = textOf(ctx, tu);
    f.mappingName = textOf(ctx, tm);
    if (vArray != 0) {
        int n = 0;
        mu::call(ctx, [&] { n = pdf_array_len(ctx, v); });
        for (int i = 0; i < n; ++i) {
            pdf_obj* item = nullptr;
            mu::call(ctx, [&] {
                item = pdf_array_get(ctx, v, i);
                if (pdf_is_array(ctx, item)) {
                    item = pdf_array_get(ctx, item, 0);
                }
            });
            f.values.push_back(textOf(ctx, item));
        }
        f.value = f.values.empty() ? std::string() : f.values.front();
    } else {
        f.value = textOf(ctx, v);
        if (v != nullptr) {
            f.values.push_back(f.value);
        }
    }
    f.defaultValue = textOf(ctx, dv);
    for (int i = 0; i < optCount; ++i) {
        pdf_obj* exp = nullptr;
        pdf_obj* label = nullptr;
        mu::call(ctx, [&] {
            pdf_obj* item = pdf_array_get(ctx, opt, i);
            if (pdf_is_array(ctx, item)) {
                exp = pdf_array_get(ctx, item, 0);
                label = pdf_array_get(ctx, item, 1);
            } else {
                exp = item;
                label = item;
            }
        });
        f.options.push_back({textOf(ctx, exp), textOf(ctx, label)});
    }
    f.formatScript = scriptOf(ctx, aa, PDF_NAME(F));
    f.keystrokeScript = scriptOf(ctx, aa, PDF_NAME(K));
    f.validateScript = scriptOf(ctx, aa, PDF_NAME(V));
    f.calculateScript = scriptOf(ctx, aa, PDF_NAME(C));
    f.format = formatFrom(f.formatScript);
    f.action = parseAction(ctx, pdf, action);

    const std::string daText = textOf(ctx, da);
    char font[64] = {};
    float size = 0;
    int n = 0;
    float color[4] = {};
    const char* daPtr = daText.c_str();
    mu::call(ctx,
             [&] { pdf_parse_default_appearance_unmapped(ctx, daPtr, font, sizeof font, &size, &n, color); });
    f.style.font = font;
    f.style.size = size;
    f.style.quadding = std::clamp(quadding, 0, 2);
    if (n == 1) {
        f.style.color = {color[0], color[0], color[0]};
    } else if (n == 3) {
        f.style.color = {color[0], color[1], color[2]};
    } else if (n == 4) {
        f.style.color = {(1 - color[0]) * (1 - color[3]), (1 - color[1]) * (1 - color[3]),
                         (1 - color[2]) * (1 - color[3])};
    }

    const bool button = f.type == FieldType::CheckBox || f.type == FieldType::RadioButton;
    for (std::size_t i = 0; i < raw.widgets.size(); ++i) {
        pdf_obj* w = raw.widgets[i];
        Widget out;
        int num = 0;
        int annotFlags = 0;
        int rotation = 0;
        fz_rect rect{};
        pdf_obj* as = nullptr;
        pdf_obj* on = nullptr;
        pdf_obj* caption = nullptr;
        pdf_obj* pageRef = nullptr;
        mu::call(ctx, [&] {
            num = pdf_to_num(ctx, w);
            rect = pdf_dict_get_rect(ctx, w, PDF_NAME(Rect));
            annotFlags = pdf_dict_get_int(ctx, w, PDF_NAME(F));
            pdf_obj* mk = pdf_dict_get(ctx, w, PDF_NAME(MK));
            rotation = pdf_dict_get_int(ctx, mk, PDF_NAME(R));
            caption = pdf_dict_get(ctx, mk, PDF_NAME(CA));
            as = pdf_dict_get(ctx, w, PDF_NAME(AS));
            if (button) {
                on = pdf_button_field_on_state(ctx, w);
            }
            pageRef = pdf_dict_get(ctx, w, PDF_NAME(P));
        });
        out.objnum = num;
        if (const auto it = map.pageOf.find(num); it != map.pageOf.end()) {
            out.page = it->second;
        } else if (pageRef != nullptr) {
            int page = -1;
            mu::call(ctx, [&] { page = pdf_lookup_page_number(ctx, pdf, pageRef); });
            out.page = page;
        }
        if (out.page >= 0 && static_cast<std::size_t>(out.page) < map.ctm.size()) {
            const fz_rect r = fz_transform_rect(rect, map.ctm[static_cast<std::size_t>(out.page)]);
            out.rect = RectF{r.x0, r.y0, r.x1, r.y1};
        }
        out.rotation = ((rotation % 360) + 360) % 360;
        out.hidden = (annotFlags & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_NO_VIEW)) != 0;
        out.caption = textOf(ctx, caption);
        if (button) {
            out.onState = textOf(ctx, on);
            const std::string state = textOf(ctx, as);
            out.on = !state.empty() && state != "Off";
            out.exportValue = i < f.options.size() ? f.options[i].exportValue : out.onState;
        }
        f.widgets.push_back(std::move(out));
    }
    return f;
}

// Поля, закрытые подписанными подписями (/Lock: All — все, кроме самой подписи; Include — список; Exclude —
// все, кроме списка).
void applyLocks(fz_context* ctx, Form& form, const std::vector<RawField>& raws) {
    for (std::size_t i = 0; i < raws.size(); ++i) {
        const Field& sig = form.fields[i];
        if (sig.type != FieldType::Signature || !sig.isSigned) {
            continue;
        }
        pdf_obj* lock = nullptr;
        const char* action = nullptr;
        pdf_obj* fields = nullptr;
        pdf_obj* first = raws[i].widgets.empty() ? raws[i].node : raws[i].widgets.front();
        mu::call(ctx, [&] {
            lock = pdf_dict_get(ctx, raws[i].node, PDF_NAME(Lock));
            if (lock == nullptr) {
                lock = pdf_dict_get(ctx, first, PDF_NAME(Lock));
            }
            action = pdf_dict_get_name(ctx, lock, PDF_NAME(Action));
            fields = pdf_dict_get(ctx, lock, PDF_NAME(Fields));
        });
        if (lock == nullptr || action == nullptr) {
            continue;
        }
        const std::string kind = action;
        const std::vector<std::string> names = fieldNames(ctx, fields);
        const auto listed = [&names](const std::string& name) {
            return std::any_of(names.begin(), names.end(), [&name](const std::string& n) {
                return name == n ||
                       (name.size() > n.size() && name.compare(0, n.size(), n) == 0 && name[n.size()] == '.');
            });
        };
        for (Field& f : form.fields) {
            if (&f == &sig) {
                continue;
            }
            if (kind == "All" || (kind == "Include" && listed(f.name)) ||
                (kind == "Exclude" && !listed(f.name))) {
                f.locked = true;
            }
        }
    }
}

bool docLevelJavaScript(fz_context* ctx, pdf_document* pdf) {
    int found = 0;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        pdf_obj* tree = pdf_load_name_tree(ctx, pdf, PDF_NAME(JavaScript));
        found = pdf_dict_len(ctx, tree) > 0;
        pdf_drop_obj(ctx, tree);
        pdf_obj* open = pdf_dict_gets(ctx, root, "OpenAction");
        if (pdf_name_eq(ctx, pdf_dict_get(ctx, open, PDF_NAME(S)), PDF_NAME(JavaScript))) {
            found = 1;
        }
        if (pdf_dict_len(ctx, pdf_dict_get(ctx, root, PDF_NAME(AA))) > 0) {
            found = 1;
        }
        const int pages = pdf_count_pages(ctx, pdf);
        for (int p = 0; p < pages && found == 0; ++p) {
            pdf_obj* aa = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, p), PDF_NAME(AA));
            const int n = pdf_dict_len(ctx, aa);
            for (int i = 0; i < n; ++i) {
                if (pdf_name_eq(ctx, pdf_dict_get(ctx, pdf_dict_get_val(ctx, aa, i), PDF_NAME(S)),
                                PDF_NAME(JavaScript))) {
                    found = 1;
                }
            }
        }
    });
    return found != 0;
}

bool fieldsJavaScript(const Form& form) {
    return std::any_of(form.fields.begin(), form.fields.end(), [](const Field& f) {
        return !f.formatScript.empty() || !f.keystrokeScript.empty() || !f.validateScript.empty() ||
               !f.calculateScript.empty() || f.action.kind == ActionKind::JavaScript;
    });
}

Form read(const Document& doc, bool withScriptsFlag) {
    Form form;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return form;
    }
    fz_context* ctx = doc.context().get();
    pdf_obj* acro = nullptr;
    pdf_obj* fields = nullptr;
    pdf_obj* co = nullptr;
    pdf_obj* formDa = nullptr;
    int formQ = 0;
    int need = 0;
    int xfa = 0;
    int sigFlags = 0;
    mu::call(ctx, [&] {
        acro = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm");
        fields = pdf_dict_get(ctx, acro, PDF_NAME(Fields));
        co = pdf_dict_get(ctx, acro, PDF_NAME(CO));
        formDa = pdf_dict_get(ctx, acro, PDF_NAME(DA));
        formQ = pdf_dict_get_int(ctx, acro, PDF_NAME(Q));
        need = pdf_dict_get_bool(ctx, acro, PDF_NAME(NeedAppearances));
        xfa = pdf_dict_get(ctx, acro, PDF_NAME(XFA)) != nullptr;
        sigFlags = pdf_dict_get_int(ctx, acro, PDF_NAME(SigFlags));
    });
    form.needAppearances = need != 0;
    form.xfa = xfa != 0;
    form.sigFlags = sigFlags;
    std::vector<RawField> raws;
    if (fields != nullptr) {
        std::unordered_set<int> seen;
        walk(ctx, fields, raws, seen, 0, false);
    }
    if (!raws.empty()) {
        const PageMap map = pageMap(ctx, pdf);
        form.fields.reserve(raws.size());
        for (const RawField& raw : raws) {
            form.fields.push_back(makeField(ctx, pdf, raw, map, formDa, formQ));
        }
        applyLocks(ctx, form, raws);
    }
    form.calculationOrder = fieldNames(ctx, co);
    if (withScriptsFlag) {
        form.javaScript = fieldsJavaScript(form) || docLevelJavaScript(ctx, pdf);
    }
    return form;
}

pdf_obj* lookup(fz_context* ctx, pdf_document* pdf, const std::string& name) {
    pdf_obj* field = nullptr;
    const char* n = name.c_str();
    mu::call(ctx, [&] {
        pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
        field = pdf_lookup_field(ctx, fields, n);
    });
    return field;
}

std::vector<int> pagesOf(const Field& f) {
    std::set<int> pages;
    for (const Widget& w : f.widgets) {
        if (w.page >= 0) {
            pages.insert(w.page);
        }
    }
    return {pages.begin(), pages.end()};
}

std::vector<int> unite(std::vector<int> a, const std::vector<int>& b) {
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end());
    a.erase(std::unique(a.begin(), a.end()), a.end());
    return a;
}

// Обрезать до max знаков (UTF-8).
std::string truncateChars(std::string_view text, int max) {
    std::u32string u = utf8::decode(text);
    if (max > 0 && static_cast<int>(u.size()) > max) {
        u.resize(static_cast<std::size_t>(max));
    }
    return utf8::encode(u);
}

// Состояние флажка или переключателя для значения: имя состояния или экспортное значение; пусто и Off — Off.
std::optional<std::string> stateFor(const Field& f, std::string_view value) {
    if (value.empty() || value == "Off") {
        return std::string("Off");
    }
    for (const Widget& w : f.widgets) {
        if (w.onState == value) {
            return w.onState;
        }
    }
    for (const Widget& w : f.widgets) {
        if (w.exportValue == value) {
            return w.onState;
        }
    }
    return std::nullopt;
}

// Вариант списка по экспортному значению или подписи.
std::optional<std::size_t> optionIndex(const Field& f, std::string_view value) {
    for (std::size_t i = 0; i < f.options.size(); ++i) {
        if (f.options[i].exportValue == value) {
            return i;
        }
    }
    for (std::size_t i = 0; i < f.options.size(); ++i) {
        if (f.options[i].label == value) {
            return i;
        }
    }
    return std::nullopt;
}

// Отказ до правки: нет поля, не поле значения, только для чтения.
std::optional<std::string> refusal(const Field& f);
std::optional<std::string> refusal(const std::optional<Field>& f) {
    if (!f) {
        return std::string("unknown-field");
    }
    return refusal(*f);
}

std::optional<std::string> refusal(const Field& field) {
    const Field* f = &field;
    if (f->type == FieldType::PushButton || f->type == FieldType::Signature ||
        f->type == FieldType::Unknown || f->widgets.empty()) {
        return std::string("not-a-value-field");
    }
    if (f->locked) {
        return std::string("locked");
    }
    if (f->readOnly()) {
        return std::string("read-only");
    }
    return std::nullopt;
}

void setRecalculate(pdf_document* pdf, bool on) { pdf->recalculate = on ? 1 : 0; }

// Флажок или переключатель: /V — имя состояния у поля, /AS — у виджетов по /AP /N, вид заново.
void setButtonState(fz_context* ctx, pdf_document* pdf, pdf_obj* field, const std::string& state,
                    const detail::WidgetPages& pages, const Field& f) {
    const char* s = state.c_str();
    mu::call(ctx, [&] {
        pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        if (state == "Off") {
            pdf_dict_put(ctx, field, PDF_NAME(V), PDF_NAME(Off));
        } else {
            pdf_dict_put_name(ctx, field, PDF_NAME(V), s);
        }
        const int n = kids != nullptr ? pdf_array_len(ctx, kids) : 1;
        for (int i = 0; i < n; ++i) {
            pdf_obj* w = kids != nullptr ? pdf_array_get(ctx, kids, i) : field;
            if (kids != nullptr && pdf_dict_get(ctx, w, PDF_NAME(T)) != nullptr) {
                continue;  // ребёнок-поле, не виджет
            }
            pdf_obj* states = pdf_dict_getp(ctx, w, "AP/N");
            if (state != "Off" && pdf_dict_gets(ctx, states, s) != nullptr) {
                pdf_dict_put_name(ctx, w, PDF_NAME(AS), s);
            } else {
                pdf_dict_put(ctx, w, PDF_NAME(AS), PDF_NAME(Off));
            }
        }
    });
    for (const Widget& w : f.widgets) {
        pdf_annot* annot = pages.find(w.objnum).first;
        if (annot != nullptr) {
            mu::call(ctx, [&] { pdf_dirty_annot(ctx, annot); });
        }
    }
    setRecalculate(pdf, true);
}

// Список с мультивыбором: /V — строка или массив экспортных значений, /I — номера вариантов.
void setChoiceValues(fz_context* ctx, pdf_document* pdf, pdf_obj* field,
                     const std::vector<std::string>& values, const std::vector<int>& indices,
                     const detail::WidgetPages& pages, const Field& f) {
    std::vector<const char*> raw;
    raw.reserve(values.size());
    for (const std::string& v : values) {
        raw.push_back(v.c_str());
    }
    const int n = static_cast<int>(raw.size());
    const char* const* items = raw.data();
    const int* idx = indices.data();
    mu::call(ctx, [&] {
        if (n == 1) {
            pdf_dict_put_text_string(ctx, field, PDF_NAME(V), items[0]);
        } else {
            pdf_obj* arr = pdf_dict_put_array(ctx, field, PDF_NAME(V), n);
            for (int i = 0; i < n; ++i) {
                pdf_array_push_text_string(ctx, arr, items[i]);
            }
        }
        if (n == 0) {
            pdf_dict_del(ctx, field, PDF_NAME(V));
            pdf_dict_del(ctx, field, PDF_NAME(I));
        } else {
            pdf_obj* arr = pdf_dict_put_array(ctx, field, PDF_NAME(I), n);
            for (int i = 0; i < n; ++i) {
                pdf_array_push_int(ctx, arr, idx[i]);
            }
        }
    });
    for (const Widget& w : f.widgets) {
        pdf_annot* annot = pages.find(w.objnum).first;
        if (annot != nullptr) {
            mu::call(ctx, [&] { pdf_dirty_annot(ctx, annot); });
        }
    }
    setRecalculate(pdf, true);
}

}  // namespace fields_impl

Form readForm(const Document& doc) { return fields_impl::read(doc, true); }

std::optional<Field> readField(const Document& doc, std::string_view name) {
    Form form = fields_impl::read(doc, false);
    for (Field& f : form.fields) {
        if (f.name == name) {
            return std::move(f);
        }
    }
    return std::nullopt;
}

std::vector<PageWidget> pageWidgets(const Document& doc, int page) {
    std::vector<PageWidget> out;
    const Form form = fields_impl::read(doc, false);
    for (const Field& f : form.fields) {
        for (const Widget& w : f.widgets) {
            if (w.page == page) {
                out.push_back({w, f.name});
            }
        }
    }
    // Порядок /Annots страницы.
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr || out.empty()) {
        return out;
    }
    fz_context* ctx = doc.context().get();
    std::vector<int> order;
    mu::call(ctx, [&] {
        pdf_obj* annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Annots));
        const int n = pdf_array_len(ctx, annots);
        for (int i = 0; i < n; ++i) {
            order.push_back(pdf_to_num(ctx, pdf_array_get(ctx, annots, i)));
        }
    });
    const auto rank = [&order](int objnum) {
        const auto it = std::find(order.begin(), order.end(), objnum);
        return static_cast<std::size_t>(it - order.begin());
    };
    std::stable_sort(out.begin(), out.end(), [&rank](const PageWidget& a, const PageWidget& b) {
        return rank(a.widget.objnum) < rank(b.widget.objnum);
    });
    return out;
}

bool hasJavaScript(const Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return false;
    }
    return fields_impl::fieldsJavaScript(fields_impl::read(doc, false)) ||
           fields_impl::docLevelJavaScript(doc.context().get(), pdf);
}

// ---------------------------------------------------------------- порядок табуляции

TabOrder tabOrder(const Document& doc, int page) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return TabOrder::Annots;
    }
    fz_context* ctx = doc.context().get();
    const char* tabs = nullptr;
    mu::call(ctx, [&] {
        tabs = pdf_to_name(ctx, pdf_dict_gets(ctx, pdf_lookup_page_obj(ctx, pdf, page), "Tabs"));
    });
    const std::string t = tabs != nullptr ? tabs : "";
    if (t == "R") {
        return TabOrder::Row;
    }
    if (t == "C") {
        return TabOrder::Column;
    }
    if (t == "S") {
        return TabOrder::Structure;
    }
    return TabOrder::Annots;
}

std::vector<int> orderWidgets(const std::vector<std::pair<int, RectF>>& widgets, TabOrder order) {
    std::vector<int> out;
    out.reserve(widgets.size());
    if (order == TabOrder::Annots || order == TabOrder::Structure) {
        for (const auto& [num, rect] : widgets) {
            out.push_back(num);
        }
        return out;
    }
    const bool rows = order == TabOrder::Row;
    // Полоса (ряд или столбец) и положение вдоль неё.
    const auto bandLo = [rows](const RectF& r) { return rows ? r.y0 : r.x0; };
    const auto bandHi = [rows](const RectF& r) { return rows ? r.y1 : r.x1; };
    const auto along = [rows](const RectF& r) { return rows ? r.x0 : r.y0; };
    std::vector<std::pair<int, RectF>> sorted = widgets;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [&](const auto& a, const auto& b) { return bandLo(a.second) < bandLo(b.second); });
    std::size_t i = 0;
    while (i < sorted.size()) {
        const float lo = bandLo(sorted[i].second);
        const float hi = bandHi(sorted[i].second);
        std::size_t j = i + 1;
        while (j < sorted.size()) {
            const RectF& r = sorted[j].second;
            const float overlap = std::min(hi, bandHi(r)) - std::max(lo, bandLo(r));
            const float smaller = std::min(hi - lo, bandHi(r) - bandLo(r));
            if (overlap <= 0.5f * smaller) {
                break;
            }
            ++j;
        }
        std::stable_sort(sorted.begin() + static_cast<std::ptrdiff_t>(i),
                         sorted.begin() + static_cast<std::ptrdiff_t>(j),
                         [&](const auto& a, const auto& b) { return along(a.second) < along(b.second); });
        for (std::size_t k = i; k < j; ++k) {
            out.push_back(sorted[k].first);
        }
        i = j;
    }
    return out;
}

std::vector<int> annotOrder(const Document& doc, int page) {
    std::vector<int> order;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return order;
    }
    fz_context* ctx = doc.context().get();
    mu::call(ctx, [&] {
        pdf_obj* annots = pdf_dict_get(ctx, pdf_lookup_page_obj(ctx, pdf, page), PDF_NAME(Annots));
        const int n = pdf_array_len(ctx, annots);
        for (int i = 0; i < n; ++i) {
            order.push_back(pdf_to_num(ctx, pdf_array_get(ctx, annots, i)));
        }
    });
    return order;
}

std::vector<int> tabSequence(const Form& form, int page, TabOrder order, const std::vector<int>& annots) {
    struct Stop {
        std::size_t rank;
        int objnum;
        RectF rect;
    };
    std::vector<Stop> stops;
    for (const Field& f : form.fields) {
        if (f.readOnly()) {
            continue;
        }
        const Widget* chosen = nullptr;
        if (f.type == FieldType::RadioButton) {
            // Одна остановка на группу: выбранный виджет страницы, иначе первый по /Annots.
            for (const Widget& w : f.widgets) {
                if (w.page == page && !w.hidden && w.on) {
                    chosen = &w;
                }
            }
        }
        for (const Widget& w : f.widgets) {
            if (w.page != page || w.hidden || (chosen != nullptr && &w != chosen)) {
                continue;
            }
            const auto it = std::find(annots.begin(), annots.end(), w.objnum);
            stops.push_back({static_cast<std::size_t>(it - annots.begin()), w.objnum, w.rect});
        }
    }
    std::stable_sort(stops.begin(), stops.end(),
                     [](const Stop& a, const Stop& b) { return a.rank < b.rank; });
    std::vector<std::pair<int, RectF>> ordered;
    std::set<std::string> radios;
    for (const Stop& s : stops) {
        const Field* f = form.fieldOf(page, s.objnum);
        if (f != nullptr && f->type == FieldType::RadioButton) {
            if (!radios.insert(f->name).second) {
                continue;
            }
        }
        ordered.emplace_back(s.objnum, s.rect);
    }
    return orderWidgets(ordered, order);
}

std::vector<int> tabSequence(const Document& doc, int page) {
    const Form form = fields_impl::read(doc, false);
    return tabSequence(form, page, tabOrder(doc, page), annotOrder(doc, page));
}

// ---------------------------------------------------------------- правка

namespace fields_impl {

// Значения values полю f (проверки, варианты, keystroke/validate MuPDF) внутри операции вызывающего: пусто —
// принято, иначе причина отказа ("not-an-option", "rejected", "not-a-value-field").
std::string assign(fz_context* ctx, pdf_document* pdf, const detail::WidgetPages& pages, const Field& f,
                   const std::vector<std::string>& values) {
    const bool choice = f.type == FieldType::ListBox || f.type == FieldType::ComboBox;
    if (choice && (f.multiSelect() || values.size() != 1)) {
        std::vector<std::pair<int, std::string>> picked;
        for (const std::string& v : values) {
            const std::optional<std::size_t> i = optionIndex(f, v);
            if (!i) {
                return "not-an-option";
            }
            picked.emplace_back(static_cast<int>(*i), f.options[*i].exportValue);
        }
        std::sort(picked.begin(), picked.end());
        picked.erase(std::unique(picked.begin(), picked.end()), picked.end());
        if (!f.multiSelect() && picked.size() > 1) {
            return "not-an-option";
        }
        std::vector<std::string> exports;
        std::vector<int> indices;
        for (const auto& [i, v] : picked) {
            indices.push_back(i);
            exports.push_back(v);
        }
        setChoiceValues(ctx, pdf, lookup(ctx, pdf, f.name), exports, indices, pages, f);
        return {};
    }
    if (values.size() != 1) {
        return "not-a-value-field";
    }
    std::string v = values.front();
    if (f.type == FieldType::CheckBox || f.type == FieldType::RadioButton) {
        const std::optional<std::string> state = stateFor(f, v);
        if (!state) {
            return "not-an-option";
        }
        setButtonState(ctx, pdf, lookup(ctx, pdf, f.name), *state, pages, f);
        return {};
    }
    if (f.type == FieldType::Text) {
        if (f.maxLen > 0) {
            v = truncateChars(v, f.maxLen);
        }
    } else if (!v.empty()) {  // combo, список с одним значением
        if (const std::optional<std::size_t> i = optionIndex(f, v)) {
            v = f.options[*i].exportValue;
        } else if (!f.editable()) {
            return "not-an-option";
        }
    }
    pdf_annot* annot = pages.find(f.widgets.front().objnum).first;
    if (annot == nullptr) {
        throw MuError(FZ_ERROR_ARGUMENT, "field " + f.name + ": widget is not on a page");
    }
    int rc = 0;
    const char* text = v.c_str();
    mu::call(ctx, [&] { rc = pdf_set_text_field_value(ctx, annot, text); });
    return rc != 0 ? std::string() : std::string("rejected");
}

}  // namespace fields_impl

SetResult setValue(Document& doc, std::string_view name, std::string_view value, const SetOptions& options) {
    return setValues(doc, name, {std::string(value)}, options);
}

SetResult setValues(Document& doc, std::string_view name, const std::vector<std::string>& values,
                    const SetOptions& options) {
    using namespace fields_impl;
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    SetResult result;
    const std::optional<Field> field = readField(doc, name);
    if (const std::optional<std::string> refused = refusal(field)) {
        result.error = *refused;
        if (field) {
            result.value = field->value;
        }
        return result;
    }
    result.value = field->value;
    Journal::Operation op(doc, "Fill field");
    beginUserAction(doc);
    detail::WidgetPages pages(doc);
    result.error = assign(ctx, pdf, pages, *field, values);
    const bool ok = result.error.empty();
    if (!options.recalculate) {
        setRecalculate(pdf, false);
    }
    result.pages = unite(pages.update(), ok ? pagesOf(*field) : std::vector<int>{});
    for (const int p : result.pages) {
        op.invalidate(p);
    }
    op.commit();
    result.accepted = ok;
    const std::optional<Field> after = readField(doc, name);
    result.value = after ? after->value : std::string();
    return result;
}

BatchResult setMany(Document& doc, const std::vector<Assignment>& list, const SetOptions& options) {
    using namespace fields_impl;
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    BatchResult result;
    const Form form = read(doc, false);
    Journal::Operation op(doc, "Fill fields");
    beginUserAction(doc);
    detail::WidgetPages pages(doc);
    std::vector<int> touched;
    for (const Assignment& a : list) {
        const Field* f = form.find(a.name);
        if (f == nullptr) {
            result.unknown.push_back(a.name);
            continue;
        }
        const std::optional<std::string> refused = refusal(*f);
        if (refused) {
            (*refused == "not-a-value-field" ? result.rejected : result.readOnly).push_back(a.name);
            continue;
        }
        if (assign(ctx, pdf, pages, *f, a.values).empty()) {
            result.updated.push_back(a.name);
            touched = unite(std::move(touched), pagesOf(*f));
        } else {
            result.rejected.push_back(a.name);
        }
    }
    if (!options.recalculate) {
        setRecalculate(pdf, false);
    }
    result.pages = unite(pages.update(), touched);
    for (const int p : result.pages) {
        op.invalidate(p);
    }
    op.commit();
    return result;
}

SetResult toggle(Document& doc, int page, int objnum, const SetOptions& options) {
    using namespace fields_impl;
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    SetResult result;
    const Form form = read(doc, false);
    const Field* field = form.fieldOf(page, objnum);
    if (field == nullptr) {
        result.error = "unknown-field";
        return result;
    }
    result.value = field->value;
    if (field->type != FieldType::CheckBox && field->type != FieldType::RadioButton) {
        result.error = "not-a-value-field";
        return result;
    }
    if (field->locked || field->readOnly()) {
        result.error = field->locked ? "locked" : "read-only";
        return result;
    }
    Journal::Operation op(doc, "Fill field");
    beginUserAction(doc);
    detail::WidgetPages pages(doc);
    pdf_annot* annot = pages.find(objnum).first;
    if (annot == nullptr) {
        result.error = "unknown-field";
        return result;
    }
    int rc = 0;
    mu::call(ctx, [&] { rc = pdf_toggle_widget(ctx, annot); });
    if (!options.recalculate) {
        setRecalculate(pdf, false);
    }
    result.pages = unite(pages.update(), pagesOf(*field));
    for (const int p : result.pages) {
        op.invalidate(p);
    }
    op.commit();
    result.accepted = rc != 0;
    result.value = readField(doc, field->name)->value;
    return result;
}

std::vector<int> resetForm(Document& doc, const std::vector<std::string>& names, bool exclude) {
    using namespace fields_impl;
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    const Form form = read(doc, false);
    const auto listed = [&names](const std::string& name) {
        return std::any_of(names.begin(), names.end(), [&name](const std::string& n) {
            return name == n ||
                   (name.size() > n.size() && name.compare(0, n.size(), n) == 0 && name[n.size()] == '.');
        });
    };
    std::vector<const Field*> targets;
    for (const Field& f : form.fields) {
        const bool chosen = names.empty() || (exclude ? !listed(f.name) : listed(f.name));
        if (chosen && !f.locked && f.type != FieldType::Signature && f.type != FieldType::PushButton) {
            targets.push_back(&f);
        }
    }
    Journal::Operation op(doc, "Reset form");
    beginUserAction(doc);
    detail::WidgetPages pages(doc);
    std::vector<int> touched;
    for (const Field* f : targets) {
        pdf_obj* obj = lookup(ctx, pdf, f->name);
        if (obj == nullptr) {
            continue;
        }
        mu::call(ctx, [&] { pdf_field_reset(ctx, pdf, obj); });
        // pdf_field_reset ставит /AS по /DV, но вид флажков не помечает изменившимся.
        for (const Widget& w : f->widgets) {
            if (pdf_annot* annot = pages.find(w.objnum).first) {
                mu::call(ctx, [&] { pdf_dirty_annot(ctx, annot); });
            }
        }
        touched = unite(std::move(touched), pagesOf(*f));
    }
    setRecalculate(pdf, true);
    const std::vector<int> result = unite(pages.update(), touched);
    for (const int p : result) {
        op.invalidate(p);
    }
    op.commit();
    return result;
}

std::vector<int> recalculate(Document& doc) {
    pdf_document* pdf = detail::pdfOf(doc);
    if (!jsEnabled(doc)) {
        return {};
    }
    Journal::Operation op(doc, "Calculate form");
    detail::WidgetPages pages(doc);
    fields_impl::setRecalculate(pdf, true);
    const std::vector<int> result = pages.update();
    for (const int p : result) {
        op.invalidate(p);
    }
    op.commit();
    return result;
}

std::string formattedValue(Document& doc, std::string_view name) {
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    const std::optional<Field> field = readField(doc, name);
    if (!field) {
        return {};
    }
    if (!jsEnabled(doc) || field->formatScript.empty()) {
        return field->value;
    }
    pdf_obj* obj = fields_impl::lookup(ctx, pdf, field->name);
    Journal::Operation op(doc, "Format field");
    char* text = nullptr;
    mu::call(ctx, [&] { text = pdf_field_event_format(ctx, pdf, obj); });
    op.commit();
    std::string out = text != nullptr ? text : field->value;
    fz_free(ctx, text);
    return out;
}

ClickResult click(Document& doc, int page, int objnum) {
    using namespace fields_impl;
    pdf_document* pdf = detail::pdfOf(doc);
    fz_context* ctx = doc.context().get();
    ClickResult result;
    const Form form = read(doc, false);
    const Field* field = form.fieldOf(page, objnum);
    pdf_obj* widgetObj = nullptr;
    mu::call(ctx, [&] { widgetObj = pdf_load_object(ctx, pdf, objnum); });
    pdf_obj* action = nullptr;
    mu::call(ctx, [&] { action = pdf_dict_get(ctx, widgetObj, PDF_NAME(A)); });
    result.action = parseAction(ctx, pdf, action);
    if (result.action.kind == ActionKind::None && field != nullptr) {
        result.action = field->action;
    }
    mu::call(ctx, [&] { pdf_drop_obj(ctx, widgetObj); });

    Journal::Operation op(doc, "Click button");
    beginUserAction(doc);
    if (result.action.kind == ActionKind::ResetForm && !result.action.hasNext) {
        // Свой сброс: MuPDF 1.28.5 не понимает /Flags «все, кроме» для полей вне списка.
        result.pages = resetForm(doc, result.action.fields, (result.action.flags & 1) != 0);
    } else {
        detail::WidgetPages pages(doc);
        pdf_annot* annot = pages.find(objnum).first;
        if (annot != nullptr) {
            mu::call(ctx, [&] {
                pdf_annot_event_down(ctx, annot);
                pdf_annot_event_up(ctx, annot);
            });
        }
        result.pages = pages.update();
    }
    for (const int p : result.pages) {
        op.invalidate(p);
    }
    op.commit();
    return result;
}

std::vector<int> focus(Document& doc, int page, int objnum, bool in) {
    (void)page;
    if (!jsEnabled(doc)) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    Journal::Operation op(doc, in ? "Field focus" : "Field blur");
    beginUserAction(doc);
    detail::WidgetPages pages(doc);
    pdf_annot* annot = pages.find(objnum).first;
    if (annot != nullptr) {
        mu::call(ctx, [&] {
            if (in) {
                pdf_annot_event_focus(ctx, annot);
            } else {
                pdf_annot_event_blur(ctx, annot);
            }
        });
    }
    const std::vector<int> result = pages.update();
    for (const int p : result) {
        op.invalidate(p);
    }
    op.commit();
    return result;
}

}  // namespace ot::forms
