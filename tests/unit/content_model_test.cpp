// unit.content_model.* — модель контент-потока (план 3.1, п. 2; эталон 3.9 «Модель»): разбор BT…ET с
// Tj/TJ/'/" даёт спаны с правильным Unicode, шрифтом, кеглем, цветом и геометрией — против ожиданий
// генератора (tests/fixtures/edit/expected.json: латиница, кириллица CID, Type1 /Differences, base14 без
// /Widths, TJ-кернинг, Tz/Tc/Tw/Ts, повёрнутый текст, два потока /Contents, XObject-форма, inline image) и
// против fz_stext той же страницы через pdf_new_run_processor (символ за символом, рамки ±0,5 pt); операции
// покрывают потоки без пропусков; причины «только чтение» (форма, поворот, Tr 7, Type3, нет шрифта,
// /Pattern).
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/edit/content_model.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::edit::ContentModel;
using ot::edit::OpKind;
using ot::edit::ReadOnly;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

const json& expected() {
    static const json meta = json::parse(ot::paths::readFile(editFixture("expected.json")));
    return meta;
}

// Восемь фикстур плана и ещё две (base14, inline image).
const char* const kFixtures[] = {"latin.pdf",    "cyr-cid.pdf",     "type1-diff.pdf",  "tj-kerning.pdf",
                                 "tz-tc-tw.pdf", "rotated.pdf",     "two-streams.pdf", "xobject-form.pdf",
                                 "base14.pdf",   "inline-image.pdf"};

std::string hexColor(std::uint32_t argb) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", (argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
    return buf;
}

// Однострочный PDF в памяти: страница A4 с контентом content и ресурсами resources (словарь без << >>);
// objects — объекты 4, 5, … («<< … >>» или поток целиком).
std::string makeRawPdf(const std::string& content, const std::string& resources,
                       const std::vector<std::string>& objects = {}) {
    std::vector<std::string> objs = {
        "<< /Type /Catalog /Pages 2 0 R >>",
        "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
        "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Resources << " + resources + " >> /Contents " +
            std::to_string(4 + objects.size()) + " 0 R >>",
    };
    objs.insert(objs.end(), objects.begin(), objects.end());
    objs.push_back("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content +
                   "\nendstream");
    std::string out = "%PDF-1.7\n";
    std::vector<size_t> offsets;
    for (size_t i = 0; i < objs.size(); ++i) {
        offsets.push_back(out.size());
        out += std::to_string(i + 1) + " 0 obj\n" + objs[i] + "\nendobj\n";
    }
    const size_t xref = out.size();
    out += "xref\n0 " + std::to_string(objs.size() + 1) + "\n0000000000 65535 f \n";
    for (size_t off : offsets) {
        char line[32];
        std::snprintf(line, sizeof line, "%010zu 00000 n \n", off);
        out += line;
    }
    out += "trailer\n<< /Size " + std::to_string(objs.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
           std::to_string(xref) + "\n%%EOF\n";
    return out;
}

}  // namespace

TEST(content_model, spans_match_generator_expectations) {
    for (const char* name : kFixtures) {
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(editFixture(name));
        const ContentModel model = ContentModel::build(*doc, 0);
        const json& exp = expected()["fixtures"][name]["spans"];
        ASSERT_EQ(model.spans().size(), exp.size());
        for (size_t i = 0; i < exp.size(); ++i) {
            const auto& s = model.spans()[i];
            const json& e = exp[i];
            SCOPED_TRACE(e.dump());
            EXPECT_EQ(model.spanText(static_cast<int>(i)), e["text"].get<std::string>());
            ASSERT_GE(s.font, 0);
            EXPECT_EQ(model.fonts()[s.font].name, e["font"].get<std::string>());
            EXPECT_NEAR(s.size, e["size"].get<double>(), 1e-3);
            EXPECT_EQ(hexColor(s.fill), e["color"].get<std::string>());
            EXPECT_STREQ(ot::edit::readOnlyCode(s.reason), e["readOnly"].get<std::string>().c_str());
            if (e.contains("x0")) {
                EXPECT_NEAR(s.origin.x, e["x0"].get<double>(), 0.5);
                EXPECT_NEAR(s.end.x, e["x1"].get<double>(), 0.5);
                EXPECT_NEAR(s.origin.y, e["baseline"].get<double>(), 0.5);
                EXPECT_NEAR(s.angle, 0.0, 0.01);
            } else {
                EXPECT_NEAR(s.origin.x, e["origin"][0].get<double>(), 0.5);
                EXPECT_NEAR(s.origin.y, e["origin"][1].get<double>(), 0.5);
                EXPECT_NEAR(s.angle, e["angle"].get<double>(), 0.05);
            }
        }
    }
}

TEST(content_model, glyphs_match_stext_through_run_processor) {
    OT_FEATURE("edit-01");
    for (const char* name : kFixtures) {
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(editFixture(name));
        const ContentModel model = ContentModel::build(*doc, 0);
        const ot::edit::CrossCheck check = ContentModel::crossCheck(*doc, 0, model);
        EXPECT_GT(check.compared, 0);
        EXPECT_EQ(check.modelChars, check.stextChars);
        EXPECT_EQ(check.compared, check.modelChars);
        EXPECT_EQ(check.mismatches, 0) << check.firstMismatch;
        EXPECT_LE(check.maxDelta, 0.5f);
    }
}

TEST(content_model, operations_cover_every_stream_byte) {
    for (const auto& [name, fx] : expected()["fixtures"].items()) {
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(editFixture(name));
        for (int page = 0; page < doc->pageCount(); ++page) {
            const ContentModel model = ContentModel::build(*doc, page);
            ASSERT_FALSE(model.streams().empty());
            for (const auto& stream : model.streams()) {
                size_t pos = 0;
                for (int op : stream.ops) {
                    const auto& o = model.operations()[op];
                    EXPECT_EQ(o.offset, pos);
                    EXPECT_GE(o.tokenOffset, o.offset);
                    EXPECT_LE(o.tokenOffset, o.offset + o.length);
                    pos = o.offset + o.length;
                }
                EXPECT_EQ(stream.tail, pos);
                EXPECT_LE(stream.tail, stream.bytes.size());
            }
            EXPECT_EQ(model.syntaxErrors(), 0);
        }
    }
}

TEST(content_model, serialize_without_edits_is_byte_identical) {
    for (const auto& [name, fx] : expected()["fixtures"].items()) {
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(editFixture(name));
        for (int page = 0; page < doc->pageCount(); ++page) {
            const ContentModel model = ContentModel::build(*doc, page);
            for (size_t s = 0; s < model.streams().size(); ++s) {
                EXPECT_EQ(model.serialize(static_cast<int>(s)), model.streams()[s].bytes) << "поток " << s;
            }
        }
    }
}

TEST(content_model, operators_and_operands) {
    auto doc = Document::openOrThrow(editFixture("tj-kerning.pdf"));
    const ContentModel model = ContentModel::build(*doc, 0);
    int tj = 0;
    for (const auto& op : model.operations()) {
        if (op.kind == OpKind::ShowArray) {
            ASSERT_EQ(op.operands.size(), 1u);
            EXPECT_EQ(op.operands[0].kind, ot::edit::Operand::Kind::Array);
            EXPECT_GE(op.span, 0);
            EXPECT_EQ(model.raw(static_cast<int>(&op - model.operations().data())).back(), 'J');
            ++tj;
        }
        if (op.kind == OpKind::Font) {
            ASSERT_EQ(op.operands.size(), 2u);
            EXPECT_EQ(op.operands[0].kind, ot::edit::Operand::Kind::Name);
            EXPECT_TRUE(op.operands[1].isNumber());
        }
    }
    EXPECT_EQ(tj, 3);
    // Литеральная строка с \( \) \\ — байты без экранирования, ширины по кодам.
    EXPECT_EQ(model.spanText(2), "Name:Value (with parens) and back\\slash");
    ASSERT_EQ(model.textObjects().size(), 2u);
    EXPECT_EQ(model.textObjects()[0].spans.size(), 2u);
}

TEST(content_model, contents_array_carries_state_and_operands) {
    auto doc = Document::openOrThrow(editFixture("two-streams.pdf"));
    const ContentModel model = ContentModel::build(*doc, 0);
    ASSERT_EQ(model.streams().size(), 2u);
    ASSERT_EQ(model.spans().size(), 3u);
    EXPECT_EQ(model.spans()[0].stream, 0);
    EXPECT_EQ(model.spans()[2].stream, 1);
    for (const auto& s : model.spans()) {
        EXPECT_EQ(hexColor(s.fill), "#000099");  // rg из первого потока действует во втором
    }
    // «72 710» — в конце первого потока, «Td» — в начале второго.
    const int showOp = model.spans()[2].op;
    const auto& td = model.operations()[showOp - 1];
    EXPECT_EQ(td.kind, OpKind::Move);
    EXPECT_EQ(td.stream, 1);
    EXPECT_TRUE(td.split);
    ASSERT_EQ(td.operands.size(), 2u);
    EXPECT_DOUBLE_EQ(td.operands[0].number, 72.0);
    EXPECT_FALSE(model.operations()[showOp].split);
    EXPECT_GT(model.streams()[0].bytes.size(), model.streams()[0].tail);  // висящие операнды — хвост потока 0
    const auto& t = model.textObjects()[model.spans()[2].textObject];
    EXPECT_EQ(model.operations()[t.begin].stream, 0);
    EXPECT_EQ(model.operations()[t.end].stream, 1);
}

TEST(content_model, inline_images_compat_and_marked_content) {
    auto doc = Document::openOrThrow(editFixture("inline-image.pdf"));
    const ContentModel model = ContentModel::build(*doc, 0);
    int images = 0;
    bool unknownInCompat = false;
    bool mcidDict = false;
    for (size_t i = 0; i < model.operations().size(); ++i) {
        const auto& op = model.operations()[i];
        const std::string_view raw = model.raw(static_cast<int>(i));
        if (op.kind == OpKind::InlineImage) {
            ++images;
            EXPECT_EQ(raw.substr(op.tokenOffset - op.offset, 2), "BI");
            EXPECT_EQ(raw.substr(raw.size() - 2), "EI");
        }
        if (op.name == "xyzzy") {
            unknownInCompat = op.kind == OpKind::Unknown && op.operands.size() == 4;
        }
        if (op.name == "BDC") {
            ASSERT_EQ(op.operands.size(), 2u);
            const auto* mcid = op.operands[1].get("MCID");
            mcidDict = mcid != nullptr && mcid->number == 0;
        }
    }
    EXPECT_EQ(images, 2);  // в данных первой — байты « EI »: конец ищется как в MuPDF, по длине картинки
    EXPECT_TRUE(unknownInCompat);
    EXPECT_TRUE(mcidDict);
    ASSERT_EQ(model.spans().size(), 2u);
    EXPECT_EQ(model.spans()[0].mcid, -1);
    EXPECT_EQ(model.spans()[1].mcid, 0);
    // Хвост — комментарий без перевода строки в конце потока.
    const auto& s = model.streams()[0];
    EXPECT_EQ(std::string_view(s.bytes).substr(s.tail).find("% trailing comment"), 1u);
}

TEST(content_model, fonts_and_read_only_reasons) {
    {
        auto doc = Document::openOrThrow(editFixture("type1-diff.pdf"));
        const ContentModel model = ContentModel::build(*doc, 0);
        ASSERT_EQ(model.fonts().size(), 1u);
        const auto& f = model.fonts()[0];
        EXPECT_EQ(f.subtype, "Type1");
        EXPECT_FALSE(f.embedded);
        EXPECT_FALSE(f.toUnicode);
        EXPECT_TRUE(f.base14);
        EXPECT_TRUE(f.widths);
        EXPECT_EQ(f.reason, ReadOnly::None);
    }
    {
        auto doc = Document::openOrThrow(editFixture("base14.pdf"));
        const ContentModel model = ContentModel::build(*doc, 0);
        ASSERT_EQ(model.fonts().size(), 1u);
        EXPECT_FALSE(model.fonts()[0].widths);
        EXPECT_TRUE(model.fonts()[0].base14);
        EXPECT_EQ(model.fonts()[0].reason, ReadOnly::None);
    }
    {
        auto doc = Document::openOrThrow(editFixture("tz-tc-tw.pdf"));
        const ContentModel model = ContentModel::build(*doc, 0);
        bool simple = false;
        bool cid = false;
        for (const auto& f : model.fonts()) {
            simple = simple || (f.subtype == "TrueType" && !f.twoByte && f.embedded && f.subset);
            cid = cid || (f.subtype == "Type0" && f.twoByte && f.toUnicode);
        }
        EXPECT_TRUE(simple);
        EXPECT_TRUE(cid);
        EXPECT_EQ(model.spans().back().render, 7);
        EXPECT_EQ(model.spans().back().reason, ReadOnly::Clip);
    }
    {
        auto doc = Document::openOrThrow(editFixture("xobject-form.pdf"));
        const ContentModel model = ContentModel::build(*doc, 0);
        ASSERT_EQ(model.streams().size(), 2u);
        EXPECT_TRUE(model.streams()[1].form);
        EXPECT_EQ(model.streams()[1].depth, 1);
        EXPECT_EQ(model.operations()[model.streams()[1].parentOp].kind, OpKind::XObject);
        EXPECT_TRUE(model.spans().back().form);
        EXPECT_EQ(model.spans().back().reason, ReadOnly::Form);
    }
    {
        // Шрифта нет в ресурсах — MuPDF подставляет запасной: «только чтение»; Type3 и узор — тоже.
        const std::string type3 =
            "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
            "/CharProcs << /a 5 0 R >> /Encoding << /Differences [97 /a] >> /FirstChar 97 /LastChar 97 "
            "/Widths [1000] >>";
        const std::string proc = "<< /Length 31 >>\nstream\n1000 0 d0 0 0 1000 1000 re f\nendstream";
        const std::string pattern =
            "<< /PatternType 2 /Shading << /ShadingType 2 /ColorSpace /DeviceRGB "
            "/Coords [0 0 100 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] "
            "/C1 [0 0 1] /N 1 >> >> >>";
        const std::string content =
            "BT /F9 12 Tf 72 700 Td (missing) Tj ET\n"
            "BT /T3 12 Tf 72 680 Td (aaa) Tj ET\n"
            "/Pattern cs /P1 scn BT /F1 12 Tf 72 660 Td (pattern) Tj ET\n"
            "0 g BT /F1 12 Tf 72 640 Td (plain) Tj ET";
        const std::string pdf =
            makeRawPdf(content, "/Font << /T3 4 0 R /F1 6 0 R >> /Pattern << /P1 7 0 R >>",
                       {type3, proc, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>", pattern});
        auto doc = Document::openOrThrowMemory(pdf);
        const ContentModel model = ContentModel::build(*doc, 0);
        ASSERT_EQ(model.spans().size(), 4u);
        EXPECT_EQ(model.spans()[0].reason, ReadOnly::FontMissing);
        EXPECT_TRUE(model.fonts()[model.spans()[0].font].substitute);
        EXPECT_EQ(model.spans()[1].reason, ReadOnly::Type3);
        EXPECT_EQ(model.spans()[2].reason, ReadOnly::Pattern);
        EXPECT_EQ(model.spans()[3].reason, ReadOnly::None);
        EXPECT_EQ(model.spanText(3), "plain");
        const auto check = ContentModel::crossCheck(*doc, 0, model);
        EXPECT_EQ(check.mismatches, 0) << check.firstMismatch;
        EXPECT_EQ(check.modelChars, check.stextChars);
    }
}

TEST(content_model, syntax_errors_like_mupdf) {
    // Лишние операнды (берутся первые), недостающие (нули), ошибка лексера, «]» вне массива, endstream в
    // середине.
    const std::string content =
        "BT /F1 12 Tf 1 2 72 700 Td (one) Tj ET\n"
        "BT /F1 12 Tf Td (two) Tj ET\n"
        "] 5 5 m BT /F1 10 Tf 72 600 Td (three) Tj ET\n"
        "endstream BT /F1 10 Tf 72 500 Td (never) Tj ET";
    const std::string pdf = makeRawPdf(content, "/Font << /F1 4 0 R >>",
                                       {"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>"});
    auto doc = Document::openOrThrowMemory(pdf);
    const ContentModel model = ContentModel::build(*doc, 0);
    ASSERT_EQ(model.spans().size(), 3u);
    EXPECT_NEAR(model.spans()[0].origin.x, 1.0, 1e-3);  // Td(1, 2) — первые два числа
    EXPECT_NEAR(model.spans()[1].origin.x, 0.0, 1e-3);  // Td без операндов — (0, 0)
    EXPECT_EQ(model.spanText(2), "three");
    EXPECT_GE(model.syntaxErrors(), 1);
    const auto& s = model.streams()[0];
    EXPECT_NE(std::string_view(s.bytes).substr(s.tail).find("never"), std::string_view::npos);
    const auto check = ContentModel::crossCheck(*doc, 0, model);
    EXPECT_EQ(check.mismatches, 0) << check.firstMismatch;
    EXPECT_EQ(check.modelChars, check.stextChars);
}

TEST(content_model, word_and_layout_fixtures_match_stext) {
    // Настоящий производитель (Word: TJ с кернингом, подмножества TrueType, теги) и фикстуры раскладки.
    for (const auto& [name, fx] : expected()["fixtures"].items()) {
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(editFixture(name));
        for (int page = 0; page < doc->pageCount(); ++page) {
            const ContentModel model = ContentModel::build(*doc, page);
            const ot::edit::CrossCheck check = ContentModel::crossCheck(*doc, page, model);
            EXPECT_GT(check.compared, 0);
            EXPECT_EQ(check.modelChars, check.stextChars);
            EXPECT_EQ(check.mismatches, 0) << check.firstMismatch;
        }
    }
}
