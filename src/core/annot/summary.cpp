#include "core/annot/summary.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/annot/annot_internal.h"
#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"

namespace ot::annot {

namespace {

namespace fs = std::filesystem;

using Rgb = std::array<float, 3>;
// fz_drop_* текста и пути принимают const — обёртки под mu::Handle.
void dropText(fz_context* ctx, fz_text* t) { fz_drop_text(ctx, t); }
void dropPath(fz_context* ctx, fz_path* p) { fz_drop_path(ctx, p); }
void dropStroke(fz_context* ctx, fz_stroke_state* s) { fz_drop_stroke_state(ctx, s); }
using TextHandle = mu::Handle<fz_text, dropText>;
using PathHandle = mu::Handle<fz_path, dropPath>;
using StrokeHandle = mu::Handle<fz_stroke_state, dropStroke>;

constexpr Rgb kInk = {0.12f, 0.12f, 0.12f};
constexpr Rgb kMuted = {0.43f, 0.43f, 0.45f};
constexpr Rgb kRule = {0.85f, 0.85f, 0.86f};
constexpr float kMargin = 28.0f;  // поля листа
constexpr float kPad = 6.0f;      // поля карточки
constexpr float kGap = 8.0f;      // между карточками
constexpr float kA4W = 595.0f;
constexpr float kA4H = 842.0f;
constexpr float kPageShare = 0.6f;  // доля листа под страницу (эталон 3.4: 60 % слева)

fs::path fontFile(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return {};
    }
    return fs::path(dir) / L"Fonts" / name;
}

// Segoe UI (обычный / полужирный) из системных; нет — Helvetica MuPDF.
mu::FontHandle loadFont(fz_context* ctx, const wchar_t* file, const char* fallback) {
    mu::FontHandle font(ctx);
    const fs::path path = fontFile(file);
    std::error_code ec;
    if (!path.empty() && fs::exists(path, ec)) {
        const std::string bytes = paths::readFile(path);
        mu::BufferHandle buf(ctx);
        try {
            mu::call(ctx, [&] {
                buf.reset(fz_new_buffer_from_copied_data(
                    ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
                font.reset(fz_new_font_from_buffer(ctx, nullptr, buf, 0, 0));
            });
            return font;
        } catch (const MuError&) {  // NOLINT(bugprone-empty-catch): дальше — запасной шрифт
        }
    }
    mu::call(ctx, [&] { font.reset(fz_new_base14_font(ctx, fallback)); });
    return font;
}

Rgb accentOf(const AnnotColor& c) {
    if (c.empty()) {
        return kMuted;
    }
    const std::uint32_t rgb = c.toRgb();
    Rgb out = {static_cast<float>((rgb >> 16) & 0xFF) / 255.0f,
               static_cast<float>((rgb >> 8) & 0xFF) / 255.0f, static_cast<float>(rgb & 0xFF) / 255.0f};
    // Слишком светлые (жёлтый маркер) — темнее, чтобы линия и полоса были видны на белом.
    const float luminance = 0.2126f * out[0] + 0.7152f * out[1] + 0.0722f * out[2];
    if (luminance > 0.75f) {
        for (float& v : out) {
            v *= 0.75f;
        }
    }
    return out;
}

std::string dateString(std::int64_t seconds) {
    if (seconds <= 0) {
        return {};
    }
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &tm);
    return buf;
}

// Строка карточки.
struct Line {
    std::string text;
    bool bold = false;
    float size = 9;
    float indent = 0;
    Rgb color = kInk;
};

struct Card {
    const Comment* comment = nullptr;
    int number = 0;
    std::vector<Line> lines;
    float height = 0;
    Rgb accent = kMuted;
};

class Writer {
public:
    Writer(fz_context* ctx, float fontSize)
        : ctx_(ctx), buf_(ctx), writer_(ctx), regular_(ctx), bold_(ctx), size_(fontSize) {
        regular_ = loadFont(ctx, L"segoeui.ttf", "Helvetica");
        bold_ = loadFont(ctx, L"segoeuib.ttf", "Helvetica-Bold");
        mu::call(ctx, [&] {
            buf_.reset(fz_new_buffer(ctx, 65536));
            writer_.reset(fz_new_document_writer_with_buffer(ctx, buf_, "pdf", ""));
        });
    }

    float lineHeight(float size) const { return size * 1.35f; }

    float width(const std::string& text, bool bold, float size) const {
        fz_font* font = bold ? bold_.get() : regular_.get();
        float w = 0;
        mu::call(ctx_, [&] {
            for (const char32_t c : utf8::decode(text)) {
                int gid = fz_encode_character(ctx_, font, static_cast<int>(c));
                if (gid == 0) {
                    gid = fz_encode_character(ctx_, font, '?');
                }
                w += fz_advance_glyph(ctx_, font, gid, 0) * size;
            }
        });
        return w;
    }

    // Перенос по словам в ширину width (длинное слово — по символам).
    std::vector<std::string> wrap(const std::string& text, bool bold, float size, float width) const {
        std::vector<std::string> out;
        std::size_t start = 0;
        while (start <= text.size()) {
            const std::size_t nl = text.find('\n', start);
            const std::string para =
                text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            std::string line;
            std::size_t i = 0;
            while (i <= para.size()) {
                const std::size_t sp = para.find(' ', i);
                const std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
                const std::string candidate = line.empty() ? word : line + " " + word;
                if (this->width(candidate, bold, size) <= width || line.empty()) {
                    line = candidate;
                    // Слово само шире строки — по символам.
                    while (this->width(line, bold, size) > width) {
                        std::u32string u = utf8::decode(line);
                        std::size_t fit = 1;
                        while (fit < u.size() &&
                               this->width(utf8::encode(u.substr(0, fit + 1)), bold, size) <= width) {
                            ++fit;
                        }
                        out.push_back(utf8::encode(u.substr(0, fit)));
                        line = utf8::encode(u.substr(fit));
                    }
                } else {
                    out.push_back(line);
                    line = word;
                }
                if (sp == std::string::npos) {
                    break;
                }
                i = sp + 1;
            }
            out.push_back(line);
            if (nl == std::string::npos) {
                break;
            }
            start = nl + 1;
        }
        return out;
    }

    fz_device* begin(float w, float h) {
        fz_device* dev = nullptr;
        mu::call(ctx_, [&] { dev = fz_begin_page(ctx_, writer_, fz_make_rect(0, 0, w, h)); });
        ++pages_;
        return dev;
    }
    void end() {
        mu::call(ctx_, [&] { fz_end_page(ctx_, writer_); });
    }

    void text(fz_device* dev, float x, float baseline, const std::string& s, bool bold, float size,
              const Rgb& color) {
        if (s.empty()) {
            return;
        }
        fz_font* font = bold ? bold_.get() : regular_.get();
        TextHandle t(ctx_);
        mu::call(ctx_, [&] {
            t.reset(fz_new_text(ctx_));
            fz_show_string(ctx_, t, font, fz_make_matrix(size, 0, 0, -size, x, baseline), s.c_str(), 0, 0,
                           FZ_BIDI_LTR, FZ_LANG_UNSET);
            fz_fill_text(ctx_, dev, t, fz_identity, fz_device_rgb(ctx_), color.data(), 1.0f,
                         fz_default_color_params);
        });
    }

    void line(fz_device* dev, float x0, float y0, float x1, float y1, const Rgb& color, float width) {
        PathHandle p(ctx_);
        StrokeHandle stroke(ctx_);
        mu::call(ctx_, [&] {
            p.reset(fz_new_path(ctx_));
            fz_moveto(ctx_, p, x0, y0);
            fz_lineto(ctx_, p, x1, y1);
            stroke.reset(fz_new_stroke_state(ctx_));
            stroke->linewidth = width;
            fz_stroke_path(ctx_, dev, p, stroke, fz_identity, fz_device_rgb(ctx_), color.data(), 1.0f,
                           fz_default_color_params);
        });
    }

    void rect(fz_device* dev, float x0, float y0, float x1, float y1, const Rgb& color, bool fill,
              float width = 0.75f) {
        PathHandle p(ctx_);
        StrokeHandle stroke(ctx_);
        mu::call(ctx_, [&] {
            p.reset(fz_new_path(ctx_));
            fz_rectto(ctx_, p, x0, y0, x1, y1);
            if (fill) {
                fz_fill_path(ctx_, dev, p, 0, fz_identity, fz_device_rgb(ctx_), color.data(), 1.0f,
                             fz_default_color_params);
            } else {
                stroke.reset(fz_new_stroke_state(ctx_));
                stroke->linewidth = width;
                fz_stroke_path(ctx_, dev, p, stroke, fz_identity, fz_device_rgb(ctx_), color.data(), 1.0f,
                               fz_default_color_params);
            }
        });
    }

    // Кружок с номером (метка на странице и в карточке).
    void badge(fz_device* dev, float cx, float cy, int number, const Rgb& color) {
        const float r = size_ * 0.75f;
        PathHandle p(ctx_);
        mu::call(ctx_, [&] {
            p.reset(fz_new_path(ctx_));
            constexpr float k = 0.5523f;  // четверть окружности Безье
            fz_moveto(ctx_, p, cx + r, cy);
            fz_curveto(ctx_, p, cx + r, cy + r * k, cx + r * k, cy + r, cx, cy + r);
            fz_curveto(ctx_, p, cx - r * k, cy + r, cx - r, cy + r * k, cx - r, cy);
            fz_curveto(ctx_, p, cx - r, cy - r * k, cx - r * k, cy - r, cx, cy - r);
            fz_curveto(ctx_, p, cx + r * k, cy - r, cx + r, cy - r * k, cx + r, cy);
            fz_closepath(ctx_, p);
            fz_fill_path(ctx_, dev, p, 0, fz_identity, fz_device_rgb(ctx_), color.data(), 1.0f,
                         fz_default_color_params);
        });
        const std::string label = std::to_string(number);
        const float small = size_ * 0.8f;
        const float w = width(label, true, small);
        text(dev, cx - w / 2, cy + small * 0.35f, label, true, small, {1, 1, 1});
    }

    std::string finish(int* pages) {
        mu::call(ctx_, [&] { fz_close_document_writer(ctx_, writer_); });
        unsigned char* data = nullptr;
        const std::size_t len = fz_buffer_storage(ctx_, buf_, &data);
        *pages = pages_;
        return std::string(reinterpret_cast<const char*>(data), len);
    }

private:
    fz_context* ctx_;
    mu::BufferHandle buf_;
    mu::DocumentWriterHandle writer_;
    mu::FontHandle regular_;
    mu::FontHandle bold_;
    float size_;
    int pages_ = 0;
};

class Builder {
public:
    Builder(Document& doc, const SummaryOptions& o) : doc_(doc), o_(o), w_(doc.context().get(), o.fontSize) {}

    Card card(const Comment& c, int number, float width) {
        Card k;
        k.comment = &c;
        k.number = number;
        k.accent = accentOf(c.color);
        const float size = o_.fontSize;
        const float textWidth = width - 2 * kPad - size * 1.8f;  // место под номер
        std::string head =
            std::to_string(number) + ". " + (o_.texts.typeName ? o_.texts.typeName(c) : std::string());
        if (!c.author.empty()) {
            head += " \xE2\x80\x94 " + c.author;  // —
        }
        for (const std::string& l : w_.wrap(head, true, size, textWidth)) {
            k.lines.push_back({l, true, size, 0, kInk});
        }
        std::string meta = dateString(c.date());
        if (c.review() != ReviewState::None && o_.texts.stateName) {
            meta += (meta.empty() ? "" : " \xC2\xB7 ") + o_.texts.stateName(c.review());
        }
        if (!meta.empty()) {
            k.lines.push_back({meta, false, size * 0.9f, 0, kMuted});
        }
        if (!c.contents.empty()) {
            for (const std::string& l : w_.wrap(c.contents, false, size, textWidth)) {
                k.lines.push_back({l, false, size, 0, kInk});
            }
        }
        addReplies(k, c, size, textWidth);
        for (const Line& l : k.lines) {
            k.height += w_.lineHeight(l.size);
        }
        k.height += 2 * kPad;
        return k;
    }

    SummaryResult run(const CommentTree& tree) {
        SummaryResult result;
        const int last = o_.lastPage < 0 ? doc_.pageCount() - 1 : std::min(o_.lastPage, doc_.pageCount() - 1);
        const int first = std::max(0, o_.firstPage);
        std::vector<const Comment*> roots;
        for (const Comment* c : arrange(tree, o_.sort, o_.filter, o_.descending, o_.isUnread)) {
            if (c->ref.page >= first && c->ref.page <= last) {
                roots.push_back(c);
            }
        }
        result.comments = static_cast<int>(roots.size());
        std::map<int, std::vector<const Comment*>> byPage;
        for (const Comment* c : roots) {
            byPage[c->ref.page].push_back(c);
        }
        if (o_.includeEmptyPages && o_.layout != SummaryLayout::CommentsOnly) {
            for (int p = first; p <= last; ++p) {
                byPage[p];
            }
        }
        switch (o_.layout) {
            case SummaryLayout::DocumentAndComments:
                for (const auto& [page, list] : byPage) {
                    pageWithCallouts(page, list);
                }
                break;
            case SummaryLayout::CommentsOnly:
                commentsOnly(roots, byPage);
                break;
            case SummaryLayout::SeparatePages:
                for (const auto& [page, list] : byPage) {
                    pageAlone(page);
                    if (!list.empty()) {
                        std::map<int, std::vector<const Comment*>> one{{page, list}};
                        commentsOnly(list, one, false);
                    }
                }
                break;
        }
        if (empty_) {
            // Ни одного листа — пустой лист с подписью (PDF без страниц не открыть).
            fz_device* dev = w_.begin(kA4W, kA4H);
            w_.text(dev, kMargin, kMargin + o_.fontSize * 2, o_.texts.noComments, false, o_.fontSize, kMuted);
            w_.end();
        }
        result.pdf = w_.finish(&result.pages);
        return result;
    }

private:
    void addReplies(Card& k, const Comment& c, float size, float width) {
        for (const Comment& r : c.replies) {
            const float indent = std::min(3, r.depth) * size * 1.2f;
            std::string head = "\xE2\x86\xB3 " + r.author;  // ↳
            if (const std::string d = dateString(r.date()); !d.empty()) {
                head += " \xC2\xB7 " + d;
            }
            for (const std::string& l : w_.wrap(head, true, size * 0.9f, width - indent)) {
                k.lines.push_back({l, true, size * 0.9f, indent, kInk});
            }
            for (const std::string& l : w_.wrap(r.contents, false, size * 0.9f, width - indent)) {
                k.lines.push_back({l, false, size * 0.9f, indent, kInk});
            }
            addReplies(k, r, size, width);
        }
    }

    void drawCard(fz_device* dev, const Card& k, float x0, float y0, float width) {
        w_.rect(dev, x0, y0, x0 + width, y0 + k.height, kRule, false);
        w_.rect(dev, x0, y0, x0 + 3, y0 + k.height, k.accent, true);
        const float size = o_.fontSize;
        w_.badge(dev, x0 + kPad + size * 0.9f, y0 + kPad + size * 0.75f, k.number, k.accent);
        float y = y0 + kPad;
        const float tx = x0 + kPad + size * 1.8f;
        for (const Line& l : k.lines) {
            const float lh = w_.lineHeight(l.size);
            w_.text(dev, tx + l.indent, y + l.size, l.text, l.bold, l.size, l.color);
            y += lh;
        }
    }

    fz_rect pageBounds(int page) {
        fz_context* ctx = doc_.context().get();
        mu::PageHandle p(ctx);
        fz_rect r{};
        mu::call(ctx, [&] {
            p.reset(fz_load_page(ctx, doc_.handle(), page));
            r = fz_bound_page(ctx, p);
        });
        return r;
    }

    void runPage(fz_device* dev, int page, fz_matrix m) {
        fz_context* ctx = doc_.context().get();
        mu::PageHandle p(ctx);
        mu::call(ctx, [&] {
            p.reset(fz_load_page(ctx, doc_.handle(), page));
            const fz_rect b = fz_bound_page(ctx, p);
            fz_matrix place = fz_concat(fz_translate(-b.x0, -b.y0), m);
            fz_run_page(ctx, p, dev, place, nullptr);
        });
    }

    // Страница слева (60 %), карточки справа, линии-связки; не уместилось — следующий лист с той же
    // страницей.
    void pageWithCallouts(int page, const std::vector<const Comment*>& list) {
        empty_ = false;
        const fz_rect b = pageBounds(page);
        const float pw = b.x1 - b.x0;
        const float ph = b.y1 - b.y0;
        const float sheetW = (pw + 2 * kMargin) / kPageShare;
        const float sheetH = std::max(ph + 2 * kMargin, kA4H * 0.5f);
        const float colX = (pw + 2 * kMargin) + kMargin / 2;
        const float colW = sheetW - colX - kMargin;
        std::vector<Card> cards;
        int number = 1;
        for (const Comment* c : list) {
            cards.push_back(card(*c, number++, colW));
        }
        std::size_t next = 0;
        do {
            fz_device* dev = w_.begin(sheetW, sheetH);
            runPage(dev, page, fz_translate(kMargin, kMargin));
            w_.rect(dev, kMargin, kMargin, kMargin + pw, kMargin + ph, kRule, false, 0.5f);
            const float titleSize = o_.fontSize * 1.3f;
            float y = kMargin;
            if (o_.texts.pageTitle) {
                w_.text(dev, colX, y + titleSize, o_.texts.pageTitle(page + 1), true, titleSize, kInk);
                y += w_.lineHeight(titleSize) + kGap;
            }
            if (cards.empty()) {
                w_.text(dev, colX, y + o_.fontSize, o_.texts.noComments, false, o_.fontSize, kMuted);
            }
            bool placedAny = false;
            while (next < cards.size()) {
                const Card& k = cards[next];
                if (placedAny && y + k.height > sheetH - kMargin) {
                    break;  // на следующий лист
                }
                drawCard(dev, k, colX, y, colW);
                // Связка: от правого края пометки к карточке; номер — у пометки.
                const RectF& r = k.comment->rect;
                const float ax = kMargin + (r.x1 - b.x0);
                const float ay = kMargin + ((r.y0 + r.y1) / 2 - b.y0);
                w_.line(dev, ax, ay, colX, y + kPad + o_.fontSize * 0.75f, k.accent, 0.75f);
                w_.badge(dev, kMargin + (r.x0 - b.x0), kMargin + (r.y0 - b.y0), k.number, k.accent);
                y += k.height + kGap;
                placedAny = true;
                ++next;
            }
            w_.end();
        } while (next < cards.size());
    }

    void pageAlone(int page) {
        empty_ = false;
        const fz_rect b = pageBounds(page);
        fz_device* dev = w_.begin(b.x1 - b.x0, b.y1 - b.y0);
        runPage(dev, page, fz_identity);
        w_.end();
    }

    // Листы A4 с карточками; заголовок сводки — на первом (withTitle), страницы — заголовками групп.
    void commentsOnly(const std::vector<const Comment*>& roots,
                      const std::map<int, std::vector<const Comment*>>& byPage, bool withTitle = true) {
        empty_ = false;
        const float colW = kA4W - 2 * kMargin;
        fz_device* dev = w_.begin(kA4W, kA4H);
        float y = kMargin;
        if (withTitle && !o_.texts.title.empty()) {
            const float s = o_.fontSize * 1.6f;
            w_.text(dev, kMargin, y + s, o_.texts.title, true, s, kInk);
            y += w_.lineHeight(s) + kGap;
        }
        const auto ensure = [&](float h) {
            if (y + h > kA4H - kMargin) {
                w_.end();
                dev = w_.begin(kA4W, kA4H);
                y = kMargin;
            }
        };
        const bool grouped = o_.sort == CommentSort::Page;
        std::vector<std::pair<int, std::vector<const Comment*>>> groups;
        if (grouped) {
            for (const auto& [page, list] : byPage) {
                if (!list.empty()) {
                    groups.emplace_back(page, list);
                }
            }
        } else {
            groups.emplace_back(-1, roots);
        }
        if (roots.empty()) {
            w_.text(dev, kMargin, y + o_.fontSize, o_.texts.noComments, false, o_.fontSize, kMuted);
        }
        int number = 1;
        for (const auto& [page, list] : groups) {
            if (page >= 0 && o_.texts.pageTitle) {
                const float s = o_.fontSize * 1.3f;
                ensure(w_.lineHeight(s) + kGap);
                w_.text(dev, kMargin, y + s, o_.texts.pageTitle(page + 1), true, s, kInk);
                y += w_.lineHeight(s) + kGap / 2;
            }
            for (const Comment* c : list) {
                Card k = card(*c, number++, colW);
                if (page < 0 && o_.texts.pageTitle) {
                    k.lines.insert(k.lines.begin() + 1, Line{o_.texts.pageTitle(c->ref.page + 1), false,
                                                             o_.fontSize * 0.9f, 0, kMuted});
                    k.height += w_.lineHeight(o_.fontSize * 0.9f);
                }
                ensure(k.height);
                drawCard(dev, k, kMargin, y, colW);
                y += k.height + kGap;
            }
        }
        w_.end();
    }

    Document& doc_;
    const SummaryOptions& o_;
    Writer w_;
    bool empty_ = true;
};

}  // namespace

SummaryResult buildSummary(Document& doc, const CommentTree& tree, const SummaryOptions& options) {
    detail::pdfOf(doc);
    Builder builder(doc, options);
    return builder.run(tree);
}

}  // namespace ot::annot
