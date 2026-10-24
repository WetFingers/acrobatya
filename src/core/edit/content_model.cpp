#include "core/edit/content_model.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/utf8.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::edit {

namespace {

constexpr int kMaxSyntaxErrors = 100;  // как MAX_SYNTAX_ERRORS интерпретатора MuPDF
constexpr int kMaxFormDepth = 12;
constexpr int kMaxForms = 2000;  // показов форм на страницу: дальше формы не разворачиваются

using ProcessorHandle = mu::Handle<pdf_processor, pdf_drop_processor>;
using DefaultColorspacesHandle = mu::Handle<fz_default_colorspaces, fz_drop_default_colorspaces>;

// Как fz_pre_translate: те же операции с плавающей точкой, что у MuPDF.
Matrix preTranslate(Matrix m, float tx, float ty) noexcept {
    m.e += tx * m.a + ty * m.c;
    m.f += tx * m.b + ty * m.d;
    return m;
}

PointF transformPoint(PointF p, const Matrix& m) noexcept {
    return {p.x * m.a + p.y * m.c + m.e, p.x * m.b + p.y * m.d + m.f};
}

PointF transformVector(PointF v, const Matrix& m) noexcept {
    return {v.x * m.a + v.y * m.c, v.x * m.b + v.y * m.d};
}

PointF add(PointF a, PointF b) noexcept { return {a.x + b.x, a.y + b.y}; }

Matrix fromFz(const fz_matrix& m) noexcept { return {m.a, m.b, m.c, m.d, m.e, m.f}; }

RectF unite(const RectF& r, const RectF& q) noexcept {
    if (!r.valid()) {
        return q;
    }
    return {std::fmin(r.x0, q.x0), std::fmin(r.y0, q.y0), std::fmax(r.x1, q.x1), std::fmax(r.y1, q.y1)};
}

std::uint32_t argbFromComponents(const std::vector<float>& c) noexcept {
    float r = 0;
    float g = 0;
    float b = 0;
    if (c.size() == 1) {
        r = g = b = c[0];
    } else if (c.size() == 3) {
        r = c[0];
        g = c[1];
        b = c[2];
    } else if (c.size() >= 4) {
        r = (1 - c[0]) * (1 - c[3]);
        g = (1 - c[1]) * (1 - c[3]);
        b = (1 - c[2]) * (1 - c[3]);
    }
    auto byte = [](float v) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    return 0xFF000000u | (byte(r) << 16) | (byte(g) << 8) | byte(b);
}

bool isCombiningMark(char32_t c) noexcept {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) || (c >= 0x0591 && c <= 0x05BD) ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || (c >= 0x1AB0 && c <= 0x1AFF) ||
           (c >= 0x1DC0 && c <= 0x1DFF) || (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE20 && c <= 0xFE2F);
}

// pdf_obj (прямой, уже разобранный pdf_parse_array/pdf_parse_dict) → Operand. Ссылки не разрешаются.
Operand toOperand(fz_context* ctx, pdf_obj* obj, int depth = 0) {
    Operand o;
    if (obj == nullptr || depth > 32) {
        return o;
    }
    if (pdf_is_indirect(ctx, obj)) {
        o.kind = Operand::Kind::Keyword;
        o.text = "R";
        o.number = pdf_to_num(ctx, obj);
    } else if (pdf_is_number(ctx, obj)) {
        o.kind = Operand::Kind::Number;
        o.number = pdf_is_int(ctx, obj) ? static_cast<double>(pdf_to_int64(ctx, obj)) : pdf_to_real(ctx, obj);
    } else if (pdf_is_name(ctx, obj)) {
        o.kind = Operand::Kind::Name;
        o.text = pdf_to_name(ctx, obj);
    } else if (pdf_is_string(ctx, obj)) {
        o.kind = Operand::Kind::String;
        o.text.assign(pdf_to_str_buf(ctx, obj), pdf_to_str_len(ctx, obj));
    } else if (pdf_is_bool(ctx, obj)) {
        o.kind = Operand::Kind::Bool;
        o.number = pdf_to_bool(ctx, obj);
    } else if (pdf_is_array(ctx, obj)) {
        o.kind = Operand::Kind::Array;
        const int n = pdf_array_len(ctx, obj);
        for (int i = 0; i < n; ++i) {
            o.items.push_back(toOperand(ctx, pdf_array_get(ctx, obj, i), depth + 1));
        }
    } else if (pdf_is_dict(ctx, obj)) {
        o.kind = Operand::Kind::Dict;
        const int n = pdf_dict_len(ctx, obj);
        for (int i = 0; i < n; ++i) {
            o.items.push_back(toOperand(ctx, pdf_dict_get_key(ctx, obj, i), depth + 1));
            o.items.push_back(toOperand(ctx, pdf_dict_get_val(ctx, obj, i), depth + 1));
        }
    }
    return o;
}

// Ключевое слово → вид операции.
OpKind kindOf(std::string_view k) noexcept {
    static const std::unordered_map<std::string_view, OpKind> kKinds = {
        {"q", OpKind::Save},          {"Q", OpKind::Restore},       {"cm", OpKind::Concat},
        {"gs", OpKind::ExtGState},    {"BT", OpKind::BeginText},    {"ET", OpKind::EndText},
        {"Tf", OpKind::Font},         {"Td", OpKind::Move},         {"TD", OpKind::MoveLeading},
        {"Tm", OpKind::TextMatrix},   {"T*", OpKind::NextLine},     {"TL", OpKind::Leading},
        {"Tc", OpKind::CharSpace},    {"Tw", OpKind::WordSpace},    {"Tz", OpKind::Scale},
        {"Ts", OpKind::Rise},         {"Tr", OpKind::Render},       {"Tj", OpKind::ShowText},
        {"TJ", OpKind::ShowArray},    {"'", OpKind::NextLineShow},  {"\"", OpKind::NextLineShowSpaced},
        {"Do", OpKind::XObject},      {"BX", OpKind::BeginCompat},  {"EX", OpKind::EndCompat},
        {"BMC", OpKind::BeginMarked}, {"BDC", OpKind::BeginMarked}, {"EMC", OpKind::EndMarked},
        {"MP", OpKind::MarkPoint},    {"DP", OpKind::MarkPoint},    {"g", OpKind::FillColor},
        {"rg", OpKind::FillColor},    {"k", OpKind::FillColor},     {"cs", OpKind::FillColor},
        {"sc", OpKind::FillColor},    {"scn", OpKind::FillColor},   {"G", OpKind::StrokeColor},
        {"RG", OpKind::StrokeColor},  {"K", OpKind::StrokeColor},   {"CS", OpKind::StrokeColor},
        {"SC", OpKind::StrokeColor},  {"SCN", OpKind::StrokeColor},
    };
    const auto it = kKinds.find(k);
    return it != kKinds.end() ? it->second : OpKind::Unknown;
}

// Пропустить пробелы и комментарии PDF с позиции pos.
std::size_t skipSpace(std::string_view s, std::size_t pos) noexcept {
    while (pos < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[pos]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == 0) {
            ++pos;
        } else if (c == '%') {
            while (pos < s.size() && s[pos] != '\r' && s[pos] != '\n') {
                ++pos;
            }
        } else {
            break;
        }
    }
    return pos;
}

struct ColorState {
    int comps = 1;         // компонент текущего пространства (Gray — 1)
    bool pattern = false;  // пространство /Pattern
    std::uint32_t argb = 0xFF000000;
};

struct GState {
    Matrix ctm;      // с преобразованием страницы (как gstate->ctm MuPDF)
    Matrix userCtm;  // пространство пользователя страницы
    float charSpace = 0;
    float wordSpace = 0;
    float scale = 1;
    float leading = 0;
    float size = 0;
    float rise = 0;
    int render = 0;
    int font = -1;
    ColorState fill;
    ColorState stroke;
};

// Лексер одного декодированного потока: pdf_lex поверх fz_open_memory, позиция — fz_tell.
class Lexer {
public:
    Lexer(fz_context* ctx, const std::string& bytes) : ctx_(ctx), stm_(ctx) {
        mu::call(ctx, [&] {
            stm_.reset(
                fz_open_memory(ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
            pdf_lexbuf_init(ctx, &buf_, PDF_LEXBUF_SMALL);
        });
        ready_ = true;
    }
    ~Lexer() {
        if (ready_) {
            pdf_lexbuf_fin(ctx_, &buf_);
        }
    }
    Lexer(const Lexer&) = delete;
    Lexer& operator=(const Lexer&) = delete;

    // Следующий токен; ошибка MuPDF (кроме отмены и нехватки памяти) — PDF_TOK_ERROR.
    pdf_token next() {
        pdf_token tok = PDF_TOK_ERROR;
        try {
            mu::call(ctx_, [&] { tok = pdf_lex(ctx_, stm_, &buf_); });
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System) {
                throw;
            }
            tok = PDF_TOK_ERROR;
        }
        return tok;
    }
    std::size_t tell() const { return static_cast<std::size_t>(fz_tell(ctx_, stm_)); }
    fz_stream* stream() const noexcept { return stm_; }
    pdf_lexbuf* buf() noexcept { return &buf_; }
    double number(pdf_token tok) const noexcept {
        return tok == PDF_TOK_INT ? static_cast<double>(buf_.i) : static_cast<double>(buf_.f);
    }
    std::string text() const { return std::string(buf_.scratch, buf_.len); }
    std::string_view keyword() const noexcept { return buf_.scratch; }

private:
    fz_context* ctx_;
    mu::StreamHandle stm_;
    pdf_lexbuf buf_{};
    bool ready_ = false;
};

}  // namespace

const Operand* Operand::get(std::string_view key) const noexcept {
    if (kind != Kind::Dict) {
        return nullptr;
    }
    for (std::size_t i = 0; i + 1 < items.size(); i += 2) {
        if (items[i].kind == Kind::Name && items[i].text == key) {
            return &items[i + 1];
        }
    }
    return nullptr;
}

bool isShowOp(OpKind kind) noexcept {
    return kind == OpKind::ShowText || kind == OpKind::ShowArray || kind == OpKind::NextLineShow ||
           kind == OpKind::NextLineShowSpaced;
}

std::string_view ContentModel::raw(int op) const {
    const Operation& o = ops_.at(static_cast<std::size_t>(op));
    return std::string_view(streams_.at(static_cast<std::size_t>(o.stream)).bytes).substr(o.offset, o.length);
}

std::string ContentModel::spanText(int span) const {
    const TextSpan& s = spans_.at(static_cast<std::size_t>(span));
    std::string out;
    for (int i = s.firstGlyph; i < s.firstGlyph + s.glyphCount; ++i) {
        for (char32_t c : glyphs_[static_cast<std::size_t>(i)].unicode) {
            utf8::append(out, c);
        }
    }
    return out;
}

// Построитель модели: обходит потоки страницы и форм, ведёт графическое и текстовое состояние.
class ModelBuilder {
public:
    ModelBuilder(fz_context* ctx, pdf_document* doc, ContentModel& model, fz_cookie* cookie)
        : ctx_(ctx), doc_(doc), m_(model), cookie_(cookie) {}

    void run(pdf_obj* resources, pdf_obj* contents) {
        GState initial;
        initial.ctm = m_.pageCtm_;
        gstates_.push_back(initial);
        resources_.push_back(resources);

        // Потоки страницы: элементы массива /Contents по порядку (не потоки — пропуск, как у MuPDF).
        std::vector<pdf_obj*> refs;
        int isArray = 0;
        int n = 0;
        mu::call(ctx_, [&] {
            isArray = pdf_is_array(ctx_, contents);
            n = isArray ? pdf_array_len(ctx_, contents) : (pdf_is_stream(ctx_, contents) ? 1 : 0);
        });
        for (int i = 0; i < n; ++i) {
            pdf_obj* ref = contents;
            int stream = 0;
            mu::call(ctx_, [&] {
                ref = isArray ? pdf_array_get(ctx_, contents, i) : contents;
                stream = pdf_is_stream(ctx_, ref);
            });
            if (stream) {
                refs.push_back(ref);
            }
        }
        for (pdf_obj* ref : refs) {
            const int index = addStream(ref, false, -1, 0);
            if (!stopped_) {
                process(index);
                splitPending_ = !pending_.empty();
            }
        }
    }

private:
    int addStream(pdf_obj* ref, bool form, int parentOp, int depth) {
        ContentStream cs;
        cs.form = form;
        cs.parentOp = parentOp;
        cs.depth = depth;
        mu::BufferHandle buf(ctx_);
        try {
            mu::call(ctx_, [&] {
                cs.objnum = pdf_to_num(ctx_, ref);
                cs.gen = pdf_to_gen(ctx_, ref);
                buf.reset(pdf_load_stream(ctx_, ref));
            });
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System) {
                throw;
            }
            buf.reset();
        }
        if (buf) {
            unsigned char* data = nullptr;
            const std::size_t len = fz_buffer_storage(ctx_, buf, &data);
            cs.bytes.assign(reinterpret_cast<const char*>(data), len);
        }
        m_.streams_.push_back(std::move(cs));
        return static_cast<int>(m_.streams_.size()) - 1;
    }

    GState& gs() { return gstates_.back(); }

    void checkCookie() {
        if (cookie_ != nullptr && cookie_->abort != 0) {
            throw MuError(FZ_ERROR_ABORT, "content model aborted");
        }
    }

    // Новая операция [start, end) потока stream.
    int addOp(int stream, OpKind kind, std::string name, std::size_t start, std::size_t end) {
        Operation op;
        op.kind = kind;
        op.name = std::move(name);
        op.operands = std::move(pending_);
        pending_.clear();
        op.stream = stream;
        op.offset = start;
        op.length = end - start;
        op.tokenOffset = skipSpace(m_.streams_[static_cast<std::size_t>(stream)].bytes, start);
        if (op.tokenOffset > end) {
            op.tokenOffset = end;
        }
        op.split = splitPending_ && !op.operands.empty();
        splitPending_ = false;
        op.textObject = inText_ ? currentText_ : -1;
        m_.ops_.push_back(std::move(op));
        const int index = static_cast<int>(m_.ops_.size()) - 1;
        m_.streams_[static_cast<std::size_t>(stream)].ops.push_back(index);
        return index;
    }

    void syntaxError() {
        ++m_.syntaxErrors_;
        if (inText_ && currentText_ >= 0) {
            m_.texts_[static_cast<std::size_t>(currentText_)].syntaxError = true;
        }
        if (m_.syntaxErrors_ >= kMaxSyntaxErrors) {
            stopped_ = true;  // «too many syntax errors; ignoring rest of page»
        }
    }

    void process(int stream) {
        // Лексер читает свою копию байтов: формы (doXObject) добавляют потоки, и вектор потоков переезжает.
        const std::string bytes = m_.streams_[static_cast<std::size_t>(stream)].bytes;
        Lexer lex(ctx_, bytes);
        std::size_t start = 0;
        int counter = 0;
        while (!stopped_) {
            if ((++counter & 255) == 0) {
                checkCookie();
            }
            const pdf_token tok = lex.next();
            const std::size_t end = lex.tell();
            if (tok == PDF_TOK_EOF) {
                break;
            }
            if (tok == PDF_TOK_ENDSTREAM) {
                stopped_ = true;  // MuPDF: endstream — конец содержимого страницы
                break;
            }
            switch (tok) {
                case PDF_TOK_INT:
                case PDF_TOK_REAL: {
                    Operand o;
                    o.kind = Operand::Kind::Number;
                    o.number = lex.number(tok);
                    pending_.push_back(std::move(o));
                    break;
                }
                case PDF_TOK_NAME: {
                    Operand o;
                    o.kind = Operand::Kind::Name;
                    o.text = lex.text();
                    pending_.push_back(std::move(o));
                    break;
                }
                case PDF_TOK_STRING: {
                    Operand o;
                    o.kind = Operand::Kind::String;
                    o.text = lex.text();
                    pending_.push_back(std::move(o));
                    break;
                }
                case PDF_TOK_OPEN_ARRAY:
                    if (inText_) {
                        if (!parseTextArray(lex)) {
                            addOp(stream, OpKind::Garbage, {}, start, lex.tell());
                            syntaxError();
                            start = lex.tell();
                        }
                    } else if (!parseObject(lex, true)) {
                        addOp(stream, OpKind::Garbage, {}, start, lex.tell());
                        syntaxError();
                        start = lex.tell();
                    }
                    break;
                case PDF_TOK_OPEN_DICT:
                    if (!parseObject(lex, false)) {
                        addOp(stream, OpKind::Garbage, {}, start, lex.tell());
                        syntaxError();
                        start = lex.tell();
                    }
                    break;
                case PDF_TOK_KEYWORD: {
                    const std::string name(lex.keyword());
                    if (name == "BI") {
                        const bool ok = skipInlineImage(lex);
                        addOp(stream, ok ? OpKind::InlineImage : OpKind::Garbage, ok ? "BI" : "", start,
                              lex.tell());
                        if (!ok) {
                            syntaxError();
                        }
                        start = lex.tell();
                        break;
                    }
                    const int op = addOp(stream, kindOf(name), name, start, end);
                    start = end;
                    interpret(op);
                    break;
                }
                default:  // ERROR, «]», «>>», «{», «}», true/false/null, R, obj… — ошибка разбора
                    addOp(stream, OpKind::Garbage, {}, start, end);
                    syntaxError();
                    start = end;
                    break;
            }
        }
        m_.streams_[static_cast<std::size_t>(stream)].tail = start;
    }

    // Массив в тексте (как in_text_array интерпретатора): числа и строки; Tw/Tc после числа — сразу.
    bool parseTextArray(Lexer& lex) {
        Operand arr;
        arr.kind = Operand::Kind::Array;
        for (;;) {
            const pdf_token tok = lex.next();
            switch (tok) {
                case PDF_TOK_CLOSE_ARRAY:
                    pending_.push_back(std::move(arr));
                    return true;
                case PDF_TOK_INT:
                case PDF_TOK_REAL: {
                    Operand o;
                    o.kind = Operand::Kind::Number;
                    o.number = lex.number(tok);
                    arr.items.push_back(std::move(o));
                    break;
                }
                case PDF_TOK_STRING: {
                    Operand o;
                    o.kind = Operand::Kind::String;
                    o.text = lex.text();
                    arr.items.push_back(std::move(o));
                    break;
                }
                case PDF_TOK_EOF:
                    return false;
                case PDF_TOK_KEYWORD: {
                    const std::string_view k = lex.keyword();
                    if ((k == "Tw" || k == "Tc") && !arr.items.empty() && arr.items.back().isNumber()) {
                        const float v = static_cast<float>(arr.items.back().number);
                        arr.items.pop_back();
                        (k == "Tw" ? gs().wordSpace : gs().charSpace) = v;
                        if (currentText_ >= 0) {
                            m_.texts_[static_cast<std::size_t>(currentText_)].syntaxError = true;
                        }
                        break;
                    }
                    return false;
                }
                default:
                    return false;
            }
        }
    }

    // Массив или словарь вне текста — pdf_parse_array / pdf_parse_dict MuPDF (те же токены, что съест он).
    bool parseObject(Lexer& lex, bool array) {
        mu::PdfObjHandle obj(ctx_);
        try {
            mu::call(ctx_, [&] {
                obj.reset(array ? pdf_parse_array(ctx_, doc_, lex.stream(), lex.buf())
                                : pdf_parse_dict(ctx_, doc_, lex.stream(), lex.buf()));
            });
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System) {
                throw;
            }
            pending_.clear();
            return false;
        }
        pending_.push_back(toOperand(ctx_, obj));
        return true;
    }

    // BI … ID данные EI — как parse_inline_image MuPDF: словарь до ID, пробел после ID, данные по длине
    // картинки (pdf_load_inline_image), затем поиск «EI» с разделителем после.
    bool skipInlineImage(Lexer& lex) {
        mu::PdfObjHandle dict(ctx_);
        mu::ImageHandle image(ctx_);
        int found = 0;
        pdf_resource_stack rdb{nullptr, resources_.back()};
        try {
            mu::call(ctx_, [&] {
                fz_stream* stm = lex.stream();
                dict.reset(pdf_parse_dict(ctx_, doc_, stm, lex.buf()));
                int ch = fz_read_byte(ctx_, stm);
                if (ch == '\r' && fz_peek_byte(ctx_, stm) == '\n') {
                    fz_read_byte(ctx_, stm);
                }
                image.reset(pdf_load_inline_image(ctx_, doc_, &rdb, dict, stm));
                ch = fz_read_byte(ctx_, stm);
                do {
                    while (ch != 'E' && ch != EOF) {
                        ch = fz_read_byte(ctx_, stm);
                    }
                    if (ch == 'E') {
                        ch = fz_read_byte(ctx_, stm);
                        if (ch == 'I') {
                            ch = fz_peek_byte(ctx_, stm);
                            if (ch == ' ' || ch <= 32 || ch == '<' || ch == '/') {
                                found = 1;
                                break;
                            }
                        }
                    }
                } while (ch != EOF);
            });
        } catch (const MuError& e) {
            if (e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System) {
                throw;
            }
            pending_.clear();
            return false;
        }
        pending_.clear();
        if (dict) {
            pending_.push_back(toOperand(ctx_, dict));
        }
        if (found == 0) {
            pending_.clear();
        }
        return found != 0;
    }

    // Числа операции: i-е число (как s[i] MuPDF — недостающие нули).
    static float num(const Operation& op, std::size_t i) {
        std::size_t k = 0;
        for (const Operand& o : op.operands) {
            if (o.isNumber()) {
                if (k == i) {
                    return static_cast<float>(o.number);
                }
                ++k;
            }
        }
        return 0.0f;
    }
    static const Operand* first(const Operation& op, Operand::Kind kind) {
        for (const Operand& o : op.operands) {
            if (o.kind == kind) {
                return &o;
            }
        }
        return nullptr;
    }
    static const Operand* last(const Operation& op, Operand::Kind kind) {
        for (auto it = op.operands.rbegin(); it != op.operands.rend(); ++it) {
            if (it->kind == kind) {
                return &*it;
            }
        }
        return nullptr;
    }

    pdf_obj* lookupResource(pdf_obj* type, const char* name) {
        pdf_obj* found = nullptr;
        for (auto it = resources_.rbegin(); it != resources_.rend() && found == nullptr; ++it) {
            pdf_obj* res = *it;
            mu::call(ctx_, [&] { found = pdf_dict_gets(ctx_, pdf_dict_get(ctx_, res, type), name); });
        }
        return found;
    }

    // Шрифт словаря dict: один GlyphMap на объект шрифта (у прямого словаря — на указатель, у отсутствующего
    // — на имя ресурса).
    int fontFor(pdf_obj* dict, const std::string& resource) {
        int num = 0;
        if (dict != nullptr) {
            mu::call(ctx_, [&] { num = pdf_to_num(ctx_, dict); });
        }
        int* cached = nullptr;
        if (num > 0) {
            cached = &fontByNum_.try_emplace(num, -1).first->second;
        } else if (dict != nullptr) {
            cached = &fontByPtr_.try_emplace(dict, -1).first->second;
        } else {
            cached = &fontByName_.try_emplace(resource, -1).first->second;
        }
        if (*cached < 0) {
            maps_.push_back(GlyphMap::load(ctx_, doc_, resources_.back(), dict, resource));
            m_.fonts_.push_back(maps_.back()->info());
            *cached = static_cast<int>(m_.fonts_.size()) - 1;
        }
        return *cached;
    }

    void setColor(ColorState& c, const Operation& op, int comps) {
        std::vector<float> v;
        for (const Operand& o : op.operands) {
            if (o.isNumber()) {
                v.push_back(static_cast<float>(o.number));
            }
        }
        if (comps > 0) {
            c.pattern = false;
            c.comps = comps;
            v.resize(static_cast<std::size_t>(comps), 0.0f);
            c.argb = argbFromComponents(v);
            return;
        }
        // sc/scn: компоненты текущего пространства; у /Pattern — имя узора (и компоненты неокрашенного).
        if (c.pattern) {
            return;
        }
        if (!v.empty()) {
            c.argb = argbFromComponents(v);
        }
    }

    void setColorSpace(ColorState& c, const Operation& op) {
        const Operand* name = first(op, Operand::Kind::Name);
        const std::string n = name != nullptr ? name->text : "";
        c.pattern = false;
        if (n == "DeviceGray" || n == "G" || n == "CalGray") {
            c.comps = 1;
        } else if (n == "DeviceRGB" || n == "RGB" || n == "CalRGB") {
            c.comps = 3;
        } else if (n == "DeviceCMYK" || n == "CMYK") {
            c.comps = 4;
        } else if (n == "Pattern") {
            c.pattern = true;
        } else {
            pdf_obj* cs = lookupResource(PDF_NAME(ColorSpace), n.c_str());
            int pattern = 0;
            int comps = 1;
            if (cs != nullptr) {
                try {
                    mu::call(ctx_, [&] {
                        pdf_obj* family = pdf_is_array(ctx_, cs) ? pdf_array_get(ctx_, cs, 0) : cs;
                        pattern = pdf_name_eq(ctx_, family, PDF_NAME(Pattern));
                        if (pdf_name_eq(ctx_, family, PDF_NAME(DeviceRGB)) ||
                            pdf_name_eq(ctx_, family, PDF_NAME(CalRGB)) ||
                            pdf_name_eq(ctx_, family, PDF_NAME(Lab))) {
                            comps = 3;
                        } else if (pdf_name_eq(ctx_, family, PDF_NAME(DeviceCMYK))) {
                            comps = 4;
                        } else if (pdf_name_eq(ctx_, family, PDF_NAME(ICCBased))) {
                            comps = pdf_dict_get_int(ctx_, pdf_array_get(ctx_, cs, 1), PDF_NAME(N));
                        }
                    });
                } catch (const MuError& e) {
                    if (e.kind() == ErrorKind::Aborted || e.kind() == ErrorKind::System) {
                        throw;
                    }
                }
            }
            c.pattern = pattern != 0;
            c.comps = comps > 0 ? comps : 1;
        }
        std::vector<float> zero(static_cast<std::size_t>(c.comps), 0.0f);
        if (c.comps == 4) {
            zero[3] = 1.0f;
        }
        c.argb = argbFromComponents(zero);
    }

    void beginText(int op) {
        TextObject t;
        t.begin = op;
        m_.texts_.push_back(t);
        currentText_ = static_cast<int>(m_.texts_.size()) - 1;
        inText_ = true;
        m_.ops_[static_cast<std::size_t>(op)].textObject = currentText_;
        tm_ = tlm_ = Matrix{};
    }

    void interpret(int index) {
        Operation& op = m_.ops_[static_cast<std::size_t>(index)];
        GState& g = gs();
        switch (op.kind) {
            case OpKind::Save: {
                const GState copy = g;
                gstates_.push_back(copy);
                break;
            }
            case OpKind::Restore:
                if (gstates_.size() > baseDepth_ + 1) {
                    gstates_.pop_back();
                }
                break;
            case OpKind::Concat: {
                const Matrix m{num(op, 0), num(op, 1), num(op, 2), num(op, 3), num(op, 4), num(op, 5)};
                g.ctm = concat(m, g.ctm);
                g.userCtm = concat(m, g.userCtm);
                break;
            }
            case OpKind::ExtGState:
                applyExtGState(op);
                break;
            case OpKind::BeginText:
                beginText(index);
                break;
            case OpKind::EndText:
                if (inText_ && currentText_ >= 0) {
                    m_.texts_[static_cast<std::size_t>(currentText_)].end = index;
                    op.textObject = currentText_;
                }
                inText_ = false;
                break;
            case OpKind::Font: {
                const Operand* name = first(op, Operand::Kind::Name);
                const std::string res = name != nullptr ? name->text : "";
                pdf_obj* dict = lookupResource(PDF_NAME(Font), res.c_str());
                int isDict = 0;
                mu::call(ctx_, [&] { isDict = pdf_is_dict(ctx_, dict); });
                g.font = fontFor(isDict ? dict : nullptr, res);
                g.size = num(op, 0);
                break;
            }
            case OpKind::Move:
                tlm_ = preTranslate(tlm_, num(op, 0), num(op, 1));
                tm_ = tlm_;
                break;
            case OpKind::MoveLeading:
                g.leading = -num(op, 1);
                tlm_ = preTranslate(tlm_, num(op, 0), num(op, 1));
                tm_ = tlm_;
                break;
            case OpKind::TextMatrix:
                tm_ = tlm_ = Matrix{num(op, 0), num(op, 1), num(op, 2), num(op, 3), num(op, 4), num(op, 5)};
                break;
            case OpKind::NextLine:
                tlm_ = preTranslate(tlm_, 0, -g.leading);
                tm_ = tlm_;
                break;
            case OpKind::Leading:
                g.leading = num(op, 0);
                break;
            case OpKind::CharSpace:
                g.charSpace = num(op, 0);
                break;
            case OpKind::WordSpace:
                g.wordSpace = num(op, 0);
                break;
            case OpKind::Scale:
                g.scale = num(op, 0) / 100.0f;
                break;
            case OpKind::Rise:
                g.rise = num(op, 0);
                break;
            case OpKind::Render:
                g.render = static_cast<int>(num(op, 0));
                break;
            case OpKind::ShowText:
            case OpKind::ShowArray:
            case OpKind::NextLineShow:
            case OpKind::NextLineShowSpaced:
                show(index);
                break;
            case OpKind::XObject:
                doXObject(index);
                break;
            case OpKind::BeginCompat:
            case OpKind::EndCompat:
                break;
            case OpKind::BeginMarked: {
                int mcid = mcids_.empty() ? -1 : mcids_.back();
                if (op.name == "BDC") {
                    const Operand* props = last(op, Operand::Kind::Dict);
                    if (props != nullptr) {
                        if (const Operand* v = props->get("MCID"); v != nullptr && v->isNumber()) {
                            mcid = static_cast<int>(v->number);
                        }
                    }
                }
                mcids_.push_back(mcid);
                break;
            }
            case OpKind::EndMarked:
                if (!mcids_.empty()) {
                    mcids_.pop_back();
                }
                break;
            case OpKind::FillColor:
            case OpKind::StrokeColor: {
                ColorState& c = op.kind == OpKind::FillColor ? g.fill : g.stroke;
                const std::string& k = op.name;
                if (k == "g" || k == "G") {
                    setColor(c, op, 1);
                } else if (k == "rg" || k == "RG") {
                    setColor(c, op, 3);
                } else if (k == "k" || k == "K") {
                    setColor(c, op, 4);
                } else if (k == "cs" || k == "CS") {
                    setColorSpace(c, op);
                } else {
                    setColor(c, op, 0);
                }
                break;
            }
            default:
                break;
        }
    }

    void applyExtGState(const Operation& op) {
        const Operand* name = first(op, Operand::Kind::Name);
        if (name == nullptr) {
            return;
        }
        pdf_obj* gsDict = lookupResource(PDF_NAME(ExtGState), name->text.c_str());
        pdf_obj* font = nullptr;
        float size = 0;
        mu::call(ctx_, [&] {
            pdf_obj* f = pdf_dict_get(ctx_, gsDict, PDF_NAME(Font));
            if (pdf_is_array(ctx_, f)) {
                font = pdf_array_get(ctx_, f, 0);
                size = pdf_array_get_real(ctx_, f, 1);
            }
        });
        if (font != nullptr) {
            gs().font = fontFor(font, "gs:" + name->text);
            gs().size = size;
        }
    }

    void doXObject(int index) {
        const Operation& op = m_.ops_[static_cast<std::size_t>(index)];
        const Operand* name = first(op, Operand::Kind::Name);
        if (name == nullptr || formCount_ >= kMaxForms) {
            return;
        }
        pdf_obj* xobj = lookupResource(PDF_NAME(XObject), name->text.c_str());
        int isForm = 0;
        int objnum = 0;
        Matrix matrix;
        pdf_obj* res = nullptr;
        mu::call(ctx_, [&] {
            isForm = pdf_is_stream(ctx_, xobj) &&
                     pdf_name_eq(ctx_, pdf_dict_get(ctx_, xobj, PDF_NAME(Subtype)), PDF_NAME(Form));
            if (isForm) {
                objnum = pdf_to_num(ctx_, xobj);
                const fz_matrix fm = pdf_dict_get_matrix(ctx_, xobj, PDF_NAME(Matrix));
                matrix = Matrix{fm.a, fm.b, fm.c, fm.d, fm.e, fm.f};
                res = pdf_dict_get(ctx_, xobj, PDF_NAME(Resources));
            }
        });
        if (!isForm) {
            return;
        }
        const int depth = m_.streams_[static_cast<std::size_t>(op.stream)].depth + 1;
        if (depth > kMaxFormDepth ||
            std::find(formStack_.begin(), formStack_.end(), objnum) != formStack_.end()) {
            return;
        }
        ++formCount_;
        // Форма — своё состояние операндов, свой стек q/Q поверх текущего, текстовое состояние сохраняется.
        const std::vector<Operand> savedPending = std::move(pending_);
        pending_.clear();
        const bool savedSplit = splitPending_;
        splitPending_ = false;
        const bool savedInText = inText_;
        const int savedText = currentText_;
        const Matrix savedTm = tm_;
        const Matrix savedTlm = tlm_;
        const std::size_t savedBase = baseDepth_;
        const std::size_t savedDepth = gstates_.size();
        const std::size_t savedMcids = mcids_.size();
        GState g = gs();
        g.ctm = concat(matrix, g.ctm);
        g.userCtm = concat(matrix, g.userCtm);
        gstates_.push_back(g);
        baseDepth_ = gstates_.size() - 1;
        resources_.push_back(res != nullptr ? res : resources_.back());
        formStack_.push_back(objnum);
        inText_ = false;
        const int stream = addStream(xobj, true, index, depth);
        process(stream);
        formStack_.pop_back();
        resources_.pop_back();
        gstates_.resize(savedDepth);
        baseDepth_ = savedBase;
        mcids_.resize(std::min(mcids_.size(), savedMcids));
        tm_ = savedTm;
        tlm_ = savedTlm;
        inText_ = savedInText;
        currentText_ = savedText;
        pending_ = savedPending;
        splitPending_ = savedSplit;
        if (stopped_ && m_.syntaxErrors_ < kMaxSyntaxErrors) {
            stopped_ = false;  // endstream внутри формы завершает только форму
        }
    }

    void show(int index) {
        Operation& op = m_.ops_[static_cast<std::size_t>(index)];
        GState& g = gs();
        if (op.kind == OpKind::NextLineShowSpaced) {
            g.wordSpace = num(op, 0);
            g.charSpace = num(op, 1);
        }
        if (op.kind == OpKind::NextLineShow || op.kind == OpKind::NextLineShowSpaced) {
            tlm_ = preTranslate(tlm_, 0, -g.leading);
            tm_ = tlm_;
        }
        if (g.font < 0) {
            return;  // «cannot draw text since font and size not set»
        }
        // Строки и сдвиги оператора: Tj, ', " — последняя строка; TJ — последний массив.
        std::vector<const Operand*> items;
        if (op.kind == OpKind::ShowArray) {
            const Operand* arr = last(op, Operand::Kind::Array);
            if (arr != nullptr) {
                for (const Operand& o : arr->items) {
                    items.push_back(&o);
                }
            } else if (const Operand* s = last(op, Operand::Kind::String); s != nullptr) {
                items.push_back(s);
            }
        } else if (const Operand* s = last(op, Operand::Kind::String); s != nullptr) {
            items.push_back(s);
        }

        GlyphMap& map = *maps_[static_cast<std::size_t>(g.font)];
        const FontInfo& font = m_.fonts_[static_cast<std::size_t>(g.font)];
        const bool vertical = map.vertical();
        TextSpan span;
        span.op = index;
        span.textObject = inText_ ? currentText_ : -1;
        span.font = g.font;
        span.size = g.size;
        span.charSpace = g.charSpace;
        span.wordSpace = g.wordSpace;
        span.scale = g.scale;
        span.leading = g.leading;
        span.rise = g.rise;
        span.render = g.render;
        span.fill = g.fill.argb;
        span.stroke = g.stroke.argb;
        span.tm = tm_;
        span.tlm = tlm_;
        span.ctm = g.userCtm;
        span.stream = op.stream;
        span.form = m_.streams_[static_cast<std::size_t>(op.stream)].form;
        span.mcid = mcids_.empty() ? -1 : mcids_.back();
        span.firstGlyph = static_cast<int>(m_.glyphs_.size());
        span.bbox = RectF::none();
        span.baseline = transformPoint({0, 0}, concat(tm_, g.ctm));

        float x = 0;  // сдвиг пера по x пространства текста от tm спана
        bool unknown = false;
        codes_.clear();
        for (std::size_t item = 0; item < items.size(); ++item) {
            const Operand& o = *items[item];
            if (o.kind == Operand::Kind::String) {
                codes_.clear();
                map.decode(o.text, codes_);
                std::uint32_t offset = 0;
                for (const GlyphMap::Code& c : codes_) {
                    if (c.cid >= 0) {
                        Matrix tsm{g.size * g.scale, 0, 0, g.size, 0, g.rise};
                        float tx = 0;
                        float ty = 0;
                        if (!vertical) {
                            tx = (c.w0 * g.size + g.charSpace) * g.scale;
                        } else {
                            tsm.e -= c.vx * g.size * g.scale;
                            tsm.f -= c.vy * g.size;
                            ty = c.w0 * g.size + g.charSpace;
                        }
                        const Matrix trm = concat(tsm, tm_);
                        const Matrix dev = concat(trm, g.ctm);
                        Glyph gl;
                        gl.unicode = c.unicode;
                        gl.code = c.code;
                        gl.codeBytes = c.bytes;
                        gl.cid = c.cid;
                        gl.gid = c.gid;
                        gl.w0 = c.w0;
                        gl.x = x;
                        gl.span = static_cast<int>(m_.spans_.size());
                        gl.item = static_cast<std::uint16_t>(std::min<std::size_t>(item, 0xFFFF));
                        gl.byteOffset = offset;
                        gl.origin = {dev.e, dev.f};
                        if (!vertical) {
                            const PointF dir = transformVector({1, 0}, dev);
                            const PointF a = transformVector({0, font.ascender}, dev);
                            const PointF d = transformVector({0, font.descender}, dev);
                            const PointF p = gl.origin;
                            const PointF q{p.x + c.w0 * dir.x, p.y + c.w0 * dir.y};
                            gl.quad = {add(p, a), add(q, a), add(p, d), add(q, d)};
                        } else {
                            const PointF dir = transformVector({0, -1}, dev);
                            const PointF a = transformVector({1, 0}, dev);
                            const PointF p{dev.e - c.w0 * dir.x, dev.f - c.w0 * dir.y};
                            const PointF q = gl.origin;
                            gl.quad = {add(p, a), add(q, a), p, q};
                        }
                        for (char32_t u : c.unicode) {
                            unknown = unknown || u == 0xFFFD;
                        }
                        span.bbox = unite(span.bbox, gl.quad.bounds());
                        m_.glyphs_.push_back(std::move(gl));
                        tm_ = preTranslate(tm_, tx, ty);
                        x += vertical ? ty : tx;
                    }
                    if (c.wordSpace()) {
                        if (!vertical) {
                            tm_ = preTranslate(tm_, g.wordSpace * g.scale, 0);
                            x += g.wordSpace * g.scale;
                        } else {
                            tm_ = preTranslate(tm_, 0, g.wordSpace);
                            x += g.wordSpace;
                        }
                    }
                    offset += c.bytes;
                }
            } else if (o.isNumber()) {
                const float tadj = -static_cast<float>(o.number) * g.size * 0.001f;
                if (!vertical) {
                    tm_ = preTranslate(tm_, tadj * g.scale, 0);
                    x += tadj * g.scale;
                } else {
                    tm_ = preTranslate(tm_, 0, tadj);
                    x += tadj;
                }
            }
        }
        span.advance = x;
        span.glyphCount = static_cast<int>(m_.glyphs_.size()) - span.firstGlyph;
        if (span.glyphCount == 0) {
            return;  // пустая строка — спана нет, сдвиг пера уже учтён
        }
        const Glyph& firstGlyph = m_.glyphs_[static_cast<std::size_t>(span.firstGlyph)];
        const Glyph& lastGlyph = m_.glyphs_.back();
        span.origin = firstGlyph.origin;
        const Matrix firstTrm =
            concat(Matrix{g.size * g.scale, 0, 0, g.size, 0, g.rise},
                   preTranslate(span.tm, vertical ? 0 : firstGlyph.x, vertical ? firstGlyph.x : 0));
        const Matrix dev = concat(firstTrm, g.ctm);
        const PointF dirv = transformVector({1, 0}, dev);
        const float len = std::hypot(dirv.x, dirv.y);
        span.dir = len > 0 ? PointF{dirv.x / len, dirv.y / len} : PointF{1, 0};
        span.end = {lastGlyph.origin.x + lastGlyph.w0 * dirv.x, lastGlyph.origin.y + lastGlyph.w0 * dirv.y};
        span.fontSize = std::sqrt(std::fabs(dev.a * dev.d - dev.b * dev.c));
        const Matrix user = concat(firstTrm, g.userCtm);
        span.angle = static_cast<float>(std::atan2(user.b, user.a) * 180.0 / 3.14159265358979323846);
        const bool upright = user.a > 0 && user.d > 0 && std::fabs(user.b) <= 0.01f * std::fabs(user.a) &&
                             std::fabs(user.c) <= 0.01f * std::fabs(user.d);
        const bool fills = span.render == 0 || span.render == 2 || span.render == 4 || span.render == 6;
        const bool strokes = span.render == 1 || span.render == 2 || span.render == 5 || span.render == 6;
        const bool textSyntax =
            span.textObject >= 0 && m_.texts_[static_cast<std::size_t>(span.textObject)].syntaxError;
        if (span.form) {
            span.reason = ReadOnly::Form;
        } else if (textSyntax) {
            span.reason = ReadOnly::Syntax;
        } else if (op.split) {
            span.reason = ReadOnly::Split;
        } else if (font.reason == ReadOnly::Type3 || font.reason == ReadOnly::Vertical) {
            span.reason = font.reason;
        } else if (!upright) {
            span.reason = ReadOnly::Rotated;
        } else if (font.reason != ReadOnly::None) {
            span.reason = font.reason;
        } else if (unknown) {
            span.reason = ReadOnly::NoUnicode;
        } else if ((fills && g.fill.pattern) || (strokes && g.stroke.pattern)) {
            span.reason = ReadOnly::Pattern;
        } else if (span.render >= 4) {
            span.reason = ReadOnly::Clip;
        }
        op.span = static_cast<int>(m_.spans_.size());
        if (span.textObject >= 0) {
            m_.texts_[static_cast<std::size_t>(span.textObject)].spans.push_back(op.span);
        }
        m_.spans_.push_back(std::move(span));
    }

    fz_context* ctx_;
    pdf_document* doc_;
    ContentModel& m_;
    fz_cookie* cookie_;
    std::vector<GState> gstates_;
    std::size_t baseDepth_ = 0;  // Q не снимает состояния ниже (форма — свой нижний уровень)
    std::vector<pdf_obj*> resources_;
    std::vector<Operand> pending_;
    bool splitPending_ = false;
    bool stopped_ = false;
    bool inText_ = false;
    int currentText_ = -1;
    Matrix tm_;
    Matrix tlm_;
    std::vector<int> mcids_;
    std::vector<int> formStack_;
    int formCount_ = 0;
    std::vector<std::unique_ptr<GlyphMap>> maps_;
    std::unordered_map<int, int> fontByNum_;
    std::unordered_map<pdf_obj*, int> fontByPtr_;
    std::unordered_map<std::string, int> fontByName_;
    std::vector<GlyphMap::Code> codes_;
};

ContentModel ContentModel::build(const Document& doc, int page, fz_cookie* cookie) {
    if (doc.pdf() == nullptr) {
        throw std::logic_error("ContentModel: not a PDF document");
    }
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ContentModel model;
    model.page_ = page;
    mu::PageHandle p(ctx);
    pdf_obj* resources = nullptr;
    pdf_obj* contents = nullptr;
    fz_rect mediabox{};
    fz_matrix ctm = fz_identity;
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        pdf_page* pp = pdf_page_from_fz_page(ctx, p);
        pdf_page_transform(ctx, pp, &mediabox, &ctm);
        resources = pdf_page_resources(ctx, pp);
        contents = pdf_page_contents(ctx, pp);
    });
    model.pageCtm_ = fromFz(ctm);
    const fz_rect bounds = fz_transform_rect(mediabox, ctm);
    model.bounds_ = {bounds.x0, bounds.y0, bounds.x1, bounds.y1};
    ModelBuilder builder(ctx, pdf, model, cookie);
    builder.run(resources, contents);
    return model;
}

CrossCheck ContentModel::crossCheck(const Document& doc, int page, const ContentModel& model,
                                    float tolerance) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    mu::PageHandle p(ctx);
    mu::StextPageHandle text(ctx);
    mu::DeviceHandle dev(ctx);
    ProcessorHandle proc(ctx);
    DefaultColorspacesHandle dcs(ctx);
    mu::call(ctx, [&] {
        p.reset(fz_load_page(ctx, doc.handle(), page));
        pdf_page* pp = pdf_page_from_fz_page(ctx, p);
        fz_rect mediabox;
        fz_matrix ctm;
        pdf_page_transform(ctx, pp, &mediabox, &ctm);
        text.reset(fz_new_stext_page(ctx, fz_transform_rect(mediabox, ctm)));
        fz_stext_options opts;
        fz_init_stext_options(ctx, &opts);
        opts.flags = FZ_STEXT_PRESERVE_LIGATURES | FZ_STEXT_PRESERVE_WHITESPACE | FZ_STEXT_INHIBIT_SPACES |
                     FZ_STEXT_IGNORE_ACTUALTEXT;
        dev.reset(fz_new_stext_device(ctx, text, &opts));
        dcs.reset(pdf_load_default_colorspaces(ctx, pdf, pp));
        proc.reset(
            pdf_new_run_processor(ctx, pdf, dev, ctm, -1, "View", nullptr, dcs, nullptr, nullptr, nullptr));
        pdf_process_contents(ctx, proc, pdf, pdf_page_resources(ctx, pp), pdf_page_contents(ctx, pp), nullptr,
                             nullptr);
        pdf_close_processor(ctx, proc);
        fz_close_device(ctx, dev);
    });

    struct Char {
        char32_t c;
        QuadF quad;
        bool zeroWidth;
    };
    std::vector<Char> stext;
    for (fz_stext_block* b = text->first_block; b != nullptr; b = b->next) {
        if (b->type != FZ_STEXT_BLOCK_TEXT) {
            continue;
        }
        for (fz_stext_line* l = b->u.t.first_line; l != nullptr; l = l->next) {
            for (fz_stext_char* ch = l->first_char; ch != nullptr; ch = ch->next) {
                const fz_quad& q = ch->quad;
                stext.push_back({static_cast<char32_t>(ch->c),
                                 {{q.ul.x, q.ul.y}, {q.ur.x, q.ur.y}, {q.ll.x, q.ll.y}, {q.lr.x, q.lr.y}},
                                 false});
            }
        }
    }
    std::vector<Char> mine;
    for (const Glyph& g : model.glyphs()) {
        for (std::size_t i = 0; i < g.unicode.size(); ++i) {
            if (i == 0 && !isCombiningMark(g.unicode[0])) {
                mine.push_back({g.unicode[0], g.quad, false});
            } else {
                // Заполнители «один глиф — несколько символов» и метки — нулевой ширины у пера: углы не
                // сверяются.
                mine.push_back({g.unicode[i], {g.quad.ur, g.quad.ur, g.quad.lr, g.quad.lr}, true});
            }
        }
    }
    CrossCheck out;
    out.modelChars = static_cast<int>(mine.size());
    out.stextChars = static_cast<int>(stext.size());
    const std::size_t n = std::min(mine.size(), stext.size());
    auto dist = [](PointF a, PointF b) { return std::fmax(std::fabs(a.x - b.x), std::fabs(a.y - b.y)); };
    for (std::size_t i = 0; i < n; ++i) {
        const Char& a = mine[i];
        const Char& b = stext[i];
        float delta = 0;
        if (!a.zeroWidth) {
            delta = std::fmax(std::fmax(dist(a.quad.ul, b.quad.ul), dist(a.quad.ur, b.quad.ur)),
                              std::fmax(dist(a.quad.ll, b.quad.ll), dist(a.quad.lr, b.quad.lr)));
        }
        ++out.compared;
        out.maxDelta = std::fmax(out.maxDelta, delta);
        if (a.c != b.c || delta > tolerance) {
            if (out.mismatches == 0) {
                char buf[192];
                std::snprintf(buf, sizeof buf,
                              "char %zu: model U+%04X (%.2f, %.2f) vs stext U+%04X (%.2f, %.2f)", i,
                              static_cast<unsigned>(a.c), a.quad.ll.x, a.quad.ll.y,
                              static_cast<unsigned>(b.c), b.quad.ll.x, b.quad.ll.y);
                out.firstMismatch = buf;
            }
            ++out.mismatches;
        }
    }
    if (out.mismatches == 0 && mine.size() != stext.size()) {
        out.firstMismatch =
            "char count: model " + std::to_string(mine.size()) + " vs stext " + std::to_string(stext.size());
    }
    return out;
}

}  // namespace ot::edit
