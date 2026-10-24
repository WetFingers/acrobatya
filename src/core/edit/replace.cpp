#include "core/edit/replace.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/edit/content_model.h"
#include "core/edit/rewriter.h"
#include "core/edit/text_blocks.h"
#include "core/mu/error.h"
#include "core/text/stext.h"
#include "core/util/utf8.h"

namespace ot::edit {

namespace replace_detail {

constexpr float kOriginTolerance = 0.5f;  // точка пера символа слоя поиска и глифа модели, пт
constexpr int kSnippetRadius = 30;        // символов до и после совпадения в предпросмотре

// Глифы модели по точке пера: сетка 1 pt.
class GlyphIndex {
public:
    explicit GlyphIndex(const ContentModel& m) : m_(m) {
        for (std::size_t g = 0; g < m.glyphs().size(); ++g) {
            const PointF o = m.glyphs()[g].origin;
            cells_[key(cell(o.x), cell(o.y))].push_back(static_cast<int>(g));
        }
    }

    // Глиф с первым символом c у точки origin; −1 — нет.
    int at(PointF origin, char32_t c) const {
        int best = -1;
        float bestDistance = kOriginTolerance;
        const int cx = cell(origin.x);
        const int cy = cell(origin.y);
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const auto it = cells_.find(key(cx + dx, cy + dy));
                if (it == cells_.end()) {
                    continue;
                }
                for (int g : it->second) {
                    const Glyph& gl = m_.glyphs()[static_cast<std::size_t>(g)];
                    if (gl.unicode.empty() || gl.unicode.front() != c) {
                        continue;
                    }
                    const float d = std::hypot(gl.origin.x - origin.x, gl.origin.y - origin.y);
                    if (d <= bestDistance) {
                        best = g;
                        bestDistance = d;
                    }
                }
            }
        }
        return best;
    }

private:
    static int cell(float v) { return static_cast<int>(std::floor(v)); }
    static long long key(int x, int y) {
        return (static_cast<long long>(x) << 32) ^ static_cast<unsigned>(y);
    }

    const ContentModel& m_;
    std::unordered_map<long long, std::vector<int>> cells_;
};

// Фрагмент текста абзаца: [range] между \x02 и \x03 (вместо — replacement, если задан), до и после — radius.
std::string snippet(const std::u32string& text, TextRange range, const std::u32string* replacement) {
    const int n = static_cast<int>(text.size());
    const int from = std::max(0, range.begin - kSnippetRadius);
    const int to = std::min(n, range.end + kSnippetRadius);
    std::u32string out;
    if (from > 0) {
        out += char32_t{0x2026};
    }
    out += text.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(range.begin - from));
    out += U'\x02';
    out += replacement != nullptr ? *replacement
                                  : text.substr(static_cast<std::size_t>(range.begin),
                                                static_cast<std::size_t>(range.end - range.begin));
    out += U'\x03';
    out += text.substr(static_cast<std::size_t>(range.end), static_cast<std::size_t>(to - range.end));
    if (to < n) {
        out += char32_t{0x2026};
    }
    return utf8::encode(out);
}

PointF anchorOf(const ContentModel& m, const TextBlock& b) {
    const TextSpan& s = m.spans()[static_cast<std::size_t>(b.spans.front())];
    return m.glyphs()[static_cast<std::size_t>(s.firstGlyph)].origin;
}

// Абзац страницы с первым глифом в точке anchor; −1 — нет.
int blockAt(const ContentModel& m, const std::vector<TextBlock>& blocks, PointF anchor) {
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].spans.empty()) {
            continue;
        }
        const PointF o = anchorOf(m, blocks[i]);
        if (std::fabs(o.x - anchor.x) <= 0.01f && std::fabs(o.y - anchor.y) <= 0.01f) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// Замены одного абзаца: где искать его заново и что заменить.
struct Group {
    int block = -1;
    PointF anchor;
    std::string text;  // TextBlock::text до правки
    std::vector<const ReplaceMatch*> matches;
};

}  // namespace replace_detail

namespace rpd = replace_detail;

const char* replaceSkipCode(ReplaceSkip skip) noexcept {
    switch (skip) {
        case ReplaceSkip::None:
            return "";
        case ReplaceSkip::CrossBlock:
            return "crossBlock";
        case ReplaceSkip::ReadOnly:
            return "readOnly";
        case ReplaceSkip::NoGlyphs:
            return "noGlyphs";
        case ReplaceSkip::Failed:
            return "failed";
    }
    return "?";
}

Replacer::Replacer(Document& doc, Rewriter& rewriter) : doc_(doc), rewriter_(rewriter) {}

std::vector<ReplaceMatch> Replacer::find(int page, std::string_view needle, const SearchOptions& options,
                                         std::string_view replacement) {
    std::vector<ReplaceMatch> out;
    const std::shared_ptr<const TextPage> layer = TextPage::build(doc_, page, SearchSession::textOptions());
    const std::vector<SearchHit> hits = searchPage(*layer, needle, options);
    if (hits.empty()) {
        return out;
    }
    const ContentModel m = ContentModel::build(doc_, page);
    const std::vector<TextBlock> blocks = blocksForPage(m);
    std::vector<int> blockOfSpan(m.spans().size(), -1);
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        for (int si : blocks[b].spans) {
            blockOfSpan[static_cast<std::size_t>(si)] = static_cast<int>(b);
        }
    }
    const rpd::GlyphIndex index(m);
    const std::u32string with = utf8::decode(replacement);
    std::map<int, ParagraphEditor> editors;
    std::map<int, std::unordered_map<int, int>> positions;  // абзац → глиф → позиция в редакторе
    for (const SearchHit& hit : hits) {
        ReplaceMatch r;
        r.hit = hit;
        if (hit.endPage != hit.page) {
            r.skip = ReplaceSkip::CrossBlock;
            out.push_back(std::move(r));
            continue;
        }
        std::vector<int> glyphs;
        for (int c = hit.firstChar;
             c <= hit.lastChar && c >= 0 && c < static_cast<int>(layer->chars().size()); ++c) {
            const TextChar& ch = layer->chars()[static_cast<std::size_t>(c)];
            const int g = index.at(ch.origin, ch.c);
            if (g >= 0) {
                glyphs.push_back(g);
            }
        }
        std::set<int> inBlocks;
        for (int g : glyphs) {
            inBlocks.insert(
                blockOfSpan[static_cast<std::size_t>(m.glyphs()[static_cast<std::size_t>(g)].span)]);
        }
        if (glyphs.empty() || inBlocks.count(-1) > 0) {
            r.skip = ReplaceSkip::NoGlyphs;
            out.push_back(std::move(r));
            continue;
        }
        if (inBlocks.size() > 1) {
            r.skip = ReplaceSkip::CrossBlock;
            out.push_back(std::move(r));
            continue;
        }
        const int b = *inBlocks.begin();
        const TextBlock& block = blocks[static_cast<std::size_t>(b)];
        r.block = b;
        r.anchor = rpd::anchorOf(m, block);
        if (!block.editable()) {
            r.skip = ReplaceSkip::ReadOnly;
            r.readOnly = readOnlyCode(block.reason);
            out.push_back(std::move(r));
            continue;
        }
        auto it = editors.find(b);
        if (it == editors.end()) {
            it = editors.emplace(b, ParagraphEditor::fromBlock(m, block, blocks)).first;
            auto& map = positions[b];
            const auto& chars = it->second.chars();
            for (std::size_t i = 0; i < chars.size(); ++i) {
                if (chars[i].glyph >= 0 && chars[i].group > 0) {
                    map.emplace(chars[i].glyph, static_cast<int>(i));
                }
            }
        }
        const ParagraphEditor& e = it->second;
        const auto& map = positions[b];
        int first = e.size();
        int last = -1;
        for (int g : glyphs) {
            const auto p = map.find(g);
            if (p == map.end()) {
                continue;  // пробел, снятый с края абзаца
            }
            first = std::min(first, p->second);
            const int group = std::max<int>(1, e.chars()[static_cast<std::size_t>(p->second)].group);
            last = std::max(last, p->second + group);
        }
        if (last < 0) {
            r.skip = ReplaceSkip::NoGlyphs;
            out.push_back(std::move(r));
            continue;
        }
        r.range = {first, last};
        const std::u32string text = e.text();
        r.found = utf8::encode(e.text(r.range));
        r.context = rpd::snippet(text, r.range, nullptr);
        r.result = rpd::snippet(text, r.range, &with);
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<ReplaceMatch> Replacer::findAll(std::string_view needle, const SearchOptions& options,
                                            std::string_view replacement, const CancelFn& cancel) {
    std::vector<ReplaceMatch> out;
    const int pages = doc_.pageCount();
    for (int page = 0; page < pages; ++page) {
        if (cancel && cancel()) {
            break;
        }
        std::vector<ReplaceMatch> m = find(page, needle, options, replacement);
        out.insert(out.end(), std::make_move_iterator(m.begin()), std::make_move_iterator(m.end()));
    }
    return out;
}

namespace replace_detail {

// Заменить совпадения групп страницы page внутри уже открытой операции: абзацы снизу вверх по порядку чтения
// (выросший абзац соседей выше не трогает), каждый — заново по первому глифу.
void replaceGroups(Document& doc, Rewriter& rewriter, int page, std::vector<Group>& groups,
                   std::u32string_view with, const std::string& operation, ReplaceReport& report) {
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.block > b.block; });
    for (Group& g : groups) {
        const ContentModel m = ContentModel::build(doc, page);
        const std::vector<TextBlock> blocks = blocksForPage(m);
        const int b = blockAt(m, blocks, g.anchor);
        auto skipAll = [&](const char* why) {
            for (const ReplaceMatch* match : g.matches) {
                ReplaceMatch s = *match;
                s.skip = ReplaceSkip::Failed;
                report.skipped.push_back(std::move(s));
            }
            if (report.message.empty()) {
                report.message = why;
            }
        };
        if (b < 0 || blocks[static_cast<std::size_t>(b)].text != g.text) {
            skipAll("paragraph changed before its replacement");
            continue;
        }
        ParagraphEditor e = ParagraphEditor::fromBlock(m, blocks[static_cast<std::size_t>(b)], blocks);
        std::vector<const ReplaceMatch*> order = g.matches;
        std::sort(order.begin(), order.end(), [](const ReplaceMatch* x, const ReplaceMatch* y) {
            return x->range.begin > y->range.begin;
        });
        for (const ReplaceMatch* match : order) {
            e.replace(match->range, with);
        }
        RewriteOptions options;
        options.operation = operation;
        const RewriteReport r = rewriter.rewrite(e, options);
        if (!r.ok()) {
            skipAll(r.message.c_str());
            if (r.error == RewriteError::Failed) {
                throw MuError(FZ_ERROR_GENERIC, "replace: " + r.message);
            }
            continue;
        }
        report.replaced += static_cast<int>(g.matches.size());
        report.dirty.push_back(r.dirty);
        report.notices.insert(report.notices.end(), r.notices.begin(), r.notices.end());
    }
}

}  // namespace replace_detail

ReplaceReport Replacer::replaceOne(const ReplaceMatch& match, std::string_view replacement,
                                   const std::string& operation) {
    ReplaceReport report;
    report.found = 1;
    if (!match.replaceable()) {
        report.skipped.push_back(match);
        return report;
    }
    std::vector<rpd::Group> groups(1);
    groups[0].block = match.block;
    groups[0].anchor = match.anchor;
    {
        const ContentModel m = ContentModel::build(doc_, match.hit.page);
        const std::vector<TextBlock> blocks = blocksForPage(m);
        const int b = rpd::blockAt(m, blocks, match.anchor);
        groups[0].text = b >= 0 ? blocks[static_cast<std::size_t>(b)].text : std::string();
    }
    groups[0].matches.push_back(&match);
    const std::u32string with = utf8::decode(replacement);
    try {
        Journal::Operation op(doc_, operation);
        rpd::replaceGroups(doc_, rewriter_, match.hit.page, groups, with, operation, report);
        if (report.replaced > 0) {
            op.invalidate(match.hit.page);
            op.markHeavy();
            op.commit();
            report.pages.push_back(match.hit.page);
        }
    } catch (const MuError& err) {
        report.replaced = 0;
        report.message = err.what();
        spdlog::warn("replace: page {}: {}", match.hit.page + 1, err.what());
    }
    return report;
}

ReplaceReport Replacer::replacePage(int page, std::string_view needle, const SearchOptions& options,
                                    std::string_view replacement, const std::string& operation) {
    ReplaceReport report;
    const std::vector<ReplaceMatch> matches = find(page, needle, options, replacement);
    report.found = static_cast<int>(matches.size());
    std::vector<rpd::Group> groups;
    std::map<int, std::size_t> groupOf;
    std::vector<TextBlock> blocks;
    std::unique_ptr<ContentModel> model;
    for (const ReplaceMatch& m : matches) {
        if (!m.replaceable()) {
            report.skipped.push_back(m);
            continue;
        }
        if (!model) {
            model = std::make_unique<ContentModel>(ContentModel::build(doc_, page));
            blocks = blocksForPage(*model);
        }
        auto it = groupOf.find(m.block);
        if (it == groupOf.end()) {
            rpd::Group g;
            g.block = m.block;
            g.anchor = m.anchor;
            g.text = blocks.at(static_cast<std::size_t>(m.block)).text;
            groups.push_back(std::move(g));
            it = groupOf.emplace(m.block, groups.size() - 1).first;
        }
        groups[it->second].matches.push_back(&m);
    }
    if (groups.empty()) {
        return report;
    }
    const std::u32string with = utf8::decode(replacement);
    try {
        Journal::Operation op(doc_, operation);
        rpd::replaceGroups(doc_, rewriter_, page, groups, with, operation, report);
        if (report.replaced > 0) {
            op.invalidate(page);
            op.markHeavy();
            op.commit();
            report.pages.push_back(page);
        }
    } catch (const MuError& err) {
        // Запись абзаца не удалась — откатана вся страница.
        report.replaced = 0;
        report.dirty.clear();
        report.message = err.what();
        spdlog::warn("replace: page {}: {}", page + 1, err.what());
    }
    return report;
}

ReplaceReport Replacer::replaceAll(std::string_view needle, const SearchOptions& options,
                                   std::string_view replacement, const ProgressFn& progress,
                                   const CancelFn& cancel) {
    ReplaceReport report;
    const int pages = doc_.pageCount();
    ReplaceProgress p;
    p.pageCount = pages;
    for (int page = 0; page < pages; ++page) {
        if (cancel && cancel()) {
            break;
        }
        const ReplaceReport r = replacePage(page, needle, options, replacement);
        report.found += r.found;
        report.replaced += r.replaced;
        report.pages.insert(report.pages.end(), r.pages.begin(), r.pages.end());
        report.skipped.insert(report.skipped.end(), r.skipped.begin(), r.skipped.end());
        report.notices.insert(report.notices.end(), r.notices.begin(), r.notices.end());
        if (report.message.empty()) {
            report.message = r.message;
        }
        p.pagesDone = page + 1;
        p.replaced = report.replaced;
        if (progress) {
            progress(p);
        }
    }
    return report;
}

}  // namespace ot::edit
