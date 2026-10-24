// unit.font_embed.* — встраивание системных шрифтов и подмножества при сохранении (план 3.2, п. 3; эталон 3.9
// «FontResolver», правила 2–4; edit-01, edit-02 — шрифты нового текста): новый Type0/Identity-H Arial из
// FakeSystemFonts (C:\Windows\Fonts) — словарь, дескриптор по таблицам шрифта, /ToUnicode с U+0020 и U+002D
// (а не U+00A0 и U+00AD, как пишет MuPDF), один объект на грань и вид, свой ресурс на странице; ширины новых
// пробегов — fz_advance_glyph ±0,01; простой TrueType WinAnsi для латиницы правила 4; текст новым шрифтом
// читается моделью и рисуется; undo снимает шрифт, а следующий add встраивает его заново (кеш ресурсов MuPDF
// не откатывается); при сохранении — подмножество: файл в десятки раз меньше, qpdf --check = 0, рендер тот
// же, текст тот же; шрифт, который не урезается, остаётся полным без исключения. compat.fonts —
// font_embed_compat.DISABLED_write пишет файлы для tools/compat_check.py (qpdf, pdfcpu strict, mutool clean,
// pypdfium2).
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/edit/content_model.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_info.h"
#include "core/edit/system_fonts.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/render/image_compare.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "core/util/utf8.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::edit::EmbeddedFont;
using ot::edit::EmbedKind;
using ot::edit::FakeSystemFonts;
using ot::edit::FontEmbedder;
using ot::edit::FontLibrary;
using ot::edit::FontProbe;
using ot::edit::SystemFace;

namespace {

fs::path fontsFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / "fonts" / name;
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

// Коды строки text шрифтом f (символ без глифа — ошибка теста).
std::string codes(FontEmbedder& emb, const EmbeddedFont& f, std::u32string_view text) {
    std::string out;
    for (char32_t u : text) {
        const std::string c = emb.encode(f, u);
        EXPECT_FALSE(c.empty()) << "нет глифа U+" << std::hex << static_cast<int>(u);
        out += c;
    }
    return out;
}

// Дописать поток содержимого в конец /Contents страницы (до 3.3 текст в тестах пишется так).
void appendContent(Document& doc, int page, const std::string& ops) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::Journal::Operation op(doc, "Test text");
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

std::string lineOps(const EmbeddedFont& f, const std::string& codeBytes, float x, float y, float size = 12) {
    char head[96];
    std::snprintf(head, sizeof head, "\nq BT /%s %g Tf %g %g Td ", f.resource.c_str(),
                  static_cast<double>(size), static_cast<double>(x), static_cast<double>(y));
    return head + hex(codeBytes) + " Tj ET Q\n";
}

// Текст спанов модели страницы, которые набраны ресурсом resource.
std::string textOf(const Document& doc, int page, const std::string& resource) {
    const auto model = ot::edit::ContentModel::build(doc, page);
    std::string out;
    for (size_t i = 0; i < model.spans().size(); ++i) {
        const auto& s = model.spans()[i];
        if (s.font >= 0 && model.fonts()[static_cast<size_t>(s.font)].resource == resource) {
            out += model.spanText(static_cast<int>(i));
        }
    }
    return out;
}

long long inkIn(const ot::Bitmap& bmp, int x0, int y0, int x1, int y1) {
    long long ink = 0;
    for (int y = std::max(0, y0); y < std::min(bmp.height, y1); ++y) {
        for (int x = std::max(0, x0); x < std::min(bmp.width, x1); ++x) {
            if (bmp.row(y)[static_cast<size_t>(x) * static_cast<size_t>(bmp.components)] < 128) {
                ++ink;
            }
        }
    }
    return ink;
}

struct Fixture {
    std::unique_ptr<Document> doc;
    FakeSystemFonts system;
    std::optional<SystemFace> arial;
    std::unique_ptr<FontLibrary> lib;
    std::unique_ptr<FontEmbedder> emb;

    explicit Fixture(const char* file = "mixed-latin.pdf") {
        doc = Document::openOrThrow(fontsFixture(file));
        arial = system.face("Arial", 400, false);
        lib = std::make_unique<FontLibrary>(doc->context().get());
        emb = std::make_unique<FontEmbedder>(*doc, *lib);
    }
};

}  // namespace

TEST(font_embed, cid_font_object) {
    OT_FEATURE("edit-02");
    Fixture fx;
    if (!fx.arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    ASSERT_TRUE(f.valid());
    EXPECT_EQ(f.resource.rfind("OtF", 0), 0u) << f.resource;
    auto probe = FontProbe::forPage(*fx.doc, 0, f.resource);
    ASSERT_NE(probe, nullptr);
    const auto& d = probe->details();
    EXPECT_EQ(d.subtype, "Type0");
    EXPECT_EQ(d.cidSubtype, "CIDFontType2");
    EXPECT_EQ(d.encoding, "Identity-H");
    EXPECT_EQ(d.file, ot::edit::FontFile::TrueType);
    EXPECT_TRUE(d.embedded);
    EXPECT_FALSE(d.subset);
    EXPECT_TRUE(d.toUnicode);
    EXPECT_EQ(d.name, "ArialMT");
    EXPECT_EQ(d.family, "Arial");
    EXPECT_FALSE(d.serif);
    EXPECT_FALSE(d.bold);
    EXPECT_NE(d.flags & 32, 0);
    EXPECT_NEAR(d.capHeight, 0.716, 0.01);  // /CapHeight из OS/2 (MuPDF его не пишет)
    EXPECT_NEAR(d.stemV, 88, 1);

    // Тот же шрифт ещё раз — тот же объект и ресурс; другая грань — другой.
    const EmbeddedFont again = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    EXPECT_EQ(again.objnum, f.objnum);
    EXPECT_EQ(again.resource, f.resource);
    const auto bold = fx.system.face("Arial", 700, false);
    ASSERT_TRUE(bold);
    const EmbeddedFont fb = fx.emb->add(0, *bold, EmbedKind::Cid);
    EXPECT_NE(fb.objnum, f.objnum);
    EXPECT_NE(fb.resource, f.resource);
    auto probeBold = FontProbe::forPage(*fx.doc, 0, fb.resource);
    ASSERT_NE(probeBold, nullptr);
    EXPECT_TRUE(probeBold->details().bold);
    EXPECT_NEAR(probeBold->details().stemV, 166, 1);

    // Коды — GID; /ToUnicode обратим, ширины /W — fz_advance_glyph ±0,01.
    for (char32_t u : std::u32string(U"Привет, ёж — 2026 € a-b")) {
        const std::string code = fx.emb->encode(f, u);
        ASSERT_EQ(code.size(), 2u);
        const int gid = fx.lib->gid(*fx.arial, u);
        EXPECT_EQ((static_cast<unsigned char>(code[0]) << 8) | static_cast<unsigned char>(code[1]), gid);
        EXPECT_NEAR(fx.emb->width(f, u), fx.lib->advance(*fx.arial, gid), 0.01);
        const auto enc = probe->encode(u);
        ASSERT_TRUE(enc.has_value()) << std::hex << static_cast<int>(u);
        EXPECT_EQ(enc->bytes, code);
        EXPECT_NEAR(enc->w0, fx.emb->width(f, u), 1e-4);
    }
    EXPECT_TRUE(fx.emb->encode(f, U'\x4E00').empty());  // иероглифа в Arial нет
    EXPECT_EQ(fx.emb->pending().size(), 2u);
}

TEST(font_embed, written_text_reads_and_renders) {
    OT_FEATURE("edit-01");
    Fixture fx;
    if (!fx.arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    const ot::Bitmap before = fx.doc->renderPage(0, {72, false});
    appendContent(*fx.doc, 0, lineOps(f, codes(*fx.emb, f, U"Привет a-b"), 72, 500));
    EXPECT_EQ(textOf(*fx.doc, 0, f.resource), "Привет a-b");  // пробел U+0020 и дефис U+002D
    const ot::Bitmap after = fx.doc->renderPage(0, {72, false});
    const int y = static_cast<int>(842 - 500);
    EXPECT_EQ(inkIn(before, 60, y - 14, 200, y + 4), 0);
    EXPECT_GT(inkIn(after, 60, y - 14, 200, y + 4), 80);
}

TEST(font_embed, simple_winansi) {
    Fixture fx;
    if (!fx.arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::SimpleWinAnsi);
    ASSERT_TRUE(f.valid());
    auto probe = FontProbe::forPage(*fx.doc, 0, f.resource);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->details().subtype, "TrueType");
    EXPECT_EQ(probe->details().encoding, "WinAnsiEncoding");
    EXPECT_TRUE(probe->details().embedded);
    EXPECT_EQ(fx.emb->encode(f, U'é'), "\xE9");
    EXPECT_EQ(fx.emb->encode(f, U'€'), "\x80");
    EXPECT_EQ(fx.emb->encode(f, U'A'), "A");
    EXPECT_TRUE(fx.emb->encode(f, U'Ж').empty());  // не WinAnsi — правило 4 берёт CID
    const int gid = fx.lib->gid(*fx.arial, U'é');
    EXPECT_NEAR(fx.emb->width(f, U'é'), fx.lib->advance(*fx.arial, gid), 0.01);
    EXPECT_NEAR(probe->encode(U'é')->w0, fx.emb->width(f, U'é'), 1e-4);
    appendContent(*fx.doc, 0, lineOps(f, codes(*fx.emb, f, U"Café €"), 72, 500));
    EXPECT_EQ(textOf(*fx.doc, 0, f.resource), "Café €");
}

TEST(font_embed, undo_then_add_again) {
    Fixture fx;
    if (!fx.arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    EmbeddedFont f;
    {
        ot::Journal::Operation op(*fx.doc, "Type text");
        f = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
        op.commit();
    }
    ASSERT_NE(FontProbe::forPage(*fx.doc, 0, f.resource), nullptr);
    (void)fx.doc->undoStack().undo();
    EXPECT_EQ(FontProbe::forPage(*fx.doc, 0, f.resource), nullptr);  // ресурс снят undo
    const EmbeddedFont again = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    ASSERT_TRUE(again.valid());
    auto probe = FontProbe::forPage(*fx.doc, 0, again.resource);
    ASSERT_NE(probe, nullptr);
    EXPECT_EQ(probe->details().subtype, "Type0");
    EXPECT_TRUE(probe->hasGlyph(U'Ж'));
}

TEST(font_embed, subset_on_save) {
    OT_FEATURE("edit-01");
    Fixture fx;
    if (!fx.arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    const std::u32string text = U"Привет, world! — 2026 €";
    appendContent(*fx.doc, 0, lineOps(f, codes(*fx.emb, f, text), 72, 500));
    const ot::Bitmap before = fx.doc->renderPage(0, {96, false});

    ot::test::TempDir dir;
    const fs::path out = dir.path() / "шрифты.pdf";
    ot::edit::SubsetReport report;
    ot::SaveOptions options;
    options.mode = ot::SaveMode::Full;
    options.prepare = [&](Document&) { report = fx.emb->subset(); };
    const ot::SaveReport saved = ot::Saver::save(*fx.doc, out, options);
    ASSERT_TRUE(saved.ok()) << saved.message;
    EXPECT_EQ(report.fonts, 1);
    EXPECT_EQ(report.subset, 1);
    EXPECT_EQ(report.failed, 0);
    EXPECT_LT(report.bytesAfter * 10, report.bytesBefore);
    EXPECT_LT(fs::file_size(out), 120u * 1024u);  // полный Arial — больше 700 КБ
    EXPECT_TRUE(fx.emb->pending().empty());

    const ot::QpdfTool qpdf;
    if (qpdf.available()) {
        const ot::ToolResult check = qpdf.check(out);
        EXPECT_EQ(check.exitCode, 0) << check.output;
    }
    // Документ переоткрыт из файла: подмножество с префиксом, текст и рендер те же.
    auto probe = FontProbe::forPage(*fx.doc, 0, f.resource);
    ASSERT_NE(probe, nullptr);
    EXPECT_TRUE(probe->details().subset);
    EXPECT_TRUE(probe->hasGlyph(U'П'));
    EXPECT_FALSE(probe->hasGlyph(U'Z'));  // не использован — не в подмножестве
    EXPECT_EQ(textOf(*fx.doc, 0, f.resource), ot::utf8::encode(text));
    const ot::Bitmap after = fx.doc->renderPage(0, {96, false});
    const ot::CompareResult cmp = ot::compareBitmaps(before, after);
    EXPECT_TRUE(cmp.within({})) << cmp.toJson();

    // Дальше набор новым шрифтом — новый полный объект, урезанный не переиспользуется.
    const EmbeddedFont next = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    EXPECT_NE(next.resource, f.resource);
    EXPECT_FALSE(fx.emb->encode(next, U'Z').empty());
}

TEST(font_embed, subset_failure_keeps_full_font) {
    Fixture fx;
    if (!fx.arial) {
        GTEST_SKIP() << "нет arial.ttf";
    }
    const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
    appendContent(*fx.doc, 0, lineOps(f, codes(*fx.emb, f, U"Привет"), 72, 500));
    // Программу шрифта портим — fz_subset_ttf_for_gids её не разберёт.
    fz_context* ctx = fx.doc->context().get();
    pdf_document* pdf = fx.doc->pdf();
    const std::string junk = "this is not a TrueType font program";
    {
        ot::Journal::Operation op(*fx.doc, "Break font");
        ot::mu::BufferHandle buf(ctx);
        ot::mu::call(ctx, [&] {
            pdf_obj* font = pdf_load_object(ctx, pdf, f.objnum);
            pdf_obj* desc =
                pdf_dict_get(ctx, pdf_array_get(ctx, pdf_dict_get(ctx, font, PDF_NAME(DescendantFonts)), 0),
                             PDF_NAME(FontDescriptor));
            buf.reset(fz_new_buffer_from_copied_data(ctx, reinterpret_cast<const unsigned char*>(junk.data()),
                                                     junk.size()));
            pdf_update_stream(ctx, pdf, pdf_dict_get(ctx, desc, PDF_NAME(FontFile2)), buf, 0);
            pdf_drop_obj(ctx, font);
        });
        op.commit();
    }
    ot::edit::SubsetReport report;
    EXPECT_NO_THROW(report = ot::edit::subsetFonts(*fx.doc, {f.objnum}));
    EXPECT_EQ(report.fonts, 1);
    EXPECT_EQ(report.subset, 0);
    EXPECT_EQ(report.failed, 1);
    EXPECT_FALSE(report.errors.empty());
    auto probe = FontProbe::forPage(*fx.doc, 0, f.resource);
    ASSERT_NE(probe, nullptr);
    EXPECT_FALSE(probe->details().subset);  // имя без префикса — шрифт остался как был
}

// compat.fonts: файлы для tools/compat_check.py — смешанная строка новым CID-шрифтом (с подмножеством и без),
// латиница WinAnsi (правило 4) — в OTTISK_COMPAT_OUT/fonts.
TEST(font_embed_compat, DISABLED_write) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path out = ot::paths::fromUtf8(outEnv) / "fonts";
    fs::create_directories(out);
    for (const bool subset : {true, false}) {
        Fixture fx;
        ASSERT_TRUE(fx.arial);
        const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::Cid);
        const auto italic = fx.system.face("Times New Roman", 400, true);
        ASSERT_TRUE(italic);
        const EmbeddedFont fi = fx.emb->add(0, *italic, EmbedKind::Cid);
        appendContent(*fx.doc, 0, lineOps(f, codes(*fx.emb, f, U"Привет, world! — 2026 €"), 72, 500));
        appendContent(*fx.doc, 0, lineOps(fi, codes(*fx.emb, fi, U"Съешь же ещё этих булок"), 72, 470));
        ot::SaveOptions options;
        options.mode = ot::SaveMode::Full;
        if (subset) {
            options.prepare = [&](Document&) { (void)fx.emb->subset(); };
        }
        const ot::SaveReport r =
            ot::Saver::save(*fx.doc, out / (subset ? "cid-subset.pdf" : "cid-full.pdf"), options);
        ASSERT_TRUE(r.ok()) << r.message;
    }
    {
        Fixture fx("fonts.pdf");
        ASSERT_TRUE(fx.arial);
        const EmbeddedFont f = fx.emb->add(0, *fx.arial, EmbedKind::SimpleWinAnsi);
        appendContent(*fx.doc, 0, lineOps(f, codes(*fx.emb, f, U"Café crème, 2026 €"), 72, 300));
        ot::SaveOptions options;
        options.mode = ot::SaveMode::Full;
        options.prepare = [&](Document&) { (void)fx.emb->subset(); };
        const ot::SaveReport r = ot::Saver::save(*fx.doc, out / "winansi-subset.pdf", options);
        ASSERT_TRUE(r.ok()) << r.message;
    }
}
