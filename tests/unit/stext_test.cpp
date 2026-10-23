// unit.stext.* — текстовый слой (эталон 3.2, план 1.4): слова и координаты кириллических фикстур совпадают с
// независимым эталоном PDFium (tests/fixtures/text/stext_expected.json — tools/gen/stext_expected.py): CID
// Identity-H с ToUnicode, Type1 с /Differences, невстроенный TrueType (подмена); порядок чтения и строки как
// у генератора (text.json); попадание в символ, строку и слово; стили (жирный, курсив, цвет, кегль); лигатуры
// сохраняются; TextCache — LRU и сброс правкой в DocumentSession.
#include <gtest/gtest.h>

#include <QFuture>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "core/util/utf8.h"
#include "services/document_session.h"
#include "services/task_context.h"
#include "support/qt_app.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::PointF;
using ot::TextCache;
using ot::TextOptions;
using ot::TextPage;

namespace fs = std::filesystem;

namespace {

fs::path textFixture(const char* name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "text" / name;
}

nlohmann::json readJson(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return nlohmann::json::parse(in);
}

// PDFium раскладывает лигатуры на буквы (U+FB01 → «fi»), MuPDF с PRESERVE_LIGATURES — нет.
std::string expandLigatures(const std::string& text) {
    std::string out;
    for (const char32_t c : ot::utf8::decode(text)) {
        switch (c) {
            case 0xFB00:
                out += "ff";
                break;
            case 0xFB01:
                out += "fi";
                break;
            case 0xFB02:
                out += "fl";
                break;
            case 0xFB03:
                out += "ffi";
                break;
            case 0xFB04:
                out += "ffl";
                break;
            default:
                ot::utf8::append(out, c);
        }
    }
    return out;
}

const ot::TextWord* findWord(const std::vector<ot::TextWord>& words, const std::string& text) {
    for (const auto& w : words) {
        if (w.text == text) {
            return &w;
        }
    }
    return nullptr;
}

int findLine(const TextPage& page, const std::string& text) {
    for (int i = 0; i < static_cast<int>(page.lines().size()); ++i) {
        if (page.lineText(i) == text) {
            return i;
        }
    }
    return -1;
}

// Первый символ fz_stext_page обходом в глубину (SEGMENT кладёт блоки в struct-узлы «Split»).
const fz_stext_char* firstStextChar(fz_stext_block* b) {
    for (; b != nullptr; b = b->next) {
        if (b->type == FZ_STEXT_BLOCK_TEXT && b->u.t.first_line != nullptr) {
            return b->u.t.first_line->first_char;
        }
        if (b->type == FZ_STEXT_BLOCK_STRUCT && b->u.s.down != nullptr) {
            if (const fz_stext_char* c = firstStextChar(b->u.s.down->first_block)) {
                return c;
            }
        }
    }
    return nullptr;
}

PointF center(const ot::RectF& r) { return {(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2}; }

}  // namespace

TEST(stext, words_match_pdfium_reference) {
    OT_FEATURE("view-18");
    OT_FEATURE("comment-02");
    const auto expected = readJson(textFixture("stext_expected.json"));
    ASSERT_FALSE(expected["files"].empty());
    for (const auto& [rel, file] : expected["files"].items()) {
        SCOPED_TRACE(rel);
        auto doc = Document::openOrThrow(ot::test::sourceDir() / fs::path(rel));
        const bool embedded = file["embedded"].get<bool>();
        const auto& pages = file["pages"];
        ASSERT_EQ(doc->pageCount(), static_cast<int>(pages.size()));
        for (int p = 0; p < doc->pageCount(); ++p) {
            SCOPED_TRACE("page " + std::to_string(p));
            const auto text = TextPage::build(*doc, p);
            const auto words = text->words();
            const auto& ref = pages[p]["words"];
            ASSERT_EQ(words.size(), ref.size()) << text->text();
            for (std::size_t i = 0; i < words.size(); ++i) {
                const auto& w = words[i];
                const auto& r = ref[i];
                SCOPED_TRACE(r["text"].get<std::string>());
                EXPECT_EQ(expandLigatures(w.text), r["text"].get<std::string>());
                const float size = r["size"].get<float>();
                const ot::TextChar& first = text->chars()[w.firstChar];
                // Точка пера — из одной и той же матрицы текста у обеих программ.
                EXPECT_NEAR(first.origin.x, r["origin"][0].get<float>(), 0.15f);
                EXPECT_NEAR(first.origin.y, r["origin"][1].get<float>(), 0.15f);
                EXPECT_NEAR(first.size, size, 0.05f);
                if (embedded) {
                    // По y обе рамки — чернила глифов; по x у MuPDF — перо, у PDFium — чернила (боковые
                    // поля).
                    EXPECT_NEAR(w.bbox.y0, r["bbox"][1].get<float>(), 0.6f);
                    EXPECT_NEAR(w.bbox.y1, r["bbox"][3].get<float>(), 0.6f);
                    EXPECT_NEAR(w.bbox.x1, r["bbox"][2].get<float>(), 0.2f * size + 0.3f);
                }
            }
        }
    }
}

TEST(stext, cyrillic_font_kinds_extract_text) {
    OT_FEATURE("view-18");
    struct Case {
        const char* path;
        std::vector<std::string> contains;
    };
    const std::vector<Case> cases = {
        {"tests/data/mini/cyr-cid.pdf", {"Привет, мир", "Съешь же ещё этих мягких французских булок"}},
        {"tests/data/mini/type1-diff.pdf",
         {"Differences: Type1 Helvetica", "Привет, мир — Ёлка, ёж, щука.",
          "Hello, world! WinAnsi: 5 € ± 2 § ©."}},
        {"tests/fixtures/text/truetype-nonembedded.pdf",
         {"Невстроенный TrueType: Arial", "Привет, мир — Ёлка, ёж, щука."}},
    };
    for (const auto& c : cases) {
        SCOPED_TRACE(c.path);
        auto doc = Document::openOrThrow(ot::test::sourceDir() / c.path);
        const auto text = TextPage::build(*doc, 0);
        ASSERT_TRUE(text->hasText());
        const std::string all = text->text();
        for (const auto& s : c.contains) {
            EXPECT_NE(all.find(s), std::string::npos) << s << "\n" << all;
        }
        EXPECT_EQ(all.find("\xEF\xBF\xBD"), std::string::npos) << "U+FFFD in text";  // USE_CID… не включён
        for (const auto& ch : text->chars()) {
            EXPECT_NE(ch.c, 0xFFFDu);
        }
    }
}

TEST(stext, lines_in_reading_order_match_generator) {
    const auto meta = readJson(textFixture("text.json"));
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    for (int p = 0; p < 3; ++p) {
        const auto text = TextPage::build(*doc, p);
        std::vector<std::string> lines;
        for (int i = 0; i < static_cast<int>(text->lines().size()); ++i) {
            lines.push_back(text->lineText(i));
        }
        std::vector<std::string> expected;
        for (const auto& l : meta["files"]["search.pdf"]["lines"]) {
            if (l["page"].get<int>() == p) {
                expected.push_back(l["text"].get<std::string>());
            }
        }
        EXPECT_EQ(lines, expected) << "page " << p;
        // Базовая линия строки — как у генератора.
        for (const auto& l : meta["files"]["search.pdf"]["lines"]) {
            if (l["page"].get<int>() != p) {
                continue;
            }
            const int line = findLine(*text, l["text"].get<std::string>());
            ASSERT_GE(line, 0);
            const auto& first = text->chars()[text->lines()[line].firstChar];
            EXPECT_NEAR(first.origin.y, l["baseline"].get<float>(), 0.05f);
            EXPECT_NEAR(first.origin.x, l["x0"].get<float>(), 0.05f);
        }
    }
    const auto page0 = TextPage::build(*doc, 0);
    const std::string all = page0->text();
    EXPECT_LT(all.find("Оттиск: поиск"), all.find("Слово отти\xCC\x81"
                                                  "ск"));
    EXPECT_LT(all.find("Первая строка"), all.find("Вторая строка"));
    EXPECT_EQ(page0->text(0, 5), "Оттиск");
}

TEST(stext, hit_testing_chars_lines_words) {
    OT_FEATURE("comment-02");
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto text = TextPage::build(*doc, 0);
    const auto words = text->words();
    const ot::TextWord* w = findWord(words, "оттиска.");
    ASSERT_NE(w, nullptr);
    const PointF mid = center(w->bbox);

    const int ch = text->charIndexAt(mid);
    ASSERT_GE(ch, w->firstChar);
    EXPECT_LT(ch, w->firstChar + w->charCount);
    EXPECT_EQ(text->lineText(text->lineAt(mid)), "Без ударения: оттиск, а в родительном падеже — оттиска.");

    const auto word = text->wordAt(mid);
    ASSERT_TRUE(word.has_value());
    EXPECT_EQ(word->text, "оттиска");  // без точки
    EXPECT_EQ(word->firstChar, w->firstChar);
    EXPECT_EQ(word->charCount, w->charCount - 1);

    // Слово с ударением — одно слово вместе с меткой U+0301.
    const ot::TextWord* accented = findWord(words,
                                            "отти\xCC\x81"
                                            "ск");
    ASSERT_NE(accented, nullptr);
    const auto aw = text->wordAt(center(accented->bbox));
    ASSERT_TRUE(aw.has_value());
    EXPECT_EQ(aw->text, accented->text);

    // Вне текста — ничего; рядом с концом строки — ближайший символ в пределах допуска.
    EXPECT_EQ(text->charIndexAt({500, 800}), -1);
    EXPECT_EQ(text->lineAt({500, 800}), -1);
    EXPECT_FALSE(text->wordAt({500, 800}).has_value());
    const int line = text->lineAt(mid);
    const auto& lineBox = text->lines()[line].bbox;
    const PointF right{lineBox.x1 + 2.0f, (lineBox.y0 + lineBox.y1) / 2};
    EXPECT_EQ(text->charIndexAt(right), -1);
    const int near = text->charIndexAt(right, 3.0f);
    ASSERT_GE(near, 0);
    EXPECT_EQ(text->chars()[near].line, line);
    EXPECT_EQ(text->lineAt(right, 3.0f), line);

    // На знаке препинания слова нет.
    const auto& period = text->chars()[w->firstChar + w->charCount - 1];
    ASSERT_EQ(period.c, U'.');
    EXPECT_FALSE(text->wordAt(center(period.quad.bounds())).has_value());
}

TEST(stext, styles_fonts_and_colors) {
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto text = TextPage::build(*doc, 0);
    auto fontOfLine = [&](const std::string& s) -> const ot::TextFont& {
        const int line = findLine(*text, s);
        EXPECT_GE(line, 0) << s;
        return text->fonts().at(text->chars()[text->lines()[line].firstChar].font);
    };
    const auto& bold = fontOfLine("Жирная строка");
    EXPECT_TRUE(bold.bold);
    EXPECT_FALSE(bold.italic);
    EXPECT_EQ(bold.name.find('+'), std::string::npos) << bold.name;  // без префикса подмножества
    EXPECT_NE(bold.name.find("Arial"), std::string::npos) << bold.name;
    const auto& italic = fontOfLine("Курсивная строка");
    EXPECT_TRUE(italic.italic);
    EXPECT_FALSE(italic.bold);
    const auto& regular = fontOfLine("Первая строка для выделения.");
    EXPECT_FALSE(regular.bold);
    EXPECT_FALSE(regular.italic);

    const int boldLine = findLine(*text, "Жирная строка");
    const auto& boldChar = text->chars()[text->lines()[boldLine].firstChar];
    EXPECT_NEAR(boldChar.size, 14.0f, 0.01f);
    EXPECT_TRUE(boldChar.flags & ot::kCharBold);

    const int red = findLine(*text, "Красная строка");
    ASSERT_GE(red, 0);
    EXPECT_EQ(text->chars()[text->lines()[red].firstChar].argb, 0xFFFF0000u);
    EXPECT_EQ(text->chars()[text->lines()[boldLine].firstChar].argb, 0xFF000000u);
}

TEST(stext, ligatures_are_preserved) {
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto text = TextPage::build(*doc, 0);
    int fi = 0;
    for (const auto& ch : text->chars()) {
        fi += ch.c == 0xFB01 ? 1 : 0;
    }
    EXPECT_EQ(fi, 1);
    EXPECT_NE(text->text().find("\xEF\xAC\x81нал"), std::string::npos);
}

TEST(stext, flags_and_page_bounds) {
    const int base = TextPage::flagsFor({});
    EXPECT_EQ(base, FZ_STEXT_PRESERVE_LIGATURES | FZ_STEXT_PRESERVE_WHITESPACE | FZ_STEXT_ACCURATE_BBOXES |
                        FZ_STEXT_PARAGRAPH_BREAK | FZ_STEXT_SEGMENT);
    EXPECT_EQ(base & FZ_STEXT_USE_CID_FOR_UNKNOWN_UNICODE, 0);
    TextOptions all;
    all.collectVectors = all.tableHunt = all.collectStructure = all.dehyphenate = true;
    EXPECT_EQ(TextPage::flagsFor(all), base | FZ_STEXT_COLLECT_VECTORS | FZ_STEXT_TABLE_HUNT |
                                           FZ_STEXT_COLLECT_STRUCTURE | FZ_STEXT_DEHYPHENATE);

    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto text = TextPage::build(*doc, 1, all);
    EXPECT_EQ(text->page(), 1);
    EXPECT_EQ(text->options(), all);
    EXPECT_NEAR(text->bounds().x1, 595.2756f, 0.01f);
    EXPECT_NEAR(text->bounds().y1, 841.8898f, 0.01f);
    for (const auto& b : text->blocks()) {
        if (b.kind == ot::TextBlockKind::Text) {
            EXPECT_EQ(b.sourcePage, 1);
        }
    }

    auto blank = Document::openOrThrow(ot::test::sourceDir() / "tests/data/mini/blank-a4.pdf");
    const auto empty = TextPage::build(*blank, 0);
    EXPECT_FALSE(empty->hasText());
    EXPECT_TRUE(empty->text().empty());
    EXPECT_TRUE(empty->words().empty());
}

TEST(stext, aborted_build_throws) {
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    fz_cookie cookie{};
    cookie.abort = 1;
    try {
        (void)TextPage::build(*doc, 0, {}, &cookie);
        FAIL() << "no exception";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ot::ErrorKind::Aborted);
    }
}

TEST(stext, text_page_outlives_document_and_thread) {
    std::shared_ptr<const TextPage> text;
    {
        auto doc = Document::openOrThrow(textFixture("search.pdf"));
        text = TextPage::build(*doc, 0);
    }
    // Документ закрыт: значения на месте, fz_stext_page читается контекстом семьи в другом потоке.
    std::string fromThread;
    std::thread([&] {
        std::unique_ptr<ot::MuContext> temp;
        fz_context* ctx = text->contextForThisThread(temp);
        fz_buffer* buf = fz_new_buffer_from_stext_page(ctx, text->stext());
        fromThread.assign(reinterpret_cast<const char*>(buf->data), buf->len);
        fz_drop_buffer(ctx, buf);
    }).join();
    EXPECT_NE(fromThread.find("Первая строка"), std::string::npos);
    EXPECT_EQ(text->charIndexOf(firstStextChar(text->stext()->first_block)), 0);
    EXPECT_EQ(text->charIndexOf(nullptr), -1);
}

TEST(stext, text_cache_lru_and_invalidate) {
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    TextCache cache(2);
    const auto p0 = cache.get(*doc, 0);
    EXPECT_EQ(cache.get(*doc, 0), p0);
    EXPECT_EQ(cache.hits(), 1u);
    EXPECT_EQ(cache.misses(), 1u);
    TextOptions vectors;
    vectors.collectVectors = true;
    const auto p0v = cache.get(*doc, 0, vectors);
    EXPECT_NE(p0v, p0);
    EXPECT_EQ(cache.size(), 2u);
    (void)cache.get(*doc, 1);  // вытесняет самый старый — {0, база}
    EXPECT_EQ(cache.size(), 2u);
    EXPECT_EQ(cache.find(0), nullptr);
    EXPECT_EQ(cache.find(0, vectors), p0v);
    cache.invalidate(0);
    EXPECT_EQ(cache.find(0, vectors), nullptr);
    EXPECT_NE(cache.find(1), nullptr);
    cache.clear();
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_EQ(TextCache().capacity(), 16u);
}

TEST(stext, session_text_cache_reset_by_page_edit) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path file = dir.path() / L"поиск.pdf";
    fs::copy_file(textFixture("search.pdf"), file);
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(file.wstring())));

    const auto a = session.textPage(0).result();
    ASSERT_TRUE(a);
    EXPECT_TRUE(a->hasText());
    EXPECT_EQ(session.textPage(0).result(), a);  // из кеша
    const auto other = session.textPage(1).result();
    EXPECT_NE(other, a);

    session
        .edit(QStringLiteral("Add note"),
              [](ot::TaskContext& tc) {
                  ot::test::addNote(tc.document(), 0, "note");
                  tc.touchPage(0);
              })
        .waitForFinished();
    const auto b = session.textPage(0).result();
    EXPECT_NE(b, a);                                 // страница 0 сброшена правкой
    EXPECT_EQ(session.textPage(1).result(), other);  // страница 1 — нет
    // Из задачи — тот же кеш.
    const auto c = session.postSync([](ot::TaskContext& tc) { return tc.textPage(0); });
    EXPECT_EQ(c, b);
    session.close();
}
