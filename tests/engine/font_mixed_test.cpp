// engine.font_mixed.* — кириллица и смешанный текст (план 3.2, п. 5; эталон 3.9 «FontResolver»; критерий
// этапа 3 «кириллица набирается в латинское подмножество через новый CID-шрифт»): «Привет, world! — 2026 €»
// набирается строкой в абзац tests/fixtures/edit/fonts/mixed-latin.pdf (простой TrueType Arial — подмножество
// только ASCII): три пробега и два шрифта (исходный и новый Type0 Arial), позиции глифов и рендер совпадают
// со строкой-эталоном, которую набрал генератор (mixed-expected.pdf: свой Type0 Arial с /W как у MuPDF),
// ширины новых пробегов — fz_advance_glyph ±0,01, текст извлекается как набран; после сохранения с
// подмножеством — qpdf --check = 0, тот же рендер и текст (файл — build/<пресет>/font-mixed/). Латиница в
// кириллическое подмножество Times и синтетические жирный и курсив — тем же путём.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_info.h"
#include "core/edit/font_resolver.h"
#include "core/edit/system_fonts.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/render/image_compare.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::edit::ContentModel;
using ot::edit::FakeSystemFonts;
using ot::edit::FontEmbedder;
using ot::edit::FontLibrary;
using ot::edit::FontProbe;
using ot::edit::FontResolver;
using ot::edit::ResolvedRun;

namespace {

fs::path fixture(const std::string& name) {
    return ot::paths::fromUtf8(OTTISK_SOURCE_DIR) / L"tests" / L"fixtures" / L"edit" /
           ot::paths::fromUtf8(name);
}

const json& meta() {
    static const json m = json::parse(ot::paths::readFile(fixture("fonts/fonts.json")));
    return m;
}

std::string hex(const std::string& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string out = "<";
    for (unsigned char c : bytes) {
        out.push_back(digits[c >> 4]);
        out.push_back(digits[c & 15]);
    }
    return out + ">";
}

std::string num(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.4f", v);
    std::string s = buf;
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.') {
        s.pop_back();
    }
    return s;
}

// Дописать поток в конец /Contents страницы (запись абзаца целиком — Rewriter сессии 3.3).
void appendContent(Document& doc, int page, const std::string& ops) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::Journal::Operation op(doc, "Type text");
    ot::mu::BufferHandle buf(ctx);
    ot::mu::PdfObjHandle stream(ctx);
    ot::mu::PdfObjHandle array(ctx);
    ot::mu::call(ctx, [&] {
        pdf_obj* pageObj = pdf_lookup_page_obj(ctx, pdf, page);
        buf.reset(fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(ops.data()),
                                                 ops.size()));
        stream.reset(pdf_add_stream(ctx, pdf, buf, nullptr, 0));
        pdf_obj* contents = pdf_dict_get(ctx, pageObj, PDF_NAME(Contents));
        if (pdf_is_array(ctx, contents)) {
            pdf_array_push(ctx, contents, stream);
        } else {
            array.reset(pdf_new_array(ctx, pdf, 2));
            pdf_array_push(ctx, array, contents);
            pdf_array_push(ctx, array, stream);
            pdf_dict_put(ctx, pageObj, PDF_NAME(Contents), array);
        }
    });
    op.invalidate(page);
    op.commit();
}

struct Typed {
    std::vector<ResolvedRun> runs;
    std::vector<std::string> resources;  // ресурс шрифта каждого пробега
};

// Набрать text строкой с началом (x, y) в пространстве PDF кеглем size в пробеге шрифта resource страницы 0.
Typed typeLine(Document& doc, FontResolver& resolver, FontEmbedder& emb, const std::string& resource,
               std::u32string_view text, double x, double y, double size,
               const ot::edit::StyleRequest& style = {}) {
    Typed t;
    std::unique_ptr<FontProbe> probe = FontProbe::forPage(doc, 0, resource);
    EXPECT_NE(probe, nullptr) << resource;
    if (!probe) {
        return t;
    }
    t.runs = resolver.runs(*probe, text, style);
    std::string ops = "\nBT " + num(x) + " " + num(y) + " Td\n";
    for (const ResolvedRun& r : t.runs) {
        std::string codes;
        std::string res = resource;
        if (r.choice.newFont) {
            const ot::edit::EmbeddedFont f = emb.add(0, r.choice.face, r.choice.embed);
            res = f.resource;
            for (char32_t u : r.text) {
                const std::string c = emb.encode(f, u);
                EXPECT_FALSE(c.empty()) << "U+" << std::hex << static_cast<int>(u);
                codes += c;
            }
        } else {
            for (char32_t u : r.text) {
                const auto c = probe->encode(u);
                EXPECT_TRUE(c.has_value()) << "U+" << std::hex << static_cast<int>(u);
                codes += c ? c->bytes : std::string();
            }
        }
        ops += "/" + res + " " + num(size) + " Tf " + hex(codes) + " Tj\n";
        t.resources.push_back(res);
    }
    ops += "ET\n";
    appendContent(doc, 0, ops);
    return t;
}

// Глифы последнего текстового объекта страницы.
std::vector<ot::edit::Glyph> lastLine(const ContentModel& model, std::vector<int>* spans = nullptr) {
    std::vector<ot::edit::Glyph> out;
    if (model.textObjects().empty()) {
        return out;
    }
    const int last = static_cast<int>(model.textObjects().size()) - 1;
    for (int s : model.textObjects()[static_cast<size_t>(last)].spans) {
        if (spans != nullptr) {
            spans->push_back(s);
        }
        const auto& span = model.spans()[static_cast<size_t>(s)];
        for (int i = 0; i < span.glyphCount; ++i) {
            out.push_back(model.glyphs()[static_cast<size_t>(span.firstGlyph + i)]);
        }
    }
    return out;
}

std::u32string glyphText(const std::vector<ot::edit::Glyph>& glyphs) {
    std::u32string out;
    for (const auto& g : glyphs) {
        out += g.unicode;
    }
    return out;
}

bool containsLine(const Document& doc, const std::string& line) {
    ot::TextOptions options;
    options.readingOrder = false;
    return ot::TextPage::build(doc, 0, options)->text().find(line) != std::string::npos;
}

}  // namespace

TEST(font_mixed, cyrillic_into_latin_subset) {
    OT_FEATURE("edit-01");
    const json& m = meta()["mixed"];
    FakeSystemFonts system;
    if (!system.face("Arial", 400, false)) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    auto doc = Document::openOrThrow(fixture("fonts/mixed-latin.pdf"));
    FontLibrary lib(doc->context().get());
    FontResolver resolver(lib, system);
    FontEmbedder emb(*doc, lib);
    const std::string resource = m["font"].get<std::string>();
    const std::string line = m["text"].get<std::string>();
    const std::u32string text = ot::utf8::decode(line);
    const double x = m["x"].get<double>();
    const double y = m["y"].get<double>();
    const double size = m["size"].get<double>();

    const Typed typed = typeLine(*doc, resolver, emb, resource, text, x, y, size);
    // Три пробега, два шрифта — как в ожиданиях генератора.
    ASSERT_EQ(typed.runs.size(), m["runs"].size());
    for (size_t i = 0; i < typed.runs.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(ot::utf8::encode(typed.runs[i].text), m["runs"][i][0].get<std::string>());
        EXPECT_EQ(typed.runs[i].choice.newFont, m["runs"][i][1].get<std::string>() == "new");
    }
    EXPECT_EQ(std::set<std::string>(typed.resources.begin(), typed.resources.end()).size(), 2u);
    ASSERT_EQ(emb.pending().size(), 1u);  // один новый шрифт на страницу и семейство

    // Модель: текст строки, ширины новых пробегов — fz_advance_glyph ±0,01, позиции — как у эталона.
    const ContentModel model = ContentModel::build(*doc, 0);
    std::vector<int> spans;
    const auto glyphs = lastLine(model, &spans);
    EXPECT_EQ(spans.size(), 3u);
    EXPECT_EQ(ot::utf8::encode(glyphText(glyphs)), line);
    const ot::edit::SystemFace arial = *system.face("Arial", 400, false);
    for (const auto& g : glyphs) {
        const auto& span = model.spans()[static_cast<size_t>(g.span)];
        if (model.fonts()[static_cast<size_t>(span.font)].resource != resource) {
            EXPECT_NEAR(g.w0, lib.advance(arial, g.gid), 0.01) << "gid " << g.gid;
        }
    }
    auto ref = Document::openOrThrow(fixture("fonts/mixed-expected.pdf"));
    const auto refGlyphs = lastLine(ContentModel::build(*ref, 0));
    ASSERT_EQ(refGlyphs.size(), glyphs.size());
    for (size_t i = 0; i < glyphs.size(); ++i) {
        EXPECT_EQ(glyphs[i].unicode, refGlyphs[i].unicode) << i;
        EXPECT_NEAR(glyphs[i].origin.x, refGlyphs[i].origin.x, 0.01) << i;
        EXPECT_NEAR(glyphs[i].origin.y, refGlyphs[i].origin.y, 0.01) << i;
        EXPECT_NEAR(glyphs[i].w0, refGlyphs[i].w0, 1e-4) << i;
    }

    // Рендер — как строка-эталон генератора; текст извлекается как набран.
    const ot::Bitmap expected = ref->renderPage(0, {144, false});
    const ot::Bitmap before = doc->renderPage(0, {144, false});
    const ot::CompareResult cmp = ot::compareBitmaps(before, expected);
    EXPECT_TRUE(cmp.within({})) << cmp.toJson();
    EXPECT_TRUE(containsLine(*doc, line));

    // Сохранение с подмножеством: qpdf --check, тот же рендер и текст после переоткрытия.
    const fs::path dir = ot::paths::fromUtf8(OTTISK_BINARY_DIR) / L"font-mixed";
    fs::create_directories(dir);
    const fs::path out = dir / L"mixed-typed.pdf";
    ot::edit::SubsetReport report;
    ot::SaveOptions options;
    options.mode = ot::SaveMode::Full;
    options.prepare = [&](Document&) { report = emb.subset(); };
    const ot::SaveReport saved = ot::Saver::save(*doc, out, options);
    ASSERT_TRUE(saved.ok()) << saved.message;
    EXPECT_EQ(report.subset, 1);
    EXPECT_EQ(report.failed, 0);
    EXPECT_LT(fs::file_size(out), 64u * 1024u);
    const ot::QpdfTool qpdf;
    if (qpdf.available()) {
        const ot::ToolResult check = qpdf.check(out);
        EXPECT_EQ(check.exitCode, 0) << check.output;
    }
    const ot::Bitmap after = doc->renderPage(0, {144, false});
    const ot::CompareResult cmpSaved = ot::compareBitmaps(after, expected);
    EXPECT_TRUE(cmpSaved.within({})) << cmpSaved.toJson();
    EXPECT_TRUE(containsLine(*doc, line));
}

TEST(font_mixed, latin_into_cyrillic_subset) {
    FakeSystemFonts system;
    if (!system.face("Times New Roman", 400, false)) {
        GTEST_SKIP() << "нет times.ttf";
    }
    auto doc = Document::openOrThrow(fixture("cyr-cid.pdf"));
    FontLibrary lib(doc->context().get());
    FontResolver resolver(lib, system);
    FontEmbedder emb(*doc, lib);
    const std::string line = "Съешь ещё: Hello, 2026!";
    const Typed typed = typeLine(*doc, resolver, emb, "F1", ot::utf8::decode(line), 72, 300, 12);
    ASSERT_GE(typed.runs.size(), 2u);
    EXPECT_FALSE(typed.runs.front().choice.newFont);  // «Съешь ещё» — в подмножестве Times
    for (const auto& r : typed.runs) {
        if (r.choice.newFont) {
            EXPECT_EQ(ot::edit::lowerAscii(r.choice.face.file.filename().string()), "times.ttf");
        }
    }
    EXPECT_EQ(ot::utf8::encode(glyphText(lastLine(ContentModel::build(*doc, 0)))), line);
    EXPECT_TRUE(containsLine(*doc, line));

    const fs::path dir = ot::paths::fromUtf8(OTTISK_BINARY_DIR) / L"font-mixed";
    fs::create_directories(dir);
    const fs::path out = dir / L"cyr-typed.pdf";
    ot::SaveOptions options;
    options.mode = ot::SaveMode::Full;
    options.prepare = [&](Document&) { (void)emb.subset(); };
    ASSERT_TRUE(ot::Saver::save(*doc, out, options).ok());
    const ot::QpdfTool qpdf;
    if (qpdf.available()) {
        EXPECT_EQ(qpdf.check(out).exitCode, 0);
    }
    EXPECT_TRUE(containsLine(*doc, line));
}

// Синтетические начертания (Tr 2 с обводкой, наклон) действительно рисуются: жирный темнее, курсив наклонён.
TEST(font_mixed, synthetic_styles_render) {
    OT_FEATURE("edit-03");
    FakeSystemFonts system;
    const auto arial = system.face("Arial", 400, false);
    if (!arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    auto doc = Document::openOrThrow(fixture("fonts/mixed-latin.pdf"));
    FontLibrary lib(doc->context().get());
    FontEmbedder emb(*doc, lib);
    const ot::edit::EmbeddedFont f = emb.add(0, *arial, ot::edit::EmbedKind::Cid);
    std::string codes;
    for (char32_t u : std::u32string(U"Жирный HHHH")) {
        codes += emb.encode(f, u);
    }
    ot::edit::FontChoice plain;
    ot::edit::FontChoice bold;
    bold.syntheticBold = true;
    ot::edit::FontChoice italic;
    italic.syntheticItalic = true;
    std::string ops;
    double y = 400;
    for (const ot::edit::FontChoice* c : {&plain, &bold, &italic}) {
        const ot::edit::SyntheticStyle st = ot::edit::syntheticStyle(*c, 20);
        const ot::Matrix tm =
            ot::edit::syntheticTextMatrix(ot::Matrix::translate(72, static_cast<float>(y)), st);
        ops += "\nq BT " + ot::edit::syntheticOps(st) + " /" + f.resource + " 20 Tf " + num(tm.a) + " " +
               num(tm.b) + " " + num(tm.c) + " " + num(tm.d) + " " + num(tm.e) + " " + num(tm.f) + " Tm " +
               hex(codes) + " Tj ET Q\n";
        y -= 50;
    }
    appendContent(*doc, 0, ops);
    const ot::Bitmap bmp = doc->renderPage(0, {72, false});
    auto ink = [&](int top) {
        long long n = 0;
        for (int yy = top; yy < top + 26 && yy < bmp.height; ++yy) {
            for (int xx = 60; xx < 300 && xx < bmp.width; ++xx) {
                n += bmp.row(yy)[static_cast<size_t>(xx) * static_cast<size_t>(bmp.components)] < 128 ? 1 : 0;
            }
        }
        return n;
    };
    const int h = bmp.height;  // страница 842 pt при 72 dpi: y вниз
    const long long inkPlain = ink(h - 400 - 20);
    const long long inkBold = ink(h - 350 - 20);
    EXPECT_GT(inkPlain, 200);
    EXPECT_GT(inkBold, inkPlain * 11 / 10);  // обводка утолщает штрихи

    // Курсив: верх рамки глифа «H» сдвинут вправо относительно низа на 0,2 высоты.
    const ContentModel model = ContentModel::build(*doc, 0);
    const auto glyphs = lastLine(model);
    ASSERT_FALSE(glyphs.empty());
    const auto& q = glyphs.back().quad;
    const float height = q.ll.y - q.ul.y;
    EXPECT_NEAR(q.ul.x - q.ll.x, 0.2f * height, 0.05f * height);
}
