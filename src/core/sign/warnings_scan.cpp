#include "core/sign/warnings_scan.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <set>
#include <string_view>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace {

constexpr int kMaxDepth = 32;
constexpr int kMaxOutlines = 20000;

// Текст строки или имени файла: строка, /UF или /F спецификации файла (и /Win /F у /Launch).
const char* fileName(fz_context* ctx, pdf_obj* f) {
    if (pdf_is_string(ctx, f)) {
        return pdf_to_text_string(ctx, f);
    }
    if (pdf_is_dict(ctx, f)) {
        pdf_obj* uf = pdf_dict_get(ctx, f, PDF_NAME(UF));
        if (pdf_is_string(ctx, uf)) {
            return pdf_to_text_string(ctx, uf);
        }
        pdf_obj* name = pdf_dict_get(ctx, f, PDF_NAME(F));
        if (pdf_is_string(ctx, name)) {
            return pdf_to_text_string(ctx, name);
        }
    }
    return nullptr;
}

class Scanner {
public:
    Scanner(Document& doc, WarningScan& out) : ctx_(doc.context().get()), pdf_(doc.pdf()), out_(out) {}

    void run(int pages) {
        catalog();
        for (int i = 0; i < pages; ++i) {
            page(i);
        }
        pdf_obj* fields = nullptr;
        int n = 0;
        mu::call(ctx_, [&] {
            fields = pdf_dict_getp(ctx_, pdf_trailer(ctx_, pdf_), "Root/AcroForm/Fields");
            n = pdf_array_len(ctx_, fields);
        });
        for (int i = 0; i < n; ++i) {
            pdf_obj* f = nullptr;
            mu::call(ctx_, [&] { f = pdf_array_get(ctx_, fields, i); });
            field(f, 0);
        }
        pdf_obj* first = nullptr;
        mu::call(ctx_, [&] { first = pdf_dict_getp(ctx_, pdf_trailer(ctx_, pdf_), "Root/Outlines/First"); });
        outlines(first, 0);
    }

private:
    void add(WarningKind kind, int page, int object, std::string where, std::string detail) {
        DocumentWarning w;
        w.kind = kind;
        w.level = warningLevel(kind);
        w.page = page;
        w.object = object;
        w.where = std::move(where);
        w.detail = std::move(detail);
        out_.warnings.push_back(std::move(w));
    }

    // Действие и его цепочка /Next.
    void action(pdf_obj* a, int page, int owner, const std::string& where, int depth) {
        if (a == nullptr || depth > kMaxDepth) {
            return;
        }
        int isDict = 0;
        int isArray = 0;
        int num = 0;
        mu::call(ctx_, [&] {
            isDict = pdf_is_dict(ctx_, a);
            isArray = pdf_is_array(ctx_, a);
            num = pdf_is_indirect(ctx_, a) ? pdf_to_num(ctx_, a) : 0;
        });
        if (num != 0 && !seenActions_.insert(num).second) {
            return;
        }
        if (isArray != 0) {
            int n = 0;
            mu::call(ctx_, [&] { n = pdf_array_len(ctx_, a); });
            for (int i = 0; i < n; ++i) {
                pdf_obj* item = nullptr;
                mu::call(ctx_, [&] { item = pdf_array_get(ctx_, a, i); });
                action(item, page, owner, where, depth + 1);
            }
            return;
        }
        if (isDict == 0) {
            return;
        }
        const char* s = nullptr;
        int js = 0;
        const char* file = nullptr;
        const char* uri = nullptr;
        pdf_obj* next = nullptr;
        mu::call(ctx_, [&] {
            s = pdf_to_name(ctx_, pdf_dict_get(ctx_, a, PDF_NAME(S)));
            js = pdf_dict_get(ctx_, a, PDF_NAME(JS)) != nullptr ? 1 : 0;
            file = fileName(ctx_, pdf_dict_get(ctx_, a, PDF_NAME(F)));
            if (file == nullptr) {
                file = fileName(ctx_, pdf_dict_getp(ctx_, a, "Win/F"));
            }
            uri = pdf_to_text_string(ctx_, pdf_dict_get(ctx_, a, PDF_NAME(URI)));
            next = pdf_dict_get(ctx_, a, PDF_NAME(Next));
        });
        const std::string_view type = s != nullptr ? s : "";
        const std::string fileText = file != nullptr ? file : "";
        if (type == "JavaScript" || js != 0) {
            add(WarningKind::JavaScript, page, owner, where, {});
        } else if (type == "Launch") {
            add(WarningKind::Launch, page, owner, where, fileText);
        } else if (type == "URI") {
            add(WarningKind::ExternalLink, page, owner, where, uri != nullptr ? uri : "");
        } else if (type == "GoToR" || type == "GoToE" || type == "ImportData" || type == "SubmitForm") {
            add(WarningKind::ExternalFile, page, owner, where, fileText);
        } else if (type == "Movie" || type == "Sound" || type == "Rendition" || type == "RichMediaExecute" ||
                   type == "GoTo3DView") {
            add(WarningKind::Multimedia, page, owner, where, std::string(type));
        }
        action(next, page, owner, where + " /Next", depth + 1);
    }

    // /A и /AA владельца (один раз на косвенный объект).
    void owner(pdf_obj* o, int page, const std::string& what) {
        int num = 0;
        pdf_obj* a = nullptr;
        pdf_obj* aa = nullptr;
        int keys = 0;
        mu::call(ctx_, [&] {
            num = pdf_is_indirect(ctx_, o) ? pdf_to_num(ctx_, o) : 0;
            a = pdf_dict_get(ctx_, o, PDF_NAME(A));
            aa = pdf_dict_get(ctx_, o, PDF_NAME(AA));
            keys = pdf_dict_len(ctx_, aa);
        });
        if (num != 0 && !seenOwners_.insert(num).second) {
            return;
        }
        action(a, page, num, what + " /A", 0);
        for (int i = 0; i < keys; ++i) {
            const char* key = nullptr;
            pdf_obj* val = nullptr;
            mu::call(ctx_, [&] {
                key = pdf_to_name(ctx_, pdf_dict_get_key(ctx_, aa, i));
                val = pdf_dict_get_val(ctx_, aa, i);
            });
            action(val, page, num, what + " /AA /" + (key != nullptr ? key : ""), 0);
        }
    }

    // Дерево имён: пары ключ — значение.
    template <class Fn>
    void nameTree(pdf_obj* node, int depth, Fn&& fn) {
        if (node == nullptr || depth > kMaxDepth) {
            return;
        }
        pdf_obj* names = nullptr;
        pdf_obj* kids = nullptr;
        int nn = 0;
        int nk = 0;
        mu::call(ctx_, [&] {
            names = pdf_dict_get(ctx_, node, PDF_NAME(Names));
            kids = pdf_dict_get(ctx_, node, PDF_NAME(Kids));
            nn = pdf_array_len(ctx_, names);
            nk = pdf_array_len(ctx_, kids);
        });
        for (int i = 0; i + 1 < nn; i += 2) {
            const char* key = nullptr;
            pdf_obj* val = nullptr;
            mu::call(ctx_, [&] {
                key = pdf_to_text_string(ctx_, pdf_array_get(ctx_, names, i));
                val = pdf_array_get(ctx_, names, i + 1);
            });
            fn(std::string(key != nullptr ? key : ""), val);
        }
        for (int i = 0; i < nk; ++i) {
            pdf_obj* kid = nullptr;
            mu::call(ctx_, [&] { kid = pdf_array_get(ctx_, kids, i); });
            nameTree(kid, depth + 1, fn);
        }
    }

    void catalog() {
        pdf_obj* root = nullptr;
        pdf_obj* jsTree = nullptr;
        pdf_obj* files = nullptr;
        pdf_obj* open = nullptr;
        int xfa = 0;
        int needAppearances = 0;
        mu::call(ctx_, [&] {
            root = pdf_dict_get(ctx_, pdf_trailer(ctx_, pdf_), PDF_NAME(Root));
            jsTree = pdf_dict_getp(ctx_, root, "Names/JavaScript");
            files = pdf_dict_getp(ctx_, root, "Names/EmbeddedFiles");
            open = pdf_dict_gets(ctx_, root, "OpenAction");
            pdf_obj* form = pdf_dict_get(ctx_, root, PDF_NAME(AcroForm));
            xfa = pdf_dict_get(ctx_, form, PDF_NAME(XFA)) != nullptr ? 1 : 0;
            needAppearances = pdf_to_bool(ctx_, pdf_dict_get(ctx_, form, PDF_NAME(NeedAppearances)));
        });
        nameTree(jsTree, 0, [&](const std::string& name, pdf_obj* a) {
            const std::size_t before = out_.warnings.size();
            action(a, -1, 0, "/Names /JavaScript", 0);
            for (std::size_t i = before; i < out_.warnings.size(); ++i) {
                if (out_.warnings[i].detail.empty()) {
                    out_.warnings[i].detail = name;
                }
            }
        });
        nameTree(files, 0, [&](const std::string& name, pdf_obj* spec) {
            const char* file = nullptr;
            mu::call(ctx_, [&] { file = fileName(ctx_, spec); });
            add(WarningKind::EmbeddedFile, -1, 0, "/Names /EmbeddedFiles", file != nullptr ? file : name);
        });
        int openIsAction = 0;
        mu::call(ctx_, [&] { openIsAction = pdf_is_dict(ctx_, open); });
        if (openIsAction != 0) {
            action(open, -1, 0, "/OpenAction", 0);
        }
        owner(root, -1, "catalog");
        if (xfa != 0) {
            add(WarningKind::Xfa, -1, 0, "/AcroForm /XFA", {});
        }
        if (needAppearances != 0) {
            add(WarningKind::NeedAppearances, -1, 0, "/AcroForm /NeedAppearances", {});
        }
    }

    void page(int index) {
        pdf_obj* obj = nullptr;
        pdf_obj* annots = nullptr;
        int n = 0;
        mu::call(ctx_, [&] {
            obj = pdf_lookup_page_obj(ctx_, pdf_, index);
            annots = pdf_dict_get(ctx_, obj, PDF_NAME(Annots));
            n = pdf_array_len(ctx_, annots);
        });
        owner(obj, index, "page");
        for (int i = 0; i < n; ++i) {
            pdf_obj* annot = nullptr;
            const char* subtype = nullptr;
            const char* file = nullptr;
            int num = 0;
            mu::call(ctx_, [&] {
                annot = pdf_array_get(ctx_, annots, i);
                subtype = pdf_to_name(ctx_, pdf_dict_get(ctx_, annot, PDF_NAME(Subtype)));
                file = fileName(ctx_, pdf_dict_get(ctx_, annot, PDF_NAME(FS)));
                num = pdf_to_num(ctx_, annot);
            });
            const std::string_view st = subtype != nullptr ? subtype : "";
            if (st == "RichMedia" || st == "Movie" || st == "Sound" || st == "Screen" || st == "3D") {
                add(WarningKind::Multimedia, index, num, "annotation /" + std::string(st), std::string(st));
            } else if (st == "FileAttachment") {
                add(WarningKind::EmbeddedFile, index, num, "annotation /FileAttachment",
                    file != nullptr ? file : "");
            }
            owner(annot, index, st == "Widget" ? "field" : "annotation");
        }
    }

    void field(pdf_obj* f, int depth) {
        if (f == nullptr || depth > kMaxDepth) {
            return;
        }
        pdf_obj* kids = nullptr;
        int n = 0;
        mu::call(ctx_, [&] {
            kids = pdf_dict_get(ctx_, f, PDF_NAME(Kids));
            n = pdf_array_len(ctx_, kids);
        });
        owner(f, -1, "field");
        for (int i = 0; i < n; ++i) {
            pdf_obj* kid = nullptr;
            mu::call(ctx_, [&] { kid = pdf_array_get(ctx_, kids, i); });
            field(kid, depth + 1);
        }
    }

    void outlines(pdf_obj* item, int depth) {
        while (item != nullptr && depth <= kMaxDepth && outlineCount_ < kMaxOutlines) {
            int num = 0;
            pdf_obj* first = nullptr;
            pdf_obj* next = nullptr;
            mu::call(ctx_, [&] {
                num = pdf_to_num(ctx_, item);
                first = pdf_dict_get(ctx_, item, PDF_NAME(First));
                next = pdf_dict_get(ctx_, item, PDF_NAME(Next));
            });
            if (num != 0 && !seenOutlines_.insert(num).second) {
                return;
            }
            ++outlineCount_;
            owner(item, -1, "outline");
            outlines(first, depth + 1);
            item = next;
        }
    }

    fz_context* ctx_;
    pdf_document* pdf_;
    WarningScan& out_;
    std::set<int> seenOwners_;
    std::set<int> seenActions_;
    std::set<int> seenOutlines_;
    int outlineCount_ = 0;
};

}  // namespace

const char* warningKindName(WarningKind kind) {
    switch (kind) {
        case WarningKind::JavaScript:
            return "JavaScript";
        case WarningKind::Launch:
            return "Launch";
        case WarningKind::ExternalLink:
            return "ExternalLink";
        case WarningKind::ExternalFile:
            return "ExternalFile";
        case WarningKind::Multimedia:
            return "Multimedia";
        case WarningKind::EmbeddedFile:
            return "EmbeddedFile";
        case WarningKind::Xfa:
            return "Xfa";
        case WarningKind::NeedAppearances:
            return "NeedAppearances";
    }
    return "?";
}

const char* warningLevelName(WarningLevel level) {
    switch (level) {
        case WarningLevel::Info:
            return "Info";
        case WarningLevel::Warning:
            return "Warning";
        case WarningLevel::Danger:
            return "Danger";
    }
    return "?";
}

WarningLevel warningLevel(WarningKind kind) {
    switch (kind) {
        case WarningKind::JavaScript:
        case WarningKind::Launch:
            return WarningLevel::Danger;
        case WarningKind::ExternalLink:
            return WarningLevel::Info;
        case WarningKind::ExternalFile:
        case WarningKind::Multimedia:
        case WarningKind::EmbeddedFile:
        case WarningKind::Xfa:
        case WarningKind::NeedAppearances:
            return WarningLevel::Warning;
    }
    return WarningLevel::Warning;
}

int WarningScan::count(WarningKind kind) const {
    return static_cast<int>(std::count_if(warnings.begin(), warnings.end(),
                                          [kind](const DocumentWarning& w) { return w.kind == kind; }));
}

WarningLevel WarningScan::worst() const {
    WarningLevel level = WarningLevel::Info;
    for (const DocumentWarning& w : warnings) {
        level = std::max(level, w.level);
    }
    return level;
}

WarningScan scanWarnings(Document& doc) {
    WarningScan scan;
    if (doc.pdf() == nullptr) {
        return scan;
    }
    Scanner(doc, scan).run(doc.pageCount());
    spdlog::debug("warnings: {} found", scan.warnings.size());
    return scan;
}

}  // namespace ot::sign
