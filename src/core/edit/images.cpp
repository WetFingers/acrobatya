#include "core/edit/images.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/rewriter.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

namespace ot::edit {

namespace edit_images {

// Число для потока: до 4 знаков после точки, без хвостовых нулей.
std::string num(double v) {
    if (std::fabs(v) < 5e-5) {
        return "0";
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s(buf);
    while (!s.empty() && s.back() == '0') {
        s.pop_back();
    }
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s == "-0" ? "0" : s;
}

std::string matrixText(const Matrix& m) {
    return num(m.a) + " " + num(m.b) + " " + num(m.c) + " " + num(m.d) + " " + num(m.e) + " " + num(m.f);
}

// Имя PDF: обычные символы как есть, прочие — #xx.
std::string pdfName(const std::string& name) {
    std::string out = "/";
    for (const unsigned char c : name) {
        const bool regular =
            c > 32 && c < 127 &&
            std::string_view("()<>[]{}/%#").find(static_cast<char>(c)) == std::string_view::npos;
        if (regular) {
            out += static_cast<char>(c);
        } else {
            char buf[4];
            std::snprintf(buf, sizeof buf, "#%02X", c);
            out += buf;
        }
    }
    return out;
}

RectF quadBounds(const Matrix& unitToPage, const RectF& unit) {
    RectF r = RectF::none();
    for (const PointF p : {PointF{unit.x0, unit.y0}, PointF{unit.x1, unit.y0}, PointF{unit.x0, unit.y1},
                           PointF{unit.x1, unit.y1}}) {
        const PointF q = mapPoint(p, unitToPage);
        r = rectUnion(r, RectF{q.x, q.y, q.x, q.y});
    }
    return r;
}

bool matrixFrom(const Operation& o, Matrix& m) {
    std::vector<float> v;
    for (const Operand& x : o.operands) {
        if (x.isNumber()) {
            v.push_back(static_cast<float>(x.number));
        }
    }
    if (v.size() < 6) {
        return false;
    }
    const std::size_t k = v.size() - 6;
    m = Matrix{v[k], v[k + 1], v[k + 2], v[k + 3], v[k + 4], v[k + 5]};
    return true;
}

bool isIdentity(const Matrix& m) {
    return std::fabs(m.a - 1) < 1e-4f && std::fabs(m.b) < 1e-4f && std::fabs(m.c) < 1e-4f &&
           std::fabs(m.d - 1) < 1e-4f && std::fabs(m.e) < 1e-4f && std::fabs(m.f) < 1e-4f;
}

// Группа места картинки в потоке: q cm [re W n] Do Q.
struct Group {
    int save = -1;
    int concat = -1;
    int re = -1;
    int w = -1;
    int n = -1;
    int restore = -1;
    Matrix before;  // CTM у q
};

struct Scan {
    std::vector<PageImage> images;
    std::map<int, Group> groups;  // операция Do → группа
    std::map<int, Matrix> beforeDo;
    Matrix finalCtm;
    int depth = 0;  // q без Q в конце страницы
};

Scan scan(const Document& doc, const ContentModel& model) {
    Scan out;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    std::vector<Matrix> stack;
    Matrix ctm;
    std::map<int, Matrix> saveCtm;
    const auto& ops = model.operations();
    const Matrix page = model.pageMatrix();
    struct Candidate {
        int op;
        std::string name;
        Matrix ctm;
    };
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const Operation& o = ops[i];
        if (model.streams()[static_cast<std::size_t>(o.stream)].form) {
            continue;
        }
        switch (o.kind) {
            case OpKind::Save:
                saveCtm[static_cast<int>(i)] = ctm;
                stack.push_back(ctm);
                break;
            case OpKind::Restore:
                if (!stack.empty()) {
                    ctm = stack.back();
                    stack.pop_back();
                }
                break;
            case OpKind::Concat: {
                Matrix m;
                if (matrixFrom(o, m)) {
                    ctm = concat(m, ctm);
                }
                break;
            }
            case OpKind::XObject:
                for (const Operand& x : o.operands) {
                    if (x.kind == Operand::Kind::Name) {
                        candidates.push_back({static_cast<int>(i), x.text, ctm});
                    }
                }
                break;
            default:
                break;
        }
    }
    out.finalCtm = ctm;
    out.depth = static_cast<int>(stack.size());

    pdf_obj* pageObj = nullptr;
    mu::call(ctx, [&] { pageObj = pdf_lookup_page_obj(ctx, pdf, model.page()); });
    for (const Candidate& c : candidates) {
        int objnum = 0;
        int width = 0;
        int height = 0;
        char filter[64] = {};
        int isImage = 0;
        const char* name = c.name.c_str();
        mu::call(ctx, [&] {
            pdf_obj* res = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources));
            pdf_obj* obj = pdf_dict_gets(ctx, pdf_dict_get(ctx, res, PDF_NAME(XObject)), name);
            if (obj != nullptr &&
                pdf_name_eq(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Subtype)), PDF_NAME(Image))) {
                isImage = 1;
                objnum = pdf_to_num(ctx, obj);
                width = pdf_dict_get_int(ctx, obj, PDF_NAME(Width));
                height = pdf_dict_get_int(ctx, obj, PDF_NAME(Height));
                pdf_obj* f = pdf_dict_get(ctx, obj, PDF_NAME(Filter));
                if (pdf_is_array(ctx, f)) {
                    f = pdf_array_get(ctx, f, 0);
                }
                if (pdf_is_name(ctx, f)) {
                    fz_strlcpy(filter, pdf_to_name(ctx, f), sizeof filter);
                }
            }
        });
        if (isImage == 0) {
            continue;
        }
        PageImage img;
        img.index = static_cast<int>(out.images.size());
        img.op = c.op;
        img.name = c.name;
        img.objnum = objnum;
        img.width = width;
        img.height = height;
        img.filter = filter;
        img.matrix = c.ctm;
        img.pageMatrix = concat(c.ctm, page);
        img.quad = QuadF{mapPoint(PointF{0, 1}, img.pageMatrix), mapPoint(PointF{1, 1}, img.pageMatrix),
                         mapPoint(PointF{0, 0}, img.pageMatrix), mapPoint(PointF{1, 0}, img.pageMatrix)};
        out.beforeDo[c.op] = c.ctm;
        // Группа: соседние операции того же потока.
        const ContentStream& st =
            model.streams()[static_cast<std::size_t>(ops[static_cast<std::size_t>(c.op)].stream)];
        const auto pos = std::find(st.ops.begin(), st.ops.end(), c.op);
        if (pos != st.ops.end()) {
            const auto k = static_cast<std::ptrdiff_t>(pos - st.ops.begin());
            auto at = [&](std::ptrdiff_t j) -> const Operation* {
                return j >= 0 && j < static_cast<std::ptrdiff_t>(st.ops.size())
                           ? &ops[static_cast<std::size_t>(st.ops[static_cast<std::size_t>(j)])]
                           : nullptr;
            };
            auto idx = [&](std::ptrdiff_t j) { return st.ops[static_cast<std::size_t>(j)]; };
            const Operation* after = at(k + 1);
            if (after != nullptr && after->kind == OpKind::Restore) {
                Group g;
                std::ptrdiff_t j = k - 1;
                const Operation* n = at(j);
                if (n != nullptr && n->name == "n" && at(j - 1) != nullptr &&
                    (at(j - 1)->name == "W" || at(j - 1)->name == "W*") && at(j - 2) != nullptr &&
                    at(j - 2)->name == "re") {
                    g.n = idx(j);
                    g.w = idx(j - 1);
                    g.re = idx(j - 2);
                    j -= 3;
                }
                const Operation* cm = at(j);
                const Operation* q = at(j - 1);
                if (cm != nullptr && cm->kind == OpKind::Concat && q != nullptr && q->kind == OpKind::Save) {
                    g.concat = idx(j);
                    g.save = idx(j - 1);
                    g.restore = idx(k + 1);
                    g.before = saveCtm[g.save];
                    img.group = true;
                    if (g.re >= 0) {
                        std::vector<float> v;
                        for (const Operand& x : ops[static_cast<std::size_t>(g.re)].operands) {
                            if (x.isNumber()) {
                                v.push_back(static_cast<float>(x.number));
                            }
                        }
                        if (v.size() >= 4) {
                            const std::size_t b = v.size() - 4;
                            const float x0 = std::min(v[b], v[b] + v[b + 2]);
                            const float y0 = std::min(v[b + 1], v[b + 1] + v[b + 3]);
                            img.clip = RectF{std::max(0.0f, x0), std::max(0.0f, y0),
                                             std::min(1.0f, x0 + std::fabs(v[b + 2])),
                                             std::min(1.0f, y0 + std::fabs(v[b + 3]))};
                        }
                    }
                    out.groups[c.op] = g;
                }
            }
        }
        img.bbox = quadBounds(img.pageMatrix, img.clip);
        out.images.push_back(std::move(img));
    }
    return out;
}

void checkLock(Document& doc) {
    if (editLock(doc) != EditLock::None) {
        throw std::runtime_error("document is signed or has /Perms: image editing is disabled");
    }
}

// Текст группы места картинки.
std::string groupText(const std::string& name, const Matrix& relative, const RectF& clip) {
    std::string s = " q " + matrixText(relative) + " cm";
    const bool clipped = clip.x0 > 0 || clip.y0 > 0 || clip.x1 < 1 || clip.y1 < 1;
    if (clipped) {
        s += " " + num(clip.x0) + " " + num(clip.y0) + " " + num(clip.x1 - clip.x0) + " " +
             num(clip.y1 - clip.y0) + " re W n";
    }
    s += " " + pdfName(name) + " Do Q";
    return s;
}

// Записать место картинки img: единичный квадрат → пространство пользователя unitToUser, обтравка clip; пусто
// name — убрать картинку.
void writePlace(ContentModel& model, const Scan& sc, const PageImage& img, const std::string& name,
                const Matrix& unitToUser, const RectF& clip) {
    const auto g = sc.groups.find(img.op);
    const Matrix before = g != sc.groups.end() ? g->second.before : sc.beforeDo.at(img.op);
    const std::string text =
        name.empty() ? std::string() : groupText(name, concat(unitToUser, invertMatrix(before)), clip);
    if (g != sc.groups.end()) {
        const Group& gr = g->second;
        model.replace(gr.save, text);
        for (const int op : {gr.concat, gr.re, gr.w, gr.n, img.op, gr.restore}) {
            if (op >= 0) {
                model.replace(op, std::string());
            }
        }
    } else {
        model.replace(img.op, text);
    }
}

// Новое изображение из байтов → ресурс страницы с новым именем OtImN; имя.
std::string addImageResource(fz_context* ctx, pdf_document* pdf, int page, std::string_view bytes) {
    mu::BufferHandle buf(ctx);
    mu::ImageHandle image(ctx);
    mu::PdfObjHandle ref(ctx);
    mu::PdfObjHandle copy(ctx);
    mu::PdfObjHandle dict(ctx);
    pdf_obj* pageObj = nullptr;
    pdf_obj* xobjs = nullptr;
    mu::call(ctx, [&] {
        buf.reset(fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(bytes.data()),
                                                 bytes.size()));
        image.reset(fz_new_image_from_buffer(ctx, buf));
        ref.reset(pdf_add_image(ctx, pdf, image));
        if (pdf->version < 14) {
            pdf->version = 14;  // /SMask (прозрачность PNG) — с PDF 1.4 (pdfcpu strict, Acrobat)
        }
        pageObj = pdf_lookup_page_obj(ctx, pdf, page);
        pdf_obj* res = pdf_dict_get(ctx, pageObj, PDF_NAME(Resources));
        if (res == nullptr) {
            // Ресурсы унаследованы от дерева страниц — своя копия у страницы.
            pdf_obj* inherited = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources));
            copy.reset(inherited != nullptr ? pdf_copy_dict(ctx, inherited) : pdf_new_dict(ctx, pdf, 2));
            pdf_dict_put(ctx, pageObj, PDF_NAME(Resources), copy);
            res = copy;
        }
        xobjs = pdf_dict_get(ctx, res, PDF_NAME(XObject));
        if (xobjs == nullptr) {
            dict.reset(pdf_new_dict(ctx, pdf, 2));
            pdf_dict_put(ctx, res, PDF_NAME(XObject), dict);
            xobjs = dict;
        }
    });
    std::string name;
    for (int i = 1;; ++i) {
        name = "OtIm" + std::to_string(i);
        int taken = 0;
        const char* n = name.c_str();
        mu::call(ctx, [&] { taken = pdf_dict_gets(ctx, xobjs, n) != nullptr; });
        if (taken == 0) {
            break;
        }
    }
    const char* n = name.c_str();
    mu::call(ctx, [&] { pdf_dict_puts(ctx, xobjs, n, ref); });
    return name;
}

// Поток в начало (prefix) и конец (suffix) /Contents страницы.
void addContent(fz_context* ctx, pdf_document* pdf, int page, const std::string& prefix,
                const std::string& suffix) {
    mu::BufferHandle head(ctx);
    mu::BufferHandle tail(ctx);
    mu::PdfObjHandle headRef(ctx);
    mu::PdfObjHandle tailRef(ctx);
    mu::PdfObjHandle array(ctx);
    const char* pre = prefix.c_str();
    const std::size_t preLen = prefix.size();
    const char* suf = suffix.c_str();
    const std::size_t sufLen = suffix.size();
    mu::call(ctx, [&] {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
        pdf_obj* contents = pdf_dict_get(ctx, pageObj, PDF_NAME(Contents));
        array.reset(pdf_new_array(ctx, pdf, 4));
        if (preLen > 0) {
            head.reset(
                fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(pre), preLen));
            headRef.reset(pdf_add_stream(ctx, pdf, head, nullptr, 0));
            pdf_array_push(ctx, array, headRef);
        }
        if (pdf_is_array(ctx, contents)) {
            for (int i = 0; i < pdf_array_len(ctx, contents); ++i) {
                pdf_array_push(ctx, array, pdf_array_get(ctx, contents, i));
            }
        } else if (contents != nullptr) {
            pdf_array_push(ctx, array, contents);
        }
        tail.reset(fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(suf), sufLen));
        tailRef.reset(pdf_add_stream(ctx, pdf, tail, nullptr, 0));
        pdf_array_push(ctx, array, tailRef);
        pdf_dict_put(ctx, pageObj, PDF_NAME(Contents), array);
    });
}

}  // namespace edit_images

using namespace edit_images;

std::vector<PageImage> pageImages(const Document& doc, const ContentModel& model) {
    return scan(doc, model).images;
}

std::vector<PageImage> pageImages(const Document& doc, int page) {
    return pageImages(doc, ContentModel::build(doc, page));
}

int imageAt(const std::vector<PageImage>& images, PointF p) {
    for (auto it = images.rbegin(); it != images.rend(); ++it) {
        if (rectContains(it->bbox, p)) {
            return it->index;
        }
    }
    return -1;
}

ImageOps::ImageOps(Document& doc) : doc_(doc) {}

std::vector<PageImage> ImageOps::list(int page) const { return pageImages(doc_, page); }

void ImageOps::setPageMatrix(int page, int index, const Matrix& unitToPage, const std::string& operation) {
    checkLock(doc_);
    ContentModel model = ContentModel::build(doc_, page);
    const Scan sc = scan(doc_, model);
    const PageImage& img = sc.images.at(static_cast<std::size_t>(index));
    const Matrix toUser = concat(unitToPage, invertMatrix(model.pageMatrix()));
    Journal::Operation op(doc_, operation);
    writePlace(model, sc, img, img.name, toUser, img.clip);
    model.apply(doc_, operation);
    op.invalidate(page);
    op.markHeavy();
    op.commit();
}

void ImageOps::move(int page, int index, PointF delta, const std::string& operation) {
    const PageImage img = list(page).at(static_cast<std::size_t>(index));
    setPageMatrix(page, index, concat(img.pageMatrix, Matrix::translate(delta.x, delta.y)), operation);
}

void ImageOps::setRect(int page, int index, const RectF& rect, const std::string& operation) {
    const PageImage img = list(page).at(static_cast<std::size_t>(index));
    const RectF& b = img.bbox;
    if (b.width() <= 0 || b.height() <= 0 || rect.width() <= 0 || rect.height() <= 0) {
        throw std::invalid_argument("image rectangle is empty");
    }
    const Matrix s = concat(concat(Matrix::translate(-b.x0, -b.y0),
                                   Matrix::scale(rect.width() / b.width(), rect.height() / b.height())),
                            Matrix::translate(rect.x0, rect.y0));
    setPageMatrix(page, index, concat(img.pageMatrix, s), operation);
}

void ImageOps::rotate(int page, int index, float degrees, const std::string& operation) {
    const PageImage img = list(page).at(static_cast<std::size_t>(index));
    const float cx = (img.bbox.x0 + img.bbox.x1) / 2;
    const float cy = (img.bbox.y0 + img.bbox.y1) / 2;
    // Координаты страницы — y вниз: положительный угол — по часовой стрелке на экране.
    Matrix r;
    const double rad = degrees * 3.14159265358979323846 / 180.0;
    if (std::fmod(std::fabs(degrees), 90.0f) == 0) {
        r = Matrix::rotate(degrees);
    } else {
        const auto c = static_cast<float>(std::cos(rad));
        const auto s = static_cast<float>(std::sin(rad));
        r = Matrix{c, s, -s, c, 0, 0};
    }
    const Matrix about = concat(concat(Matrix::translate(-cx, -cy), r), Matrix::translate(cx, cy));
    setPageMatrix(page, index, concat(img.pageMatrix, about), operation);
}

void ImageOps::flip(int page, int index, bool horizontal, const std::string& operation) {
    const PageImage img = list(page).at(static_cast<std::size_t>(index));
    const float cx = (img.bbox.x0 + img.bbox.x1) / 2;
    const float cy = (img.bbox.y0 + img.bbox.y1) / 2;
    const Matrix about = concat(concat(Matrix::translate(-cx, -cy),
                                       Matrix::scale(horizontal ? -1.0f : 1.0f, horizontal ? 1.0f : -1.0f)),
                                Matrix::translate(cx, cy));
    setPageMatrix(page, index, concat(img.pageMatrix, about), operation);
}

void ImageOps::crop(int page, int index, const RectF& rect, const std::string& operation) {
    checkLock(doc_);
    ContentModel model = ContentModel::build(doc_, page);
    const Scan sc = scan(doc_, model);
    const PageImage& img = sc.images.at(static_cast<std::size_t>(index));
    // Рамка обрезки → единицы картинки (обратная матрица единичного квадрата), пересечение с квадратом.
    const Matrix inv = invertMatrix(img.pageMatrix);
    RectF u = RectF::none();
    for (const PointF p : {PointF{rect.x0, rect.y0}, PointF{rect.x1, rect.y0}, PointF{rect.x0, rect.y1},
                           PointF{rect.x1, rect.y1}}) {
        const PointF q = mapPoint(p, inv);
        u = rectUnion(u, RectF{q.x, q.y, q.x, q.y});
    }
    RectF clip{std::max(0.0f, u.x0), std::max(0.0f, u.y0), std::min(1.0f, u.x1), std::min(1.0f, u.y1)};
    if (clip.empty()) {
        throw std::invalid_argument("crop rectangle misses the image");
    }
    const float eps = 1e-3f;
    if (clip.x0 < eps && clip.y0 < eps && clip.x1 > 1 - eps && clip.y1 > 1 - eps) {
        clip = RectF{0, 0, 1, 1};  // вся картинка — обрезки нет
    }
    Journal::Operation op(doc_, operation);
    writePlace(model, sc, img, img.name, img.matrix, clip);
    model.apply(doc_, operation);
    op.invalidate(page);
    op.markHeavy();
    op.commit();
}

void ImageOps::replace(int page, int index, std::string_view bytes, const std::string& operation) {
    checkLock(doc_);
    ContentModel model = ContentModel::build(doc_, page);
    const Scan sc = scan(doc_, model);
    const PageImage& img = sc.images.at(static_cast<std::size_t>(index));
    fz_context* ctx = doc_.context().get();
    Journal::Operation op(doc_, operation);
    const std::string name = addImageResource(ctx, doc_.pdf(), page, bytes);
    writePlace(model, sc, img, name, img.matrix, img.clip);
    model.apply(doc_, operation);
    op.invalidate(page);
    op.markHeavy();
    op.commit();
}

int ImageOps::add(int page, std::string_view bytes, const RectF& rect, const std::string& operation) {
    checkLock(doc_);
    if (rect.width() <= 0 || rect.height() <= 0) {
        throw std::invalid_argument("image rectangle is empty");
    }
    const ContentModel model = ContentModel::build(doc_, page);
    const Scan sc = scan(doc_, model);
    const Matrix toUser = invertMatrix(model.pageMatrix());
    // Единичный квадрат: (0,0) — левый нижний угол на экране, (0,1) — левый верхний.
    const PointF a = mapPoint(PointF{rect.x0, rect.y1}, toUser);
    const PointF b = mapPoint(PointF{rect.x1, rect.y1}, toUser);
    const PointF c = mapPoint(PointF{rect.x0, rect.y0}, toUser);
    const Matrix unit{b.x - a.x, b.y - a.y, c.x - a.x, c.y - a.y, a.x, a.y};
    fz_context* ctx = doc_.context().get();
    Journal::Operation op(doc_, operation);
    const std::string name = addImageResource(ctx, doc_.pdf(), page, bytes);
    const bool clean = isIdentity(sc.finalCtm) && sc.depth == 0;
    std::string suffix;
    if (!clean) {
        for (int i = 0; i <= sc.depth; ++i) {
            suffix += "Q ";
        }
    }
    suffix += groupText(name, unit, RectF{0, 0, 1, 1}).substr(1) + "\n";
    addContent(ctx, doc_.pdf(), page, clean ? std::string() : std::string("q\n"), suffix);
    op.invalidate(page);
    op.markHeavy();
    op.commit();
    return static_cast<int>(sc.images.size());
}

void ImageOps::remove(int page, int index, const std::string& operation) {
    checkLock(doc_);
    ContentModel model = ContentModel::build(doc_, page);
    const Scan sc = scan(doc_, model);
    const PageImage& img = sc.images.at(static_cast<std::size_t>(index));
    Journal::Operation op(doc_, operation);
    writePlace(model, sc, img, std::string(), img.matrix, img.clip);
    model.apply(doc_, operation);
    op.invalidate(page);
    op.markHeavy();
    op.commit();
}

ImageData ImageOps::exportImage(int page, int index) const {
    const PageImage img = list(page).at(static_cast<std::size_t>(index));
    fz_context* ctx = doc_.context().get();
    pdf_document* pdf = doc_.pdf();
    mu::ImageHandle image(ctx);
    mu::PixmapHandle pix(ctx);
    mu::PixmapHandle rgb(ctx);
    mu::BufferHandle png(ctx);
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    int jpeg = 0;
    const char* name = img.name.c_str();
    mu::call(ctx, [&] {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
        pdf_obj* res = pdf_dict_get_inheritable(ctx, pageObj, PDF_NAME(Resources));
        pdf_obj* obj = pdf_dict_gets(ctx, pdf_dict_get(ctx, res, PDF_NAME(XObject)), name);
        image.reset(pdf_load_image(ctx, pdf, obj));
        fz_compressed_buffer* cb = fz_compressed_image_buffer(ctx, image);
        if (cb != nullptr && cb->params.type == FZ_IMAGE_JPEG && cb->buffer != nullptr) {
            jpeg = 1;
            size = fz_buffer_storage(ctx, cb->buffer, const_cast<unsigned char**>(&data));
        } else {
            pix.reset(fz_get_pixmap_from_image(ctx, image, nullptr, nullptr, nullptr, nullptr));
            fz_pixmap* src = pix;
            if (fz_pixmap_colorspace(ctx, src) != nullptr &&
                fz_colorspace_type(ctx, fz_pixmap_colorspace(ctx, src)) != FZ_COLORSPACE_GRAY &&
                fz_colorspace_type(ctx, fz_pixmap_colorspace(ctx, src)) != FZ_COLORSPACE_RGB) {
                rgb.reset(fz_convert_pixmap(ctx, src, fz_device_rgb(ctx), nullptr, nullptr,
                                            fz_default_color_params, 1));
                src = rgb;
            }
            png.reset(fz_new_buffer_from_pixmap_as_png(ctx, src, fz_default_color_params));
            size = fz_buffer_storage(ctx, png, const_cast<unsigned char**>(&data));
        }
    });
    ImageData out;
    out.bytes.assign(reinterpret_cast<const char*>(data), size);
    out.format = jpeg != 0 ? "jpg" : "png";
    out.width = img.width;
    out.height = img.height;
    return out;
}

ImageData probeImage(std::string_view bytes) {
    ImageData out;
    fz_context* ctx = MuContext::forThisThread().get();
    mu::BufferHandle buf(ctx);
    mu::ImageHandle image(ctx);
    int w = 0;
    int h = 0;
    int type = 0;
    unsigned char head[8] = {};
    std::copy_n(bytes.data(), std::min<std::size_t>(8, bytes.size()), head);
    try {
        mu::call(ctx, [&] {
            buf.reset(fz_new_buffer_from_copied_data(
                ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
            image.reset(fz_new_image_from_buffer(ctx, buf));
            w = image.get()->w;
            h = image.get()->h;
            type = fz_recognize_image_format(ctx, head);
        });
    } catch (const std::exception&) {
        return out;
    }
    out.width = w;
    out.height = h;
    out.format = type == FZ_IMAGE_JPEG ? "jpg" : "png";
    return out;
}

}  // namespace ot::edit
