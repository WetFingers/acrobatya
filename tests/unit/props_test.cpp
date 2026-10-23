// unit.props.* — свойства документа и метаданные (эталон 3.1 «Свойства документа», 3.13 «Метаданные»): /Info
// (чтение и запись, пользовательские ключи), XMP — свой разбор и сериализатор (правка заголовка
// синхронизирует dc:title, dc:creator, dc:description, pdf:Keywords, xmp:ModifyDate; pdfaid, dc:subject
// rdf:Bag и чужие схемы не трогаются; нет XMP — создаётся; XML валиден), /Lang (ocr-32), Tagged, шрифты по
// всем страницам (тип, встроен, подмножество, кодировка) с кешем, «Дополнительно» (версия, линеаризация,
// объекты, pdf_check_document), undo правки свойств, сверка /Info сохранённого файла по qpdf --json (трейлер
// → /Info). unit.xmp.* — разбор, даты, экранирование.
#include <gtest/gtest.h>

#include <QString>
#include <QXmlStreamReader>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/props.h"
#include "core/doc/saver.h"
#include "core/doc/xmp.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::Journal;
namespace props = ot::props;
namespace xmp = ot::xmp;

namespace {

fs::path mini(const std::wstring& name) { return ot::test::sourceDir() / L"tests/data/mini" / name; }

// XML правильно построен (QXmlStreamReader без ошибок).
::testing::AssertionResult wellFormed(const std::string& xml) {
    QXmlStreamReader reader(QString::fromUtf8(xml.data(), static_cast<qsizetype>(xml.size())));
    while (!reader.atEnd()) {
        reader.readNext();
    }
    if (reader.hasError()) {
        return ::testing::AssertionFailure()
               << reader.errorString().toStdString() << " at line " << reader.lineNumber() << "\n"
               << xml.substr(0, 1500);
    }
    return ::testing::AssertionSuccess();
}

// Документ с заданным XMP в /Metadata (без /Info).
std::unique_ptr<Document> docWithXmp(const std::string& packet) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    Journal::Operation op(*doc, "Set XMP");
    ot::mu::BufferHandle buf(ctx);
    ot::mu::PdfObjHandle dict(ctx);
    ot::mu::PdfObjHandle ref(ctx);
    const auto* data = reinterpret_cast<const unsigned char*>(packet.data());
    const size_t size = packet.size();
    ot::mu::call(ctx, [&] {
        buf.reset(fz_new_buffer_from_copied_data(ctx, data, size));
        dict.reset(pdf_new_dict(ctx, pdf, 2));
        pdf_dict_put(ctx, dict, PDF_NAME(Type), PDF_NAME(Metadata));
        pdf_dict_put(ctx, dict, PDF_NAME(Subtype), PDF_NAME(XML));
        ref.reset(pdf_add_stream(ctx, pdf, buf, dict, 0));
        pdf_dict_put(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root)), PDF_NAME(Metadata), ref);
    });
    op.commit();
    doc->undoStack().clear();
    return doc;
}

const char* kPdfaPacket = R"(<?xpacket begin="﻿" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about="" xmlns:pdfaid="http://www.aiim.org/pdfa/ns/id/" pdfaid:part="2" pdfaid:conformance="B"/>
  <rdf:Description rdf:about="" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:pdf="http://ns.adobe.com/pdf/1.3/"
    pdf:Keywords="старые; слова">
   <dc:title><rdf:Alt><rdf:li xml:lang="x-default">Старый</rdf:li><rdf:li xml:lang="en">Old</rdf:li></rdf:Alt></dc:title>
   <dc:subject><rdf:Bag><rdf:li>один</rdf:li><rdf:li>два</rdf:li></rdf:Bag></dc:subject>
  </rdf:Description>
  <rdf:Description rdf:about="" xmlns:foo="urn:example:foo"><foo:bar>чужое &amp; нетронутое</foo:bar></rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>)";

}  // namespace

// ---------------------------------------------------------------- xmp

TEST(xmp, dates_convert_between_pdf_and_iso) {
    EXPECT_EQ(xmp::pdfDateToIso("D:20260101120000Z"), "2026-01-01T12:00:00Z");
    EXPECT_EQ(xmp::pdfDateToIso("D:20261023153045+03'00'"), "2026-10-23T15:30:45+03:00");
    EXPECT_EQ(xmp::pdfDateToIso("D:2026"), "2026");
    EXPECT_EQ(xmp::pdfDateToIso("D:202610"), "2026-10");
    EXPECT_EQ(xmp::pdfDateToIso("garbage"), "");
    EXPECT_EQ(xmp::isoToPdfDate("2026-10-23T15:30:45+03:00"), "D:20261023153045+03'00'");
    EXPECT_EQ(xmp::isoToPdfDate("2026-01-01T12:00:00Z"), "D:20260101120000Z");
    EXPECT_EQ(xmp::isoToPdfDate("2026-01-01T12:00:00.123-05:30"), "D:20260101120000-05'30'");
}

TEST(xmp, edits_own_properties_and_keeps_everything_else) {
    xmp::Packet p = xmp::Packet::parse(kPdfaPacket);
    EXPECT_EQ(p.get(xmp::kDc, "title").value_or(""), "Старый");
    EXPECT_EQ(p.get(xmp::kPdf, "Keywords").value_or(""), "старые; слова");
    EXPECT_EQ(p.get(xmp::kPdfaId, "part").value_or(""), "2");
    p.setAlt(xmp::kDc, "title", "Новый <заголовок> & \"кавычки\"");
    p.setSimple(xmp::kPdf, "Keywords", "новые");
    p.setSeq(xmp::kDc, "creator", {"Автор"});
    p.setSimple(xmp::kXmp, "ModifyDate", "2026-10-23T15:30:45+03:00");
    const std::string out = p.serialize();
    EXPECT_TRUE(wellFormed(out));
    xmp::Packet q = xmp::Packet::parse(out);
    EXPECT_EQ(q.get(xmp::kDc, "title").value_or(""), "Новый <заголовок> & \"кавычки\"");
    EXPECT_EQ(q.get(xmp::kPdf, "Keywords").value_or(""), "новые");
    EXPECT_EQ(q.get(xmp::kDc, "creator").value_or(""), "Автор");
    EXPECT_EQ(q.get(xmp::kXmp, "ModifyDate").value_or(""), "2026-10-23T15:30:45+03:00");
    // pdfaid, Bag dc:subject, вторая язычная версия заголовка и чужая схема — как были.
    EXPECT_EQ(q.get(xmp::kPdfaId, "part").value_or(""), "2");
    EXPECT_EQ(q.get(xmp::kPdfaId, "conformance").value_or(""), "B");
    EXPECT_EQ(q.get(xmp::kDc, "subject").value_or(""), "один; два");
    EXPECT_EQ(q.get("urn:example:foo", "bar").value_or(""), "чужое & нетронутое");
    EXPECT_NE(out.find("xml:lang=\"en\">Old</rdf:li>"), std::string::npos);
    EXPECT_NE(out.find("<?xpacket begin="), std::string::npos);
    EXPECT_NE(out.find("<?xpacket end=\"w\"?>"), std::string::npos);
    // Удаление.
    q.remove(xmp::kPdf, "Keywords");
    EXPECT_FALSE(xmp::Packet::parse(q.serialize()).get(xmp::kPdf, "Keywords").has_value());
}

TEST(xmp, new_packet_and_malformed_input) {
    xmp::Packet p = xmp::Packet::create();
    p.setAlt(xmp::kDc, "title", "Т");
    p.setSimple(xmp::kPdfx, "Department", "ОТК");
    const std::string out = p.serialize();
    EXPECT_TRUE(wellFormed(out));
    EXPECT_EQ(xmp::Packet::parse(out).get(xmp::kPdfx, "Department").value_or(""), "ОТК");
    EXPECT_THROW((void)xmp::Packet::parse("<x:xmpmeta><rdf:RDF></x:xmpmeta>"), std::runtime_error);
    EXPECT_THROW((void)xmp::Packet::parse("not xml at all <"), std::runtime_error);
}

// ---------------------------------------------------------------- props

TEST(props, reads_info_lang_and_xmp_of_fixture) {
    OT_FEATURE("view-45");
    auto doc = Document::openOrThrow(mini(L"blank-a4.pdf"));
    const props::InfoFields info = props::readInfo(*doc);
    EXPECT_EQ(info.title, "blank-a4");
    EXPECT_EQ(info.author, "Оттиск: генератор фикстур");
    EXPECT_EQ(info.subject, "Пустая страница A4");
    EXPECT_EQ(info.creator, "tools/gen/make_mini.py");
    EXPECT_EQ(info.creationDate, "D:20260101120000Z");
    EXPECT_TRUE(info.custom.empty());
    EXPECT_EQ(props::lang(*doc), "ru-RU");
    EXPECT_FALSE(props::tagged(*doc));
    const std::string packet = props::xmpPacket(*doc);
    EXPECT_TRUE(wellFormed(packet));
    EXPECT_EQ(xmp::Packet::parse(packet).get(xmp::kDc, "title").value_or(""), "blank-a4");
}

TEST(props, title_edit_syncs_info_and_xmp_and_undo_restores_both) {
    OT_FEATURE("view-46");
    auto doc = Document::openOrThrow(mini(L"blank-a4.pdf"));
    ot::DocProperties p = doc->properties();
    p.title = "Новый заголовок";
    p.keywords = "раз; два";
    doc->setProperties(p);
    EXPECT_EQ(doc->properties().title, "Новый заголовок");
    const props::InfoFields info = props::readInfo(*doc);
    EXPECT_EQ(info.keywords, "раз; два");
    EXPECT_FALSE(info.modDate.empty());
    EXPECT_NE(info.modDate, "D:20260101120000Z");  // ModDate — время правки
    const std::string packet = props::xmpPacket(*doc);
    ASSERT_TRUE(wellFormed(packet));
    const xmp::Packet x = xmp::Packet::parse(packet);
    EXPECT_EQ(x.get(xmp::kDc, "title").value_or(""), "Новый заголовок");
    EXPECT_EQ(x.get(xmp::kDc, "creator").value_or(""), info.author);
    EXPECT_EQ(x.get(xmp::kDc, "description").value_or(""), info.subject);
    EXPECT_EQ(x.get(xmp::kPdf, "Keywords").value_or(""), "раз; два");
    EXPECT_EQ(x.get(xmp::kXmp, "ModifyDate").value_or(""), xmp::pdfDateToIso(info.modDate));
    EXPECT_EQ(x.get(xmp::kXmp, "CreateDate").value_or(""), "2026-01-01T12:00:00Z");
    // dc:language (Bag) и xmpMM — нетронуты.
    EXPECT_EQ(x.get(xmp::kDc, "language").value_or(""), "ru-RU");
    EXPECT_EQ(doc->undoStack().undoName(), "Document properties");
    (void)doc->undoStack().undo();
    EXPECT_EQ(doc->properties().title, "blank-a4");
    EXPECT_EQ(xmp::Packet::parse(props::xmpPacket(*doc)).get(xmp::kDc, "title").value_or(""), "blank-a4");
}

TEST(props, missing_xmp_is_created_and_pdfa_bag_and_foreign_schemas_survive) {
    OT_FEATURE("view-46");
    auto plain = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    EXPECT_EQ(props::xmpPacket(*plain), "");
    ot::DocProperties p = plain->properties();
    p.title = "Без XMP";
    p.author = "Я";
    plain->setProperties(p);
    const std::string created = props::xmpPacket(*plain);
    ASSERT_TRUE(wellFormed(created));
    EXPECT_EQ(xmp::Packet::parse(created).get(xmp::kDc, "title").value_or(""), "Без XMP");
    fz_context* ctx = plain->context().get();
    int typeOk = 0;
    ot::mu::call(ctx, [&] {
        pdf_obj* meta = pdf_dict_getp(ctx, pdf_trailer(ctx, plain->pdf()), "Root/Metadata");
        typeOk = pdf_name_eq(ctx, pdf_dict_get(ctx, meta, PDF_NAME(Type)), PDF_NAME(Metadata)) &&
                 pdf_name_eq(ctx, pdf_dict_get(ctx, meta, PDF_NAME(Subtype)), PDF_NAME(XML));
    });
    EXPECT_EQ(typeOk, 1);

    auto pdfa = docWithXmp(kPdfaPacket);
    EXPECT_EQ(pdfa->properties().pdfa, "PDF/A-2B");
    p = pdfa->properties();
    p.title = "Заголовок PDF/A";
    pdfa->setProperties(p);
    const std::string kept = props::xmpPacket(*pdfa);
    ASSERT_TRUE(wellFormed(kept));
    const xmp::Packet x = xmp::Packet::parse(kept);
    EXPECT_EQ(x.get(xmp::kDc, "title").value_or(""), "Заголовок PDF/A");
    EXPECT_EQ(x.get(xmp::kPdfaId, "part").value_or(""), "2");
    EXPECT_EQ(x.get(xmp::kDc, "subject").value_or(""), "один; два");
    EXPECT_EQ(x.get("urn:example:foo", "bar").value_or(""), "чужое & нетронутое");
    EXPECT_EQ(pdfa->properties().pdfa, "PDF/A-2B");
}

TEST(props, custom_info_keys_are_written_to_info_and_pdfx) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    props::InfoFields info = props::readInfo(*doc);
    info.title = "С ключами";
    info.custom["Department"] = "ОТК";
    info.custom["Project"] = "АкроБатя";
    props::writeInfo(*doc, info);
    const props::InfoFields back = props::readInfo(*doc);
    EXPECT_EQ(back.custom.at("Department"), "ОТК");
    EXPECT_EQ(back.custom.at("Project"), "АкроБатя");
    const xmp::Packet x = xmp::Packet::parse(props::xmpPacket(*doc));
    EXPECT_EQ(x.get(xmp::kPdfx, "Department").value_or(""), "ОТК");
    // Удалённый пользовательский ключ уходит и из /Info, и из XMP.
    info.custom.erase("Department");
    props::writeInfo(*doc, info);
    EXPECT_EQ(props::readInfo(*doc).custom.count("Department"), 0u);
    EXPECT_FALSE(xmp::Packet::parse(props::xmpPacket(*doc)).get(xmp::kPdfx, "Department").has_value());
}

TEST(props, language_is_read_and_written_with_undo) {
    OT_FEATURE("ocr-32");
    auto doc = Document::openOrThrow(mini(L"blank-a4.pdf"));
    props::setLang(*doc, "en-US");
    EXPECT_EQ(props::lang(*doc), "en-US");
    EXPECT_EQ(doc->undoStack().undoName(), "Document language");
    (void)doc->undoStack().undo();
    EXPECT_EQ(props::lang(*doc), "ru-RU");
    props::setLang(*doc, "");
    EXPECT_EQ(props::lang(*doc), "");
}

TEST(props, fonts_of_all_pages_with_type_embedding_and_encoding) {
    OT_FEATURE("view-45");
    auto cyr = Document::openOrThrow(mini(L"cyr-cid.pdf"));
    const std::vector<props::FontInfo> cid = props::fonts(*cyr);
    ASSERT_FALSE(cid.empty());
    EXPECT_EQ(cid[0].type, "Type0 (CIDFontType2)");
    EXPECT_TRUE(cid[0].embedded);
    EXPECT_TRUE(cid[0].subset);
    EXPECT_EQ(cid[0].encoding, "Identity-H");
    EXPECT_NE(cid[0].name.find("Arial"), std::string::npos) << cid[0].name;
    EXPECT_EQ(cid[0].firstPage, 0);

    auto t1 = Document::openOrThrow(mini(L"type1-diff.pdf"));
    const std::vector<props::FontInfo> type1 = props::fonts(*t1);
    ASSERT_FALSE(type1.empty());
    bool helvetica = false;
    for (const props::FontInfo& f : type1) {
        if (f.name == "Helvetica") {
            helvetica = true;
            EXPECT_EQ(f.type, "Type1");
            EXPECT_FALSE(f.embedded);
            EXPECT_FALSE(f.subset);
            EXPECT_EQ(f.encoding, "WinAnsiEncoding + Differences");
        }
    }
    EXPECT_TRUE(helvetica);

    // Шрифт, общий для страниц, — одна строка; кеш отдаёт тот же список, пока номер правки тот же.
    auto many = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 4));
    props::FontCache cache;
    const auto& first = cache.fonts(*many, 7);
    ASSERT_EQ(first.size(), 1u);
    EXPECT_EQ(first[0].name, "Helvetica");
    EXPECT_EQ(first[0].pages, 4);
    EXPECT_EQ(cache.scans(), 1);
    (void)cache.fonts(*many, 7);
    EXPECT_EQ(cache.scans(), 1);
    (void)cache.fonts(*many, 8);
    EXPECT_EQ(cache.scans(), 2);
}

TEST(props, advanced_reports_version_objects_tagged_and_check_warnings) {
    OT_FEATURE("view-45");
    auto doc = Document::openOrThrow(mini(L"blank-a4.pdf"));
    const props::Advanced a = props::advanced(*doc);
    EXPECT_EQ(a.pdfVersion, 17);
    EXPECT_FALSE(a.linearized);
    EXPECT_FALSE(a.tagged);
    EXPECT_GT(a.objects, 3);
    EXPECT_EQ(a.pages, 1);
    EXPECT_GT(a.fileSize, 0u);
    EXPECT_FALSE(a.repaired);
    EXPECT_TRUE(a.warnings.empty());
    EXPECT_EQ(a.lang, "ru-RU");

    auto broken = Document::openOrThrow(mini(L"truncated.pdf"));
    const props::Advanced b = props::advanced(*broken);
    EXPECT_TRUE(b.repaired);

    // /MarkInfo /Marked true — «Документ с тегами».
    auto tagged = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    {
        fz_context* ctx = tagged->context().get();
        Journal::Operation op(*tagged, "Mark");
        ot::mu::call(ctx, [&] {
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, tagged->pdf()), PDF_NAME(Root));
            pdf_obj* mark = pdf_new_dict(ctx, tagged->pdf(), 1);
            pdf_dict_puts_drop(ctx, root, "MarkInfo", mark);  // словарь живёт в каталоге
            pdf_dict_puts_drop(ctx, mark, "Marked", PDF_TRUE);
        });
        op.commit();
    }
    EXPECT_TRUE(props::tagged(*tagged));
}

TEST(props, saved_info_matches_qpdf_json_and_xmp) {
    OT_FEATURE("view-46");
    const ot::QpdfTool qpdf;
    if (!qpdf.available()) {
        GTEST_SKIP() << "qpdf.exe not found";
    }
    ot::test::TempDir dir;
    const fs::path path = dir.path() / L"свойства.pdf";
    ot::paths::writeFile(path, ot::paths::readFile(mini(L"blank-a4.pdf")));
    auto doc = Document::openOrThrow(path);
    ot::DocProperties p = doc->properties();
    p.title = "Сохранённый заголовок";
    p.author = "Автор Авторович";
    doc->setProperties(p);
    ASSERT_TRUE(ot::Saver::save(*doc).ok());
    // qpdf 12 (JSON v2): ключа --json-key=trailer нет — трейлер лежит среди объектов ключа «qpdf».
    const ot::ToolResult objects = qpdf.json(path, "qpdf");
    ASSERT_EQ(objects.exitCode, 0) << objects.output;
    const nlohmann::json o = nlohmann::json::parse(objects.output);
    const nlohmann::json& all = o.at("qpdf").at(1);
    const std::string infoRef = all.at("trailer").at("value").at("/Info").get<std::string>();  // "2 0 R"
    const nlohmann::json& info = all.at("obj:" + infoRef).at("value");
    EXPECT_EQ(info.at("/Title").get<std::string>(), "u:Сохранённый заголовок");
    EXPECT_EQ(info.at("/Author").get<std::string>(), "u:Автор Авторович");
    const xmp::Packet x = xmp::Packet::parse(props::xmpPacket(*doc));
    EXPECT_EQ(x.get(xmp::kDc, "title").value_or(""), "Сохранённый заголовок");
    EXPECT_EQ(x.get(xmp::kDc, "creator").value_or(""), "Автор Авторович");
    EXPECT_EQ(xmp::isoToPdfDate(x.get(xmp::kXmp, "ModifyDate").value_or("")), props::readInfo(*doc).modDate);
}
