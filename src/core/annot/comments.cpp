#include "core/annot/comments.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#include "core/annot/annot_internal.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/error.h"
#include "core/util/hash.h"
#include "core/util/utf8.h"

namespace ot::annot {

namespace {

constexpr std::array<std::string_view, kReviewStateCount> kReviewStateNames = {"None", "Accepted", "Rejected",
                                                                               "Cancelled", "Completed"};

// Одна запись /Annots, прочитанная внутри mu::call: строки — указатели на строки объекта (кеш текста MuPDF),
// копируются сразу после вызова.
struct CommentRaw {
    int objnum = 0;
    int flags = 0;
    fz_rect rect{};
    char subtype[64] = {};
    const char* nm = nullptr;
    const char* author = nullptr;
    const char* contents = nullptr;
    const char* rc = nullptr;
    const char* subject = nullptr;
    const char* intent = nullptr;
    const char* icon = nullptr;
    const char* rt = nullptr;
    const char* state = nullptr;
    const char* stateModel = nullptr;
    int hasState = 0;
    int colorN = 0;
    float color[4] = {};
    std::int64_t created = 0;
    std::int64_t modified = 0;
    int irt = 0;
};

void readRaw(fz_context* ctx, pdf_obj* obj, pdf_annot* annot, fz_matrix ctm, CommentRaw* out) {
    out->objnum = pdf_to_num(ctx, obj);
    out->flags = pdf_dict_get_int(ctx, obj, PDF_NAME(F));
    const char* name = pdf_dict_get_name(ctx, obj, PDF_NAME(Subtype));
    strncpy_s(out->subtype, sizeof(out->subtype), name != nullptr ? name : "", _TRUNCATE);
    out->rect = annot != nullptr ? pdf_bound_annot(ctx, annot)
                                 : fz_transform_rect(pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect)), ctm);
    out->nm = pdf_dict_get_text_string(ctx, obj, PDF_NAME(NM));
    out->author = pdf_dict_get_text_string(ctx, obj, PDF_NAME(T));
    out->contents = pdf_dict_get_text_string(ctx, obj, PDF_NAME(Contents));
    if (pdf_dict_get(ctx, obj, PDF_NAME(RC)) != nullptr) {
        out->rc = pdf_dict_get_text_string(ctx, obj, PDF_NAME(RC));
    }
    out->subject = pdf_dict_get_text_string(ctx, obj, PDF_NAME(Subj));
    out->intent = pdf_dict_get_name(ctx, obj, PDF_NAME(IT));
    out->icon = pdf_dict_get_name(ctx, obj, PDF_NAME(Name));
    out->rt = pdf_dict_get_name(ctx, obj, PDF_NAME(RT));
    pdf_obj* state = pdf_dict_gets(ctx, obj, "State");
    pdf_obj* model = pdf_dict_gets(ctx, obj, "StateModel");
    out->hasState = state != nullptr || model != nullptr ? 1 : 0;
    // /State и /StateModel — текстовые строки (ISO 32000, табл. 172); у некоторых программ — имена.
    out->state = pdf_is_name(ctx, state) ? pdf_to_name(ctx, state) : pdf_to_text_string(ctx, state);
    out->stateModel = pdf_is_name(ctx, model) ? pdf_to_name(ctx, model) : pdf_to_text_string(ctx, model);
    pdf_obj* c = pdf_dict_get(ctx, obj, PDF_NAME(C));
    const int n = pdf_array_len(ctx, c);
    if (n == 1 || n == 3 || n == 4) {
        out->colorN = n;
        for (int i = 0; i < n; ++i) {
            out->color[i] = pdf_array_get_real(ctx, c, i);
        }
    }
    out->created = pdf_dict_get_date(ctx, obj, PDF_NAME(CreationDate));
    out->modified = pdf_dict_get_date(ctx, obj, PDF_NAME(M));
    out->irt = pdf_to_num(ctx, pdf_dict_get(ctx, obj, PDF_NAME(IRT)));
}

std::string str(const char* s) { return s != nullptr ? std::string(s) : std::string(); }

// Запись документа до разбора по веткам.
struct Entry {
    Comment comment;
    int irt = 0;
    std::string rt;
    bool status = false;
    std::string state;
    std::string model;
};

enum class Role { Skip, Root, Reply, Group, Status };

Role roleOf(const Entry& e, const std::unordered_map<int, std::size_t>& byNum,
            const std::vector<Entry>& entries) {
    const AnnotType type = e.comment.type;
    if (type == AnnotType::Popup || !isComment(type)) {
        return Role::Skip;
    }
    if (e.irt <= 0 || e.irt == e.comment.ref.objnum) {
        return Role::Root;
    }
    const auto it = byNum.find(e.irt);
    const bool parentKnown = it != byNum.end() && isComment(entries[it->second].comment.type) &&
                             entries[it->second].comment.type != AnnotType::Popup;
    if (e.status) {
        return parentKnown ? Role::Status : Role::Skip;
    }
    if (!parentKnown) {
        return Role::Root;  // ответ без родителя (родитель удалён в чужой программе) — не теряется
    }
    return e.rt == "Group" ? Role::Group : Role::Reply;
}

bool dateLess(const Comment& a, const Comment& b) {
    const std::int64_t da = a.created != 0 ? a.created : a.modified;
    const std::int64_t db = b.created != 0 ? b.created : b.modified;
    if (da != db) {
        return da < db;
    }
    if (a.ref.page != b.ref.page) {
        return a.ref.page < b.ref.page;
    }
    return a.index < b.index;
}

const Comment* findIn(const std::vector<Comment>& list, const AnnotRef& ref) {
    for (const Comment& c : list) {
        if (c.ref == ref) {
            return &c;
        }
        for (const AnnotRef& g : c.group) {
            if (g == ref) {
                return &c;
            }
        }
        if (const Comment* r = findIn(c.replies, ref)) {
            return r;
        }
    }
    return nullptr;
}

bool contains(const Comment& c, const AnnotRef& ref) {
    if (c.ref == ref) {
        return true;
    }
    for (const AnnotRef& g : c.group) {
        if (g == ref) {
            return true;
        }
    }
    for (const StatusEntry& s : c.history) {
        if (s.ref == ref) {
            return true;
        }
    }
    return std::any_of(c.replies.begin(), c.replies.end(),
                       [&](const Comment& r) { return contains(r, ref); });
}

const Comment* findStatusOwner(const std::vector<Comment>& list, const AnnotRef& ref) {
    for (const Comment& c : list) {
        for (const StatusEntry& s : c.history) {
            if (s.ref == ref) {
                return &c;
            }
        }
        if (const Comment* r = findStatusOwner(c.replies, ref)) {
            return r;
        }
    }
    return nullptr;
}

void collectBranch(const Comment& c, std::vector<AnnotRef>& out) {
    out.push_back(c.ref);
    for (const AnnotRef& g : c.group) {
        out.push_back(g);
    }
    for (const StatusEntry& s : c.history) {
        out.push_back(s.ref);
    }
    for (const Comment& r : c.replies) {
        collectBranch(r, out);
    }
}

void forEachIn(const std::vector<Comment>& list, const std::function<void(const Comment&)>& fn) {
    for (const Comment& c : list) {
        fn(c);
        forEachIn(c.replies, fn);
    }
}

// Найти родителя и проверить, что это комментарий (ответ и статус ставятся только на комментарий).
AnnotInfo targetOf(Document& doc, const AnnotRef& ref) {
    const std::optional<AnnotInfo> info = findAnnot(doc, ref);
    if (!info || !isComment(info->type)) {
        detail::badArgument("comment " + std::to_string(ref.objnum) + " is not on page " +
                            std::to_string(ref.page));
    }
    return *info;
}

// Записка ответа или статуса: рамка и цвет родителя, скрыта на странице, без окна.
AnnotProps serviceNote(const AnnotInfo& parent) {
    AnnotProps p = toolDefaults(AnnotType::Text);
    p.rect = parent.rect;
    if (p.rect.empty()) {
        p.rect = {parent.rect.x0, parent.rect.y0, parent.rect.x0 + 20, parent.rect.y0 + 20};
    }
    p.flags = kServiceAnnotFlags;
    p.icon = "Comment";
    if (!parent.color.empty()) {
        p.color = parent.color;
    }
    return p;
}

// ---------------------------------------------------------------- /RC

void appendEscaped(std::string& out, std::string_view text) {
    for (const char ch : text) {
        switch (ch) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += "&quot;";
                break;
            default:
                out.push_back(ch);
        }
    }
}

std::string hexColor(std::uint32_t rgb) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out = "#";
    for (int shift = 20; shift >= 0; shift -= 4) {
        out.push_back(kHex[(rgb >> shift) & 0xF]);
    }
    return out;
}

std::string styleOf(const RichSpan& s) {
    std::string style;
    if (s.bold) {
        style += "font-weight:bold;";
    }
    if (s.italic) {
        style += "font-style:italic;";
    }
    if (s.underline) {
        style += "text-decoration:underline;";
    }
    if (s.color != kNoRichColor) {
        style += "color:" + hexColor(s.color) + ";";
    }
    if (!style.empty()) {
        style.pop_back();
    }
    return style;
}

struct RichStyle {
    bool bold = false;
    bool italic = false;
    bool underline = false;
    std::uint32_t color = kNoRichColor;
};

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return out;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

std::optional<std::uint32_t> parseCssColor(std::string_view v) {
    v = trim(v);
    if (!v.empty() && v.front() == '#') {
        std::string hex(v.substr(1));
        if (hex.size() == 3) {
            hex = {hex[0], hex[0], hex[1], hex[1], hex[2], hex[2]};
        }
        if (hex.size() != 6) {
            return std::nullopt;
        }
        try {
            return static_cast<std::uint32_t>(std::stoul(hex, nullptr, 16));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    const std::string l = lower(v);
    if (l.rfind("rgb(", 0) == 0 && l.back() == ')') {
        std::uint32_t rgb = 0;
        std::string_view body(l);
        body = body.substr(4, body.size() - 5);
        for (int i = 0; i < 3; ++i) {
            const std::size_t comma = body.find(',');
            const std::string part(trim(body.substr(0, comma)));
            int value = 0;
            try {
                value = std::clamp(std::stoi(part), 0, 255);
            } catch (const std::exception&) {
                return std::nullopt;
            }
            rgb = (rgb << 8) | static_cast<std::uint32_t>(value);
            if (comma == std::string_view::npos) {
                if (i != 2) {
                    return std::nullopt;
                }
                break;
            }
            body.remove_prefix(comma + 1);
        }
        return rgb;
    }
    return std::nullopt;
}

void applyCss(RichStyle& st, std::string_view css) {
    while (!css.empty()) {
        const std::size_t semi = css.find(';');
        const std::string_view decl = css.substr(0, semi);
        css = semi == std::string_view::npos ? std::string_view{} : css.substr(semi + 1);
        const std::size_t colon = decl.find(':');
        if (colon == std::string_view::npos) {
            continue;
        }
        const std::string key = lower(trim(decl.substr(0, colon)));
        const std::string value = lower(trim(decl.substr(colon + 1)));
        if (key == "font-weight") {
            st.bold = value == "bold" || value == "bolder" || value == "600" || value == "700" ||
                      value == "800" || value == "900";
        } else if (key == "font-style") {
            st.italic = value == "italic" || value == "oblique";
        } else if (key == "text-decoration") {
            st.underline = value.find("underline") != std::string::npos;
        } else if (key == "color") {
            if (const auto c = parseCssColor(value)) {
                st.color = *c;
            }
        } else if (key == "font") {
            if (value.find("bold") != std::string::npos) {
                st.bold = true;
            }
            if (value.find("italic") != std::string::npos) {
                st.italic = true;
            }
        }
    }
}

// Значение атрибута name в теле тега (без учёта регистра имени); нет — nullopt.
std::optional<std::string> attribute(std::string_view tag, std::string_view name) {
    std::size_t i = 0;
    while (i < tag.size()) {
        while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t' || tag[i] == '\r' || tag[i] == '\n')) {
            ++i;
        }
        const std::size_t start = i;
        while (i < tag.size() && tag[i] != '=' && tag[i] != ' ' && tag[i] != '\t' && tag[i] != '\r' &&
               tag[i] != '\n' && tag[i] != '/') {
            ++i;
        }
        const std::string key = lower(tag.substr(start, i - start));
        while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t')) {
            ++i;
        }
        if (i >= tag.size() || tag[i] != '=') {
            if (i == start) {
                ++i;
            }
            continue;
        }
        ++i;
        while (i < tag.size() && (tag[i] == ' ' || tag[i] == '\t')) {
            ++i;
        }
        std::string value;
        if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
            const char q = tag[i++];
            const std::size_t end = tag.find(q, i);
            value =
                std::string(tag.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
            i = end == std::string_view::npos ? tag.size() : end + 1;
        } else {
            const std::size_t vs = i;
            while (i < tag.size() && tag[i] != ' ' && tag[i] != '\t') {
                ++i;
            }
            value = std::string(tag.substr(vs, i - vs));
        }
        if (key == name) {
            return value;
        }
    }
    return std::nullopt;
}

std::string decodeEntities(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&') {
            out.push_back(text[i++]);
            continue;
        }
        const std::size_t semi = text.find(';', i);
        if (semi == std::string_view::npos || semi - i > 12) {
            out.push_back(text[i++]);
            continue;
        }
        const std::string_view name = text.substr(i + 1, semi - i - 1);
        if (name == "amp") {
            out.push_back('&');
        } else if (name == "lt") {
            out.push_back('<');
        } else if (name == "gt") {
            out.push_back('>');
        } else if (name == "quot") {
            out.push_back('"');
        } else if (name == "apos") {
            out.push_back('\'');
        } else if (name == "nbsp") {
            utf8::append(out, U' ');
        } else if (!name.empty() && name[0] == '#') {
            char32_t code = 0;
            try {
                code = name.size() > 1 && (name[1] == 'x' || name[1] == 'X')
                           ? static_cast<char32_t>(std::stoul(std::string(name.substr(2)), nullptr, 16))
                           : static_cast<char32_t>(std::stoul(std::string(name.substr(1)), nullptr, 10));
            } catch (const std::exception&) {
                code = 0xFFFD;
            }
            utf8::append(out, code);
        } else {
            out.append(text.substr(i, semi - i + 1));
        }
        i = semi + 1;
    }
    return out;
}

void pushText(std::vector<RichSpan>& spans, const RichStyle& st, std::string_view text) {
    if (text.empty()) {
        return;
    }
    if (!spans.empty()) {
        RichSpan& last = spans.back();
        if (last.bold == st.bold && last.italic == st.italic && last.underline == st.underline &&
            last.color == st.color) {
            last.text.append(text);
            return;
        }
    }
    spans.push_back({std::string(text), st.bold, st.italic, st.underline, st.color});
}

}  // namespace

// ---------------------------------------------------------------- значения

std::string_view reviewStateName(ReviewState state) noexcept {
    const int i = static_cast<int>(state);
    return i >= 0 && i < kReviewStateCount ? kReviewStateNames[static_cast<std::size_t>(i)]
                                           : std::string_view{};
}

ReviewState reviewStateFromName(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kReviewStateNames.size(); ++i) {
        if (kReviewStateNames[i] == name) {
            return static_cast<ReviewState>(i);
        }
    }
    return ReviewState::None;
}

ReviewState Comment::review() const noexcept {
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->model == kReviewModel) {
            return reviewStateFromName(it->state);
        }
    }
    return ReviewState::None;
}

std::string Comment::reviewAuthor() const {
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->model == kReviewModel) {
            return reviewStateFromName(it->state) == ReviewState::None ? std::string() : it->author;
        }
    }
    return {};
}

bool Comment::marked() const noexcept {
    for (auto it = history.rbegin(); it != history.rend(); ++it) {
        if (it->model == kMarkedModel) {
            return it->state == "Marked";
        }
    }
    return false;
}

int Comment::replyCount() const noexcept {
    int n = 0;
    for (const Comment& r : replies) {
        n += 1 + r.replyCount();
    }
    return n;
}

const Comment* CommentTree::find(const AnnotRef& ref) const { return findIn(roots, ref); }

const Comment* CommentTree::rootOf(const AnnotRef& ref) const {
    for (const Comment& c : roots) {
        if (contains(c, ref)) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<AnnotRef> CommentTree::branch(const AnnotRef& ref) const {
    std::vector<AnnotRef> out;
    const Comment* c = find(ref);
    if (c == nullptr) {
        c = findStatusOwner(roots, ref);
        if (c == nullptr) {
            return out;
        }
        for (const StatusEntry& s : c->history) {
            if (s.ref == ref) {
                out.push_back(s.ref);  // статус — только он сам
            }
        }
        return out;
    }
    collectBranch(*c, out);
    return out;
}

void CommentTree::forEach(const std::function<void(const Comment&)>& fn) const { forEachIn(roots, fn); }

int CommentTree::count() const {
    int n = 0;
    forEach([&](const Comment&) { ++n; });
    return n;
}

bool isServiceAnnot(const AnnotInfo& info) noexcept {
    if (info.type == AnnotType::Popup) {
        return true;
    }
    return info.type == AnnotType::Text && info.inReplyTo > 0 && info.replyType != "Group";
}

// ---------------------------------------------------------------- дерево

CommentTree buildComments(Document& doc) {
    CommentTree tree;
    if (doc.pdf() == nullptr) {
        return tree;
    }
    tree.pageCount = doc.pageCount();
    std::vector<Entry> entries;
    for (int page = 0; page < tree.pageCount; ++page) {
        detail::PageAnnots pa(doc, page);
        fz_context* ctx = pa.ctx();
        pdf_obj* annots = pa.annots();
        int n = 0;
        mu::call(ctx, [&] { n = pdf_array_len(ctx, annots); });
        for (int i = 0; i < n; ++i) {
            int isDict = 0;
            int num = 0;
            mu::call(ctx, [&] {
                pdf_obj* obj = pdf_array_get(ctx, annots, i);
                isDict = pdf_is_dict(ctx, obj);
                num = pdf_to_num(ctx, obj);
            });
            if (isDict == 0 || num <= 0) {
                continue;
            }
            CommentRaw raw;
            pdf_annot* annot = pa.find(num);
            const fz_matrix ctm = pa.ctm();
            mu::call(ctx, [&] { readRaw(ctx, pdf_array_get(ctx, annots, i), annot, ctm, &raw); });
            Entry e;
            Comment& c = e.comment;
            c.ref = {page, raw.objnum, str(raw.nm)};
            c.subtype = raw.subtype;
            c.type = annotTypeFromName(c.subtype);
            c.rect = {raw.rect.x0, raw.rect.y0, raw.rect.x1, raw.rect.y1};
            c.flags = raw.flags;
            c.author = str(raw.author);
            c.subject = str(raw.subject);
            c.contents = str(raw.contents);
            c.richContents = str(raw.rc);
            c.intent = str(raw.intent);
            c.icon = str(raw.icon);
            c.color.n = raw.colorN;
            for (int k = 0; k < raw.colorN; ++k) {
                c.color.c[static_cast<std::size_t>(k)] = raw.color[k];
            }
            c.created = raw.created;
            c.modified = raw.modified;
            c.index = i;
            e.irt = raw.irt;
            e.rt = str(raw.rt);
            e.status = raw.hasState != 0;
            e.state = str(raw.state);
            e.model = str(raw.stateModel);
            if (e.status && e.model.empty()) {
                // /StateModel не задан: по /State (ISO 32000, табл. 172).
                e.model = e.state == "Marked" || e.state == "Unmarked" ? std::string(kMarkedModel)
                                                                       : std::string(kReviewModel);
            }
            entries.push_back(std::move(e));
        }
    }
    std::unordered_map<int, std::size_t> byNum;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        byNum.emplace(entries[i].comment.ref.objnum, i);
    }
    std::vector<Role> roles(entries.size(), Role::Skip);
    std::vector<std::vector<std::size_t>> replies(entries.size());
    std::vector<std::vector<std::size_t>> statuses(entries.size());
    std::vector<std::vector<std::size_t>> groups(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        roles[i] = roleOf(entries[i], byNum, entries);
        if (roles[i] == Role::Skip || roles[i] == Role::Root) {
            continue;
        }
        const std::size_t parent = byNum.at(entries[i].irt);
        if (roles[i] == Role::Reply) {
            replies[parent].push_back(i);
        } else if (roles[i] == Role::Status) {
            statuses[parent].push_back(i);
        } else {
            groups[parent].push_back(i);
        }
    }
    // Сборка веток в глубину; петли /IRT (испорченный файл) — по посещённым: недостижимые из корней ответы
    // становятся корнями.
    std::vector<char> visited(entries.size(), 0);
    std::function<Comment(std::size_t, int)> assemble = [&](std::size_t i, int depth) {
        visited[i] = 1;
        Comment c = std::move(entries[i].comment);
        c.depth = depth;
        for (const std::size_t g : groups[i]) {
            visited[g] = 1;
            c.group.push_back(entries[g].comment.ref);
        }
        std::vector<std::size_t> st = statuses[i];
        std::stable_sort(st.begin(), st.end(), [&](std::size_t a, std::size_t b) {
            return dateLess(entries[a].comment, entries[b].comment);
        });
        for (const std::size_t s : st) {
            visited[s] = 1;
            const Comment& sc = entries[s].comment;
            c.history.push_back({sc.ref, entries[s].model, entries[s].state, sc.author, sc.date()});
        }
        std::vector<std::size_t> rs;
        for (const std::size_t r : replies[i]) {
            if (visited[r] == 0) {
                rs.push_back(r);
            }
        }
        std::stable_sort(rs.begin(), rs.end(), [&](std::size_t a, std::size_t b) {
            return dateLess(entries[a].comment, entries[b].comment);
        });
        for (const std::size_t r : rs) {
            if (visited[r] == 0) {
                c.replies.push_back(assemble(r, depth + 1));
            }
        }
        return c;
    };
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (roles[i] == Role::Root && visited[i] == 0) {
            tree.roots.push_back(assemble(i, 0));
        }
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (roles[i] == Role::Reply && visited[i] == 0) {
            tree.roots.push_back(assemble(i, 0));
        }
    }
    std::stable_sort(tree.roots.begin(), tree.roots.end(), [](const Comment& a, const Comment& b) {
        return a.ref.page != b.ref.page ? a.ref.page < b.ref.page : a.index < b.index;
    });
    return tree;
}

AnnotRef addReply(Document& doc, const AnnotRef& parent, const ReplyProps& reply,
                  const std::string& operation) {
    detail::pdfOf(doc);
    if (!parent.valid() || parent.page >= doc.pageCount()) {
        detail::notFound(parent);
    }
    const AnnotInfo target = targetOf(doc, parent);
    AnnotProps p = serviceNote(target);
    p.author = reply.author;
    p.contents = reply.contents;
    p.richContents = reply.richContents;
    p.nm = reply.nm;
    p.created = reply.created;
    p.modified = reply.created;
    detail::PageAnnots pa(doc, parent.page);
    Journal::Operation op(doc, operation);
    const AnnotRef ref = detail::createIn(doc, pa, p, parent.objnum, "R", false);
    op.invalidate(parent.page);
    op.commit();
    return ref;
}

AnnotRef setStatus(Document& doc, const AnnotRef& target, std::string_view model, std::string_view state,
                   const std::string& author, std::int64_t when) {
    detail::pdfOf(doc);
    if (!target.valid() || target.page >= doc.pageCount()) {
        detail::notFound(target);
    }
    const bool review = model == kReviewModel;
    const bool marked = model == kMarkedModel;
    if (!review && !marked) {
        detail::badArgument("unknown state model " + std::string(model));
    }
    const bool known = review ? std::find(kReviewStateNames.begin(), kReviewStateNames.end(), state) !=
                                    kReviewStateNames.end()
                              : state == "Marked" || state == "Unmarked";
    if (!known) {
        detail::badArgument("unknown state " + std::string(state) + " of model " + std::string(model));
    }
    const AnnotInfo info = targetOf(doc, target);
    AnnotProps p = serviceNote(info);
    p.author = author;
    p.contents = std::string(state) + " set by " + author;
    p.created = when;
    p.modified = when;
    detail::PageAnnots pa(doc, target.page);
    Journal::Operation op(doc, marked ? "Set check mark" : "Set status");
    const AnnotRef ref = detail::createIn(doc, pa, p, target.objnum, "R", false);
    pdf_obj* obj = pa.object(ref.objnum);
    fz_context* ctx = pa.ctx();
    const std::string stateText(state);
    const std::string modelText(model);
    mu::call(ctx, [&] {
        pdf_dict_puts_drop(ctx, obj, "State", pdf_new_text_string(ctx, stateText.c_str()));
        pdf_dict_puts_drop(ctx, obj, "StateModel", pdf_new_text_string(ctx, modelText.c_str()));
    });
    op.invalidate(target.page);
    op.commit();
    return ref;
}

std::string commentsDocumentKey(Document& doc) {
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return {};
    }
    fz_context* ctx = doc.context().get();
    std::string id;
    mu::call(ctx, [&] {
        pdf_obj* first = pdf_array_get(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(ID)), 0);
        if (pdf_is_string(ctx, first)) {
            id.assign(pdf_to_str_buf(ctx, first), pdf_to_str_len(ctx, first));
        }
    });
    return id.empty() ? std::string() : hash::sha256Hex(id);
}

// ---------------------------------------------------------------- сортировка и фильтр

bool CommentFilter::active() const noexcept {
    return types != 0 || !authors.empty() || !states.empty() || onlyMine || unreadOnly || markedOnly ||
           !colors.empty() || !search.empty();
}

std::string foldCase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char32_t c : utf8::decode(text)) {
        utf8::append(out, static_cast<char32_t>(fz_tolower(static_cast<int>(c))));
    }
    return out;
}

namespace {

bool textFound(const Comment& c, const std::string& needle) {
    if (foldCase(c.contents).find(needle) != std::string::npos) {
        return true;
    }
    return std::any_of(c.replies.begin(), c.replies.end(),
                       [&](const Comment& r) { return textFound(r, needle); });
}

bool anyUnread(const Comment& c, const std::function<bool(const Comment&)>& isUnread) {
    if (isUnread(c)) {
        return true;
    }
    return std::any_of(c.replies.begin(), c.replies.end(),
                       [&](const Comment& r) { return anyUnread(r, isUnread); });
}

}  // namespace

bool matches(const Comment& comment, const CommentFilter& filter,
             const std::function<bool(const Comment&)>& isUnread) {
    const int type = static_cast<int>(comment.type);
    if (filter.types != 0 && (type < 0 || type >= 32 || (filter.types & (1u << type)) == 0)) {
        return false;
    }
    if (!filter.authors.empty() &&
        std::find(filter.authors.begin(), filter.authors.end(), comment.author) == filter.authors.end()) {
        return false;
    }
    if (!filter.states.empty() &&
        std::find(filter.states.begin(), filter.states.end(), comment.review()) == filter.states.end()) {
        return false;
    }
    if (filter.onlyMine && comment.author != filter.me) {
        return false;
    }
    if (filter.unreadOnly && (!isUnread || !anyUnread(comment, isUnread))) {
        return false;
    }
    if (filter.markedOnly && !comment.marked()) {
        return false;
    }
    if (!filter.colors.empty() &&
        (comment.color.empty() || std::find(filter.colors.begin(), filter.colors.end(),
                                            comment.color.toRgb()) == filter.colors.end())) {
        return false;
    }
    if (!filter.search.empty() && !textFound(comment, foldCase(filter.search))) {
        return false;
    }
    return true;
}

std::vector<const Comment*> arrange(const CommentTree& tree, CommentSort sort, const CommentFilter& filter,
                                    bool descending, const std::function<bool(const Comment&)>& isUnread) {
    std::vector<const Comment*> out;
    out.reserve(tree.roots.size());
    for (const Comment& c : tree.roots) {
        if (matches(c, filter, isUnread)) {
            out.push_back(&c);
        }
    }
    std::unordered_map<const Comment*, std::string> authors;
    if (sort == CommentSort::Author) {
        for (const Comment* c : out) {
            authors.emplace(c, foldCase(c->author));
        }
    }
    // Главный ключ: <0, 0, >0.
    const auto primary = [&](const Comment* a, const Comment* b) -> int {
        switch (sort) {
            case CommentSort::Page:
                return 0;
            case CommentSort::Author:
                return authors[a].compare(authors[b]);
            case CommentSort::Date:
                return a->date() < b->date() ? -1 : (a->date() > b->date() ? 1 : 0);
            case CommentSort::Type:
                return static_cast<int>(a->type) - static_cast<int>(b->type);
            case CommentSort::Status:
                return static_cast<int>(a->review()) - static_cast<int>(b->review());
        }
        return 0;
    };
    std::stable_sort(out.begin(), out.end(), [&](const Comment* a, const Comment* b) {
        const int p = primary(a, b);
        if (p != 0) {
            return descending ? p > 0 : p < 0;
        }
        if (sort == CommentSort::Page && descending && a->ref.page != b->ref.page) {
            return a->ref.page > b->ref.page;
        }
        if (a->ref.page != b->ref.page) {
            return a->ref.page < b->ref.page;
        }
        return descending && sort == CommentSort::Page ? a->index > b->index : a->index < b->index;
    });
    return out;
}

std::map<int, std::vector<int>> hiddenByFilter(const CommentTree& tree, const CommentFilter& filter,
                                               const std::function<bool(const Comment&)>& isUnread) {
    std::map<int, std::vector<int>> hidden;
    if (!filter.active()) {
        return hidden;
    }
    std::function<void(const Comment&)> hide = [&](const Comment& c) {
        hidden[c.ref.page].push_back(c.ref.objnum);
        for (const AnnotRef& g : c.group) {
            hidden[g.page].push_back(g.objnum);
        }
        for (const Comment& r : c.replies) {
            hide(r);
        }
    };
    for (const Comment& c : tree.roots) {
        if (!matches(c, filter, isUnread)) {
            hide(c);
        }
    }
    for (auto& [page, list] : hidden) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    return hidden;
}

// ---------------------------------------------------------------- /RC

std::string richContentsFrom(const std::vector<RichSpan>& spans) {
    std::string out =
        "<?xml version=\"1.0\"?><body xmlns=\"http://www.w3.org/1999/xhtml\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\" xfa:APIVersion=\"Acrobat:11.0.0\" "
        "xfa:spec=\"2.0.2\"><p dir=\"ltr\">";
    for (const RichSpan& s : spans) {
        std::string_view rest = s.text;
        const std::string style = styleOf(s);
        while (true) {
            const std::size_t nl = rest.find('\n');
            const std::string_view piece = rest.substr(0, nl);
            if (!piece.empty()) {
                if (style.empty()) {
                    appendEscaped(out, piece);
                } else {
                    out += "<span style=\"" + style + "\">";
                    appendEscaped(out, piece);
                    out += "</span>";
                }
            }
            if (nl == std::string_view::npos) {
                break;
            }
            out += "</p><p dir=\"ltr\">";
            rest.remove_prefix(nl + 1);
        }
    }
    out += "</p></body>";
    return out;
}

std::vector<RichSpan> parseRichContents(std::string_view xhtml) {
    std::vector<RichSpan> spans;
    const std::string_view trimmed = trim(xhtml);
    if (trimmed.empty()) {
        return spans;
    }
    if (trimmed.front() != '<') {
        spans.push_back({std::string(xhtml)});
        return spans;
    }
    struct Frame {
        std::string name;
        RichStyle style;
    };
    std::vector<Frame> stack;
    RichStyle base;
    int paragraphs = 0;
    bool sawElement = false;
    std::size_t i = 0;
    const auto current = [&]() -> const RichStyle& { return stack.empty() ? base : stack.back().style; };
    while (i < xhtml.size()) {
        if (xhtml[i] != '<') {
            const std::size_t end = xhtml.find('<', i);
            const std::string_view raw =
                xhtml.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i);
            i = end == std::string_view::npos ? xhtml.size() : end;
            // Пробелы между блоками (после <body>, между </p> и <p>) — не текст.
            const bool inBlockGap = stack.empty() || stack.back().name == "body";
            if (inBlockGap && trim(raw).empty()) {
                continue;
            }
            pushText(spans, current(), decodeEntities(raw));
            continue;
        }
        if (xhtml.compare(i, 4, "<!--") == 0) {
            const std::size_t end = xhtml.find("-->", i);
            i = end == std::string_view::npos ? xhtml.size() : end + 3;
            continue;
        }
        if (xhtml.compare(i, 2, "<?") == 0 || xhtml.compare(i, 2, "<!") == 0) {
            const std::size_t end = xhtml.find('>', i);
            i = end == std::string_view::npos ? xhtml.size() : end + 1;
            continue;
        }
        const std::size_t end = xhtml.find('>', i);
        if (end == std::string_view::npos) {
            break;
        }
        std::string_view tag = xhtml.substr(i + 1, end - i - 1);
        i = end + 1;
        sawElement = true;
        const bool closing = !tag.empty() && tag.front() == '/';
        if (closing) {
            tag.remove_prefix(1);
        }
        const bool selfClosing = !tag.empty() && tag.back() == '/';
        if (selfClosing) {
            tag.remove_suffix(1);
        }
        std::size_t nameEnd = 0;
        while (nameEnd < tag.size() && tag[nameEnd] != ' ' && tag[nameEnd] != '\t' && tag[nameEnd] != '\r' &&
               tag[nameEnd] != '\n') {
            ++nameEnd;
        }
        std::string name = lower(tag.substr(0, nameEnd));
        if (const std::size_t colon = name.find(':'); colon != std::string::npos) {
            name = name.substr(colon + 1);  // xhtml:p
        }
        const std::string_view attrs = tag.substr(nameEnd);
        if (closing) {
            for (std::size_t k = stack.size(); k > 0; --k) {
                if (stack[k - 1].name == name) {
                    stack.resize(k - 1);
                    break;
                }
            }
            continue;
        }
        if (name == "br") {
            pushText(spans, current(), "\n");
            continue;
        }
        if (name == "p" || name == "div") {
            if (paragraphs > 0) {
                pushText(spans, current(), "\n");
            }
            ++paragraphs;
        }
        RichStyle st = current();
        if (name == "b" || name == "strong") {
            st.bold = true;
        } else if (name == "i" || name == "em") {
            st.italic = true;
        } else if (name == "u") {
            st.underline = true;
        } else if (name == "font") {
            if (const auto color = attribute(attrs, "color")) {
                if (const auto c = parseCssColor(*color)) {
                    st.color = *c;
                }
            }
        }
        if (const auto css = attribute(attrs, "style")) {
            applyCss(st, *css);
        }
        if (!selfClosing) {
            stack.push_back({name, st});
        }
    }
    if (!sawElement) {
        spans.clear();
        spans.push_back({std::string(xhtml)});
    }
    // Абзац в начале (пустой первый <p>) не нужен; хвостовые переводы строк — тоже.
    while (!spans.empty() && !spans.back().text.empty() && spans.back().text.back() == '\n') {
        spans.back().text.pop_back();
        if (spans.back().text.empty()) {
            spans.pop_back();
        }
    }
    return spans;
}

std::string plainText(const std::vector<RichSpan>& spans) {
    std::string out;
    for (const RichSpan& s : spans) {
        out += s.text;
    }
    return out;
}

}  // namespace ot::annot
