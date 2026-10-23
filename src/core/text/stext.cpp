#include "core/text/stext.h"

#include <algorithm>
#include <cstring>
#include <string_view>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/utf8.h"

extern "C" {
#include <mupdf/ucdn.h>
}

namespace ot {

namespace {

constexpr int kBaseFlags =
    FZ_STEXT_PRESERVE_LIGATURES | FZ_STEXT_PRESERVE_WHITESPACE | FZ_STEXT_ACCURATE_BBOXES;
constexpr int kReadingOrderFlags = FZ_STEXT_PARAGRAPH_BREAK | FZ_STEXT_SEGMENT;

RectF toRect(const fz_rect& r) { return {r.x0, r.y0, r.x1, r.y1}; }
PointF toPoint(const fz_point& p) { return {p.x, p.y}; }
QuadF toQuad(const fz_quad& q) { return {toPoint(q.ul), toPoint(q.ur), toPoint(q.ll), toPoint(q.lr)}; }

// Страница документа, откуда блок: fz_stext_page_details по id блока (без id — −1; поиск бросает за пределами
// списка, поэтому длина проверяется заранее).
int sourcePageOf(fz_context* ctx, fz_stext_page* page, fz_stext_block* block) {
    if (page->id_list == nullptr || block->id < 0 ||
        static_cast<std::size_t>(block->id) >= fz_pool_array_len(ctx, page->id_list)) {
        return -1;
    }
    fz_stext_page_details* details = nullptr;
    mu::call(ctx, [&] { details = fz_stext_page_details_for_block(ctx, page, block); });
    return details != nullptr ? details->page : -1;
}

bool isSpace(char32_t c) { return c == U' ' || fz_is_unicode_whitespace(static_cast<int>(c)) != 0; }

bool containsWord(std::string_view name, std::initializer_list<std::string_view> words) {
    return std::any_of(words.begin(), words.end(),
                       [&](std::string_view w) { return name.find(w) != std::string_view::npos; });
}

}  // namespace

int TextPage::flagsFor(const Options& options) noexcept {
    int flags = kBaseFlags;
    if (options.readingOrder) {
        flags |= kReadingOrderFlags;
    }
    if (options.collectVectors) {
        flags |= FZ_STEXT_COLLECT_VECTORS;
    }
    if (options.tableHunt) {
        flags |= FZ_STEXT_TABLE_HUNT;
    }
    if (options.collectStructure) {
        flags |= FZ_STEXT_COLLECT_STRUCTURE;
    }
    if (options.dehyphenate) {
        flags |= FZ_STEXT_DEHYPHENATE;
    }
    return flags;
}

bool TextPage::isWordChar(char32_t c) noexcept {
    if (c == U'_') {
        return true;
    }
    switch (ucdn_get_general_category(static_cast<uint32_t>(c))) {
        case UCDN_GENERAL_CATEGORY_LL:
        case UCDN_GENERAL_CATEGORY_LM:
        case UCDN_GENERAL_CATEGORY_LO:
        case UCDN_GENERAL_CATEGORY_LT:
        case UCDN_GENERAL_CATEGORY_LU:
        case UCDN_GENERAL_CATEGORY_MC:
        case UCDN_GENERAL_CATEGORY_ME:
        case UCDN_GENERAL_CATEGORY_MN:
        case UCDN_GENERAL_CATEGORY_ND:
        case UCDN_GENERAL_CATEGORY_NL:
        case UCDN_GENERAL_CATEGORY_NO:
            return true;
        default:
            return false;
    }
}

std::shared_ptr<const TextPage> TextPage::build(const Document& doc, int page, const Options& options,
                                                fz_cookie* cookie) {
    fz_context* ctx = doc.context().get();
    mu::PageHandle p(ctx);
    mu::StextPageHandle text(ctx);
    mu::DeviceHandle dev(ctx);
    fz_stext_options opts{};
    fz_rect bounds{};
    const int flags = flagsFor(options);
    // Как fz_new_stext_page_from_page (содержимое страницы без пометок), но с cookie и номером страницы в
    // fz_stext_page_details (fz_new_stext_device_for_page).
    mu::call(ctx, [&] {
        fz_init_stext_options(ctx, &opts);
        opts.flags = flags;
        p.reset(fz_load_page(ctx, doc.handle(), page));
        bounds = fz_bound_page(ctx, p);
        text.reset(fz_new_stext_page(ctx, bounds));
        dev.reset(fz_new_stext_device_for_page(ctx, text, &opts, 0, page, bounds));
        fz_run_page_contents(ctx, p, dev, fz_identity, cookie);
        fz_close_device(ctx, dev);
    });
    if (cookie != nullptr && cookie->abort != 0) {
        throw MuError(FZ_ERROR_ABORT, "text extraction aborted");
    }
    dev.reset();
    p.reset();
    auto owner = doc.context().clone();
    std::shared_ptr<TextPage> result(
        new TextPage(std::move(owner), text.release(), page, options, toRect(bounds)));
    result->collect(ctx);
    return result;
}

TextPage::TextPage(std::unique_ptr<MuContext> owner, fz_stext_page* stext, int page, const Options& options,
                   const RectF& bounds)
    : owner_(std::move(owner)),
      family_(owner_->family()),
      stext_(stext),
      page_(page),
      options_(options),
      bounds_(bounds) {}

TextPage::~TextPage() {
    if (stext_ != nullptr && owner_) {
        // Последняя ссылка умирает в каком-то одном потоке: свой клон контекста — только здесь.
        fz_drop_stext_page(owner_->get(), stext_);
    }
}

fz_context* TextPage::contextForThisThread(std::unique_ptr<MuContext>& temporary) const {
    MuContext* local = &MuContext::forThisThread();
    if (local->family() != family_) {
        temporary = owner_->clone();
        local = temporary.get();
    }
    return local->get();
}

int TextPage::charIndexOf(const fz_stext_char* ch) const {
    // Карта строится при первом вопросе (поиск спрашивает только у страниц с совпадениями): обход в том же
    // порядке, что collect.
    std::call_once(charIndexOnce_, [this] {
        charIndex_.reserve(chars_.size());
        int index = 0;
        indexChars(stext_->first_block, index);
    });
    const auto it = charIndex_.find(ch);
    return it == charIndex_.end() ? -1 : it->second;
}

void TextPage::indexChars(fz_stext_block* first, int& index) const {
    for (fz_stext_block* b = first; b != nullptr; b = b->next) {
        if (b->type == FZ_STEXT_BLOCK_TEXT) {
            for (fz_stext_line* l = b->u.t.first_line; l != nullptr; l = l->next) {
                for (fz_stext_char* c = l->first_char; c != nullptr; c = c->next) {
                    charIndex_.emplace(c, index++);
                }
            }
        } else if (b->type == FZ_STEXT_BLOCK_STRUCT && b->u.s.down != nullptr) {
            indexChars(b->u.s.down->first_block, index);
        }
    }
}

int TextPage::fontIndex(fz_context* ctx, fz_font* font) {
    if (font == nullptr) {
        return -1;
    }
    if (const auto it = fontIndex_.find(font); it != fontIndex_.end()) {
        return it->second;
    }
    TextFont info;
    const char* name = fz_font_name(ctx, font);
    std::string_view n = name != nullptr ? name : "";
    if (n.size() > 7 && n[6] == '+') {  // префикс подмножества «ABCDEF+»
        n.remove_prefix(7);
    }
    info.name.assign(n);
    info.bold =
        fz_font_is_bold(ctx, font) != 0 || containsWord(n, {"Bold", "Black", "Heavy", "Semibold", "Demi"});
    info.italic = fz_font_is_italic(ctx, font) != 0 || containsWord(n, {"Italic", "Oblique"});
    info.serif = fz_font_is_serif(ctx, font) != 0;
    info.monospaced = fz_font_is_monospaced(ctx, font) != 0;
    const int index = static_cast<int>(fonts_.size());
    fonts_.push_back(std::move(info));
    fontIndex_.emplace(font, index);
    return index;
}

void TextPage::collect(fz_context* ctx) { collectBlocks(ctx, stext_->first_block, -1); }

void TextPage::collectBlocks(fz_context* ctx, fz_stext_block* first, int structNode) {
    for (fz_stext_block* b = first; b != nullptr; b = b->next) {
        TextBlock block;
        block.bbox = toRect(b->bbox);
        block.structNode = structNode;
        block.sourcePage = sourcePageOf(ctx, stext_, b);
        switch (b->type) {
            case FZ_STEXT_BLOCK_TEXT: {
                block.kind = TextBlockKind::Text;
                block.firstLine = static_cast<int>(lines_.size());
                const int blockIndex = static_cast<int>(blocks_.size());
                for (fz_stext_line* l = b->u.t.first_line; l != nullptr; l = l->next) {
                    TextLine line;
                    line.bbox = toRect(l->bbox);
                    line.dir = toPoint(l->dir);
                    line.vertical = l->wmode != 0;
                    line.block = blockIndex;
                    line.firstChar = static_cast<int>(chars_.size());
                    const int lineIndex = static_cast<int>(lines_.size());
                    for (fz_stext_char* c = l->first_char; c != nullptr; c = c->next) {
                        TextChar ch;
                        ch.c = static_cast<char32_t>(c->c);
                        ch.quad = toQuad(c->quad);
                        ch.origin = toPoint(c->origin);
                        ch.size = c->size;
                        ch.argb = c->argb;
                        ch.flags = c->flags;
                        ch.font = fontIndex(ctx, c->font);
                        ch.line = lineIndex;
                        chars_.push_back(ch);
                    }
                    line.charCount = static_cast<int>(chars_.size()) - line.firstChar;
                    lines_.push_back(line);
                }
                block.lineCount = static_cast<int>(lines_.size()) - block.firstLine;
                blocks_.push_back(block);
                break;
            }
            case FZ_STEXT_BLOCK_IMAGE:
                block.kind = TextBlockKind::Image;
                blocks_.push_back(block);
                break;
            case FZ_STEXT_BLOCK_VECTOR: {
                TextVector v;
                v.bbox = block.bbox;
                v.argb = b->u.v.argb;
                const uint32_t f = b->u.v.flags;
                v.stroked = (f & FZ_STEXT_VECTOR_IS_STROKED) != 0;
                v.rectangle = (f & FZ_STEXT_VECTOR_IS_RECTANGLE) != 0;
                v.continues = (f & FZ_STEXT_VECTOR_CONTINUES) != 0;
                v.underline = (f & FZ_STEXT_VECTOR_IS_UNDERLINE) != 0;
                v.strikeout = (f & FZ_STEXT_VECTOR_IS_STRIKEOUT) != 0;
                v.highlight = (f & FZ_STEXT_VECTOR_IS_HIGHLIGHT) != 0;
                block.kind = TextBlockKind::Vector;
                block.vector = static_cast<int>(vectors_.size());
                vectors_.push_back(v);
                blocks_.push_back(block);
                break;
            }
            case FZ_STEXT_BLOCK_GRID:
                block.kind = TextBlockKind::Grid;
                blocks_.push_back(block);
                break;
            case FZ_STEXT_BLOCK_STRUCT: {
                const fz_stext_struct* down = b->u.s.down;
                if (down == nullptr) {  // узел известен, но содержимого на странице нет
                    break;
                }
                TextStructNode node;
                const char* type = fz_structure_to_string(down->standard);
                node.type = type != nullptr ? type : "";
                node.raw = down->raw;
                node.parent = structNode;
                node.index = b->u.s.index;
                const int nodeIndex = static_cast<int>(structure_.size());
                structure_.push_back(std::move(node));
                if (structNode >= 0) {
                    structure_[structNode].children.push_back(nodeIndex);
                }
                collectBlocks(ctx, down->first_block, nodeIndex);
                break;
            }
            default:
                break;
        }
    }
}

std::string TextPage::lineText(int line) const {
    std::string out;
    if (line < 0 || line >= static_cast<int>(lines_.size())) {
        return out;
    }
    const TextLine& l = lines_[line];
    for (int i = l.firstChar; i < l.firstChar + l.charCount; ++i) {
        utf8::append(out, chars_[i].c);
    }
    return out;
}

std::string TextPage::text() const {
    std::string out;
    bool firstBlock = true;
    for (const TextBlock& b : blocks_) {
        if (b.kind != TextBlockKind::Text || b.lineCount == 0) {
            continue;
        }
        if (!firstBlock) {
            out += '\n';
        }
        firstBlock = false;
        for (int l = b.firstLine; l < b.firstLine + b.lineCount; ++l) {
            out += lineText(l);
            out += '\n';
        }
    }
    return out;
}

std::string TextPage::text(int first, int last) const {
    std::string out;
    const int n = static_cast<int>(chars_.size());
    first = std::max(first, 0);
    last = std::min(last, n - 1);
    for (int i = first; i <= last; ++i) {
        if (i > first && chars_[i].line != chars_[i - 1].line) {
            out += '\n';
        }
        utf8::append(out, chars_[i].c);
    }
    return out;
}

std::vector<TextWord> TextPage::words() const {
    std::vector<TextWord> words;
    for (const TextLine& l : lines_) {
        TextWord cur;
        bool open = false;
        auto flush = [&] {
            if (open) {
                words.push_back(std::move(cur));
                cur = TextWord{};
                open = false;
            }
        };
        for (int i = l.firstChar; i < l.firstChar + l.charCount; ++i) {
            const TextChar& ch = chars_[i];
            if (isSpace(ch.c)) {
                flush();
                continue;
            }
            if (!open) {
                cur.firstChar = i;
                cur.bbox = RectF::none();
                open = true;
            }
            ++cur.charCount;
            cur.bbox = rectUnion(cur.bbox, ch.quad.bounds());
            utf8::append(cur.text, ch.c);
        }
        flush();
    }
    return words;
}

int TextPage::charIndexAt(PointF p, float tolerance) const {
    int best = -1;
    float bestDistance = tolerance;
    for (int i = 0; i < static_cast<int>(chars_.size()); ++i) {
        const TextChar& ch = chars_[i];
        if (ch.quad.contains(p)) {
            return i;
        }
        if (tolerance > 0) {
            const float d = rectDistance(ch.quad.bounds(), p);
            if (d <= bestDistance && (best < 0 || d < bestDistance)) {
                best = i;
                bestDistance = d;
            }
        }
    }
    return best;
}

int TextPage::lineAt(PointF p, float tolerance) const {
    int best = -1;
    float bestDistance = tolerance;
    for (int i = 0; i < static_cast<int>(lines_.size()); ++i) {
        const float d = rectDistance(lines_[i].bbox, p);
        if (d == 0) {
            return i;
        }
        if (d <= bestDistance && (best < 0 || d < bestDistance)) {
            best = i;
            bestDistance = d;
        }
    }
    return best;
}

std::pair<int, int> TextPage::wordBounds(int index) const {
    if (index < 0 || index >= static_cast<int>(chars_.size()) || !isWordChar(chars_[index].c)) {
        return {index, index};
    }
    const TextLine& l = lines_[chars_[index].line];
    int first = index;
    while (first > l.firstChar && isWordChar(chars_[first - 1].c)) {
        --first;
    }
    int last = index;
    while (last + 1 < l.firstChar + l.charCount && isWordChar(chars_[last + 1].c)) {
        ++last;
    }
    return {first, last};
}

std::optional<TextWord> TextPage::wordAt(PointF p, float tolerance) const {
    const int index = charIndexAt(p, tolerance);
    if (index < 0 || !isWordChar(chars_[index].c)) {
        return std::nullopt;
    }
    const auto [first, last] = wordBounds(index);
    TextWord word;
    word.firstChar = first;
    word.charCount = last - first + 1;
    word.bbox = RectF::none();
    for (int i = first; i <= last; ++i) {
        word.bbox = rectUnion(word.bbox, chars_[i].quad.bounds());
        utf8::append(word.text, chars_[i].c);
    }
    return word;
}

// ---------------------------------------------------------------- TextCache

std::shared_ptr<const TextPage> TextCache::get(const Document& doc, int page, const TextOptions& options,
                                               fz_cookie* cookie) {
    const Key key{page, TextPage::flagsFor(options)};
    if (const auto it = index_.find(key); it != index_.end()) {
        order_.splice(order_.begin(), order_, it->second);
        ++hits_;
        return it->second->second;
    }
    ++misses_;
    auto text = TextPage::build(doc, page, options, cookie);
    order_.emplace_front(key, text);
    index_[key] = order_.begin();
    trim();
    return text;
}

std::shared_ptr<const TextPage> TextCache::find(int page, const TextOptions& options) const {
    const auto it = index_.find(Key{page, TextPage::flagsFor(options)});
    return it == index_.end() ? nullptr : it->second->second;
}

void TextCache::invalidate(int page) {
    for (auto it = index_.lower_bound(Key{page, 0}); it != index_.end() && it->first.first == page;) {
        order_.erase(it->second);
        it = index_.erase(it);
    }
}

void TextCache::clear() {
    index_.clear();
    order_.clear();
}

void TextCache::setCapacity(std::size_t capacity) {
    capacity_ = capacity == 0 ? 1 : capacity;
    trim();
}

void TextCache::trim() {
    while (order_.size() > capacity_) {
        index_.erase(order_.back().first);
        order_.pop_back();
    }
}

}  // namespace ot
