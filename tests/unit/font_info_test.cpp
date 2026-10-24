// unit.font_info.* — инспекция встроенных шрифтов (план 3.2, п. 1; эталон 3.9 «FontResolver»): тип,
// подмножество, кодировка, ToUnicode, наличие глифа для символа (код в кодировке шрифта и непустой контур),
// метрики, /Flags, /ItalicAngle, /StemV и стиль — на 10 фикстурах: tests/fixtures/edit/fonts/fonts.pdf
// (девять шрифтов с ожиданиями генератора в fonts.json) и девять фикстур редактора 3.1 (Type0 и простые
// TrueType Word, base14, Type1 с /Differences, Symbol). Каждый глиф модели страницы обратим: encode(Unicode)
// даёт код с тем же Unicode и шириной.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/edit/content_model.h"
#include "core/edit/font_info.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::edit::ContentModel;
using ot::edit::FontDetails;
using ot::edit::FontProbe;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

const json& fontsMeta() {
    static const json meta = json::parse(ot::paths::readFile(editFixture("fonts/fonts.json")));
    return meta;
}

char32_t firstChar(const std::string& s) {
    const std::u32string u = ot::utf8::decode(s);
    return u.empty() ? U'\0' : u.front();
}

std::string probeFile(const char* name) { return std::string("fixture ") + name; }

// Десять фикстур пункта: девять шрифтов fonts.pdf и фикстуры 3.1 с другими устройствами шрифтов.
const char* const kFixtures[] = {
    "fonts/fonts.pdf", "latin.pdf",      "cyr-cid.pdf",         "type1-diff.pdf", "base14.pdf",
    "tz-tc-tw.pdf",    "tj-kerning.pdf", "word-paragraphs.pdf", "word-lists.pdf", "align-justify.pdf"};

}  // namespace

// Поля fonts.json против FontDetails и наличия глифов.
TEST(font_info, fixture_fonts) {
    auto doc = Document::openOrThrow(editFixture("fonts/fonts.pdf"));
    const json& expect = fontsMeta()["fonts.pdf"];
    ASSERT_EQ(expect.size(), 9u);
    for (const auto& [key, e] : expect.items()) {
        SCOPED_TRACE(key);
        std::unique_ptr<FontProbe> probe = FontProbe::forPage(*doc, 0, key);
        ASSERT_NE(probe, nullptr);
        const FontDetails& d = probe->details();
        EXPECT_EQ(d.resource, key);
        auto str = [&](const char* field, const std::string& actual) {
            if (e.contains(field)) {
                EXPECT_EQ(actual, e[field].get<std::string>()) << field;
            }
        };
        auto flag = [&](const char* field, bool actual) {
            if (e.contains(field)) {
                EXPECT_EQ(actual, e[field].get<bool>()) << field;
            }
        };
        auto num = [&](const char* field, double actual, double tol) {
            if (e.contains(field)) {
                EXPECT_NEAR(actual, e[field].get<double>(), tol) << field;
            }
        };
        str("baseFont", d.baseFont);
        str("name", d.name);
        str("family", d.family);
        str("subtype", d.subtype);
        str("cidSubtype", d.cidSubtype);
        str("encoding", d.encoding);
        str("file", ot::edit::fontFileCode(d.file));
        flag("embedded", d.embedded);
        flag("subset", d.subset);
        flag("base14", d.base14);
        flag("toUnicode", d.toUnicode);
        flag("type3", d.type3);
        flag("bold", d.bold);
        flag("italic", d.italic);
        flag("serif", d.serif);
        flag("fixedPitch", d.fixedPitch);
        num("flags", d.flags, 0);
        num("italicAngle", d.italicAngle, 0.01);
        num("stemV", d.stemV, 0.01);
        num("weight", d.weight, 0);
        if (e.contains("has")) {
            for (const auto& c : e["has"]) {
                const char32_t u = firstChar(c.get<std::string>());
                EXPECT_TRUE(probe->hasGlyph(u)) << "нет глифа " << c.get<std::string>();
            }
        }
        if (e.contains("missing")) {
            for (const auto& c : e["missing"]) {
                const char32_t u = firstChar(c.get<std::string>());
                EXPECT_FALSE(probe->hasGlyph(u)) << "лишний глиф " << c.get<std::string>();
            }
        }
        if (d.embedded && !d.type3) {
            EXPECT_GT(d.glyphCount, 0);
            EXPECT_GT(d.ascent, 0.f);
            EXPECT_LT(d.descent, 0.f);
        }
    }
}

// Фикстуры 3.1: что инспекция говорит о шрифтах, которые там знаем наверняка.
TEST(font_info, edit_fixtures) {
    {
        auto doc = Document::openOrThrow(editFixture("latin.pdf"));
        auto f = FontProbe::forPage(*doc, 0, "F1");
        ASSERT_NE(f, nullptr);
        const FontDetails& d = f->details();
        EXPECT_EQ(d.subtype, "Type0");
        EXPECT_EQ(d.cidSubtype, "CIDFontType2");
        EXPECT_EQ(d.encoding, "Identity-H");
        EXPECT_EQ(d.name, "ArialMT");
        EXPECT_EQ(d.family, "Arial");
        EXPECT_TRUE(d.subset);
        EXPECT_TRUE(d.embedded);
        EXPECT_TRUE(d.toUnicode);
        EXPECT_EQ(d.file, ot::edit::FontFile::TrueType);
        EXPECT_TRUE(f->hasGlyph(U'e'));
        EXPECT_FALSE(f->hasGlyph(U'ё'));  // латинское подмножество Arial без «ё» (тест плана)
        EXPECT_FALSE(f->hasGlyph(U'€'));
        const auto e = f->encode(U'e');
        ASSERT_TRUE(e.has_value());
        EXPECT_EQ(e->bytes.size(), 2u);
        EXPECT_GT(e->gid, 0);
    }
    {
        auto doc = Document::openOrThrow(editFixture("cyr-cid.pdf"));
        auto f = FontProbe::forPage(*doc, 0, "F1");
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->details().family, "Times New Roman");
        EXPECT_TRUE(f->details().serif);
        EXPECT_TRUE(f->hasGlyph(U'ё'));
        EXPECT_TRUE(f->hasGlyph(U'ж'));
        EXPECT_FALSE(f->hasGlyph(U'Q'));  // в кириллическом подмножестве латиницы нет
    }
    {
        auto doc = Document::openOrThrow(editFixture("type1-diff.pdf"));
        auto f = FontProbe::forPage(*doc, 0, "F1");
        ASSERT_NE(f, nullptr);
        const FontDetails& d = f->details();
        EXPECT_EQ(d.name, "Helvetica");
        EXPECT_EQ(d.family, "Helvetica");
        EXPECT_TRUE(d.base14);
        EXPECT_FALSE(d.embedded);
        EXPECT_TRUE(d.differences);
        EXPECT_EQ(d.encoding, "WinAnsiEncoding+Differences");
        const auto p = f->encode(U'П');  // кириллица на кодах cp1251 через /Differences
        ASSERT_TRUE(p.has_value());
        EXPECT_EQ(p->bytes, std::string("\xCF"));
    }
    {
        auto doc = Document::openOrThrow(editFixture("base14.pdf"));
        auto f = FontProbe::forPage(*doc, 0, "F1");
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->details().family, "Times");
        EXPECT_TRUE(f->details().base14);
        EXPECT_TRUE(f->details().serif);
        EXPECT_EQ(f->details().weight, 400);
        EXPECT_TRUE(f->hasGlyph(U'€'));  // WinAnsi 0x80, глиф — у запасного шрифта MuPDF
    }
    {
        auto doc = Document::openOrThrow(editFixture("word-paragraphs.pdf"));
        auto f = FontProbe::forPage(*doc, 0, "F2");
        ASSERT_NE(f, nullptr);
        const FontDetails& d = f->details();
        EXPECT_EQ(d.subtype, "TrueType");
        EXPECT_EQ(d.encoding, "WinAnsiEncoding");
        EXPECT_TRUE(d.subset);
        EXPECT_FALSE(d.toUnicode);
        EXPECT_EQ(d.stemV, 44.f);
        EXPECT_FALSE(f->hasGlyph(U'Ж'));  // WinAnsi без кириллицы
    }
    {
        auto doc = Document::openOrThrow(editFixture("word-lists.pdf"));
        auto f = FontProbe::forPage(*doc, 0, "F3");
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->details().name, "SymbolMT");
        EXPECT_EQ(f->details().family, "Symbol");
    }
}

// Каждый глиф модели страницы обратим: encode(Unicode) → код того же Unicode и той же ширины.
TEST(font_info, encode_round_trip) {
    for (const char* name : kFixtures) {
        SCOPED_TRACE(probeFile(name));
        auto doc = Document::openOrThrow(editFixture(name));
        const std::vector<FontDetails> fonts = ot::edit::inspectPageFonts(*doc, 0);
        EXPECT_FALSE(fonts.empty());
        const ContentModel model = ContentModel::build(*doc, 0);
        std::vector<std::unique_ptr<FontProbe>> probes(model.fonts().size());
        int checked = 0;
        for (const auto& g : model.glyphs()) {
            const auto& span = model.spans()[static_cast<size_t>(g.span)];
            if (span.form || span.font < 0 || g.unicode.size() != 1 || g.unicode[0] == 0xFFFD ||
                g.unicode[0] == U' ') {
                continue;
            }
            const auto& info = model.fonts()[static_cast<size_t>(span.font)];
            if (info.type3 || info.substitute || info.resource.rfind("gs:", 0) == 0) {
                continue;
            }
            auto& probe = probes[static_cast<size_t>(span.font)];
            if (!probe) {
                probe = FontProbe::forPage(*doc, 0, info.resource);
                ASSERT_NE(probe, nullptr) << info.resource;
            }
            const auto enc = probe->encode(g.unicode[0]);
            ASSERT_TRUE(enc.has_value())
                << info.resource << " U+" << std::hex << static_cast<int>(g.unicode[0]);
            std::vector<ot::edit::GlyphMap::Code> codes;
            probe->glyphs().decode(enc->bytes, codes);
            ASSERT_EQ(codes.size(), 1u);
            EXPECT_EQ(codes[0].unicode, g.unicode);
            EXPECT_NEAR(enc->w0, g.w0, 1e-4);
            ++checked;
        }
        EXPECT_GT(checked, 10);
    }
}

TEST(font_info, parse_name) {
    struct Case {
        const char* base;
        const char* family;
        int weight;
        bool italic;
    };
    const Case cases[] = {
        {"ABCDEF+ArialMT", "Arial", 0, false},
        {"Arial-BoldMT", "Arial", 700, false},
        {"Arial,BoldItalic", "Arial", 700, true},
        {"TimesNewRomanPSMT", "Times New Roman", 0, false},
        {"TimesNewRomanPS-ItalicMT", "Times New Roman", 0, true},
        {"CourierNewPS-BoldItalicMT", "Courier New", 700, true},
        {"Times-Roman", "Times", 400, false},
        {"Helvetica-BoldOblique", "Helvetica", 700, true},
        {"SegoeUI-Semibold", "Segoe UI", 600, false},
        {"Calibri-Light", "Calibri", 300, false},
        {"MyriadPro-Regular", "Myriad Pro", 400, false},
        {"MS-Gothic", "MS Gothic", 0, false},
        {"Georgia", "Georgia", 0, false},
        {"Arial Unicode MS", "Arial Unicode MS", 0, false},
        {"SymbolMT", "Symbol", 0, false},
        {"ZapfDingbats", "ZapfDingbats", 0, false},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(c.base);
        const ot::edit::FontName n = ot::edit::parseFontName(c.base);
        EXPECT_EQ(n.family, c.family);
        EXPECT_EQ(n.weight, c.weight);
        EXPECT_EQ(n.italic, c.italic);
    }
    EXPECT_EQ(ot::edit::parseFontName("ABCDEF+ArialMT").postscript, "ArialMT");
    EXPECT_TRUE(ot::edit::hasSubsetTag("ABCDEF+ArialMT"));
    EXPECT_FALSE(ot::edit::hasSubsetTag("ABCDEf+ArialMT"));
    EXPECT_FALSE(ot::edit::hasSubsetTag("Arial"));
}

TEST(font_info, weight_from_stem) {
    EXPECT_EQ(ot::edit::weightFromStemV(0), 0);  // не задан
    EXPECT_EQ(ot::edit::weightFromStemV(44), 400);
    EXPECT_EQ(ot::edit::weightFromStemV(80), 400);
    EXPECT_EQ(ot::edit::weightFromStemV(88), 400);
    EXPECT_EQ(ot::edit::weightFromStemV(120), 700);
    EXPECT_EQ(ot::edit::weightFromStemV(140), 700);
    EXPECT_EQ(ot::edit::weightFromStemV(200), 900);
}
