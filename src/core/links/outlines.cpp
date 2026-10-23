#include "core/links/outlines.h"

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot::outline {

namespace {

using IteratorHandle = mu::Handle<fz_outline_iterator, fz_drop_outline_iterator>;

// Поля закладки, прочитанные внутри mu::call: строки живут в итераторе до следующего хода.
struct RawItem {
    const char* title = nullptr;
    const char* uri = nullptr;
    int isOpen = 0;
    int flags = 0;
    float r = 0;
    float g = 0;
    float b = 0;
};

// Итератор PDF кладёт в flags сырой /F (бит 1 — курсив, бит 2 — жирный, ISO 32000 12.3.3), а в r, g, b —
// 0–255 (pdf-outline.c); итератор из fz_outline — те же 8 бит.
constexpr int kItalicFlag = 1;
constexpr int kBoldFlag = 2;

std::uint8_t outlineChannel(float v) {
    if (v <= 0) {
        return 0;
    }
    return v >= 255 ? std::uint8_t{255} : static_cast<std::uint8_t>(v + 0.5f);
}

}  // namespace

bool hasOutline(Document& doc) {
    fz_context* ctx = doc.context().get();
    IteratorHandle it(ctx);
    int has = 0;
    mu::call(ctx, [&] {
        it.reset(fz_new_outline_iterator(ctx, doc.handle()));  // NULL — формат без закладок (картинки)
        has = it && fz_outline_iterator_item(ctx, it) != nullptr ? 1 : 0;
    });
    return has != 0;
}

std::vector<Item> children(Document& doc, const std::vector<int>& path) {
    fz_context* ctx = doc.context().get();
    IteratorHandle it(ctx);
    int at = 0;
    mu::call(ctx, [&] {
        it.reset(fz_new_outline_iterator(ctx, doc.handle()));
        at = it && fz_outline_iterator_item(ctx, it) != nullptr ? FZ_OUTLINE_ITERATOR_AT_ITEM
                                                                : FZ_OUTLINE_ITERATOR_AT_EMPTY;
    });
    // Спуск по пути: на уровне — index шагов вперёд, затем к первому ребёнку.
    for (const int index : path) {
        if (at != FZ_OUTLINE_ITERATOR_AT_ITEM || index < 0) {
            return {};
        }
        for (int k = 0; k < index && at == FZ_OUTLINE_ITERATOR_AT_ITEM; ++k) {
            mu::call(ctx, [&] { at = fz_outline_iterator_next(ctx, it); });
        }
        if (at != FZ_OUTLINE_ITERATOR_AT_ITEM) {
            return {};
        }
        mu::call(ctx, [&] { at = fz_outline_iterator_down(ctx, it); });
    }
    std::vector<Item> out;
    for (int pos = 0; at == FZ_OUTLINE_ITERATOR_AT_ITEM; ++pos) {
        RawItem raw;
        bool present = false;
        mu::call(ctx, [&] {
            const fz_outline_item* item = fz_outline_iterator_item(ctx, it);
            if (item != nullptr) {
                present = true;
                raw.title = item->title;
                raw.uri = item->uri;
                raw.isOpen = item->is_open;
                raw.flags = item->flags;
                raw.r = item->r;
                raw.g = item->g;
                raw.b = item->b;
            }
        });
        if (!present) {
            break;
        }
        Item item;
        item.title = raw.title != nullptr ? std::string(raw.title) : std::string();
        const std::string uri = raw.uri != nullptr ? std::string(raw.uri) : std::string();
        item.open = raw.isOpen != 0;
        item.italic = (raw.flags & kItalicFlag) != 0;
        item.bold = (raw.flags & kBoldFlag) != 0;
        const std::array<std::uint8_t, 3> rgb{outlineChannel(raw.r), outlineChannel(raw.g),
                                              outlineChannel(raw.b)};
        if (rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0) {
            item.color = rgb;
        }
        // Есть ли дети: шаг вниз и назад (down без детей встаёт «под» узлом, up возвращает).
        int down = FZ_OUTLINE_ITERATOR_DID_NOT_MOVE;
        mu::call(ctx, [&] {
            down = fz_outline_iterator_down(ctx, it);
            if (down != FZ_OUTLINE_ITERATOR_DID_NOT_MOVE) {
                fz_outline_iterator_up(ctx, it);
            }
        });
        item.hasChildren = down == FZ_OUTLINE_ITERATOR_AT_ITEM;
        item.path = path;
        item.path.push_back(pos);
        item.action = links::actionFromUri(doc, uri);
        out.push_back(std::move(item));
        mu::call(ctx, [&] { at = fz_outline_iterator_next(ctx, it); });
    }
    return out;
}

}  // namespace ot::outline
