// unit.form_data_io — данные формы (план 6.1, п. 3; эталон 3.6 «Данные», 7.3; forms-35, forms-36): значения
// tests/fixtures/forms/forms-all.pdf с кириллицей, многострочным текстом с табуляцией, флажком,
// переключателем и списком с мультивыбором проходят круг FDF / XFDF / XML / TSV (запись → чтение → импорт в
// чистую копию); иерархические имена (xfa-stub.pdf «form1[0].page1[0].name[0]») — /Kids в FDF и вложенные
// <field> в XFDF; неизвестное поле, поле только для чтения и кнопка — строки отчёта; FDF с /F на другой
// документ — отказ; TSV парами «имя → значение» и в UTF-16; формат — по содержимому и расширению.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/forms/data_io.h"
#include "core/forms/fields.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
namespace forms = ot::forms;
using forms::DataFormat;
using ot::Document;

namespace {

fs::path fixture(const char* name) { return ot::test::sourceDir() / "tests" / "fixtures" / "forms" / name; }

std::unique_ptr<Document> openForms() { return Document::openOrThrow(fixture("forms-all.pdf")); }

const std::string kName = "Привет, мир — ёж";
const std::string kComment = "строка 1\nстрока 2\tс табуляцией и \"кавычками\"";

void fill(Document& doc) {
    ASSERT_TRUE(forms::setValue(doc, "name", kName).accepted);
    ASSERT_TRUE(forms::setValue(doc, "comment", kComment).accepted);
    ASSERT_TRUE(forms::setValue(doc, "agree", "Off").accepted);
    ASSERT_TRUE(forms::setValue(doc, "color", "blue").accepted);
    ASSERT_TRUE(forms::setValues(doc, "langs", {"en", "de"}).accepted);
    ASSERT_TRUE(forms::setValue(doc, "country", "Италия").accepted);
}

}  // namespace

TEST(form_data_io, round_trip_all_formats) {
    OT_FEATURE("forms-35");
    OT_FEATURE("forms-36");
    auto source = openForms();
    fill(*source);
    const forms::FormData data = forms::collectData(*source, "forms-all.pdf");
    // Кнопки, подпись — не данные.
    for (const forms::FieldValue& v : data.fields) {
        EXPECT_NE(v.name, "reset");
        EXPECT_NE(v.name, "sig");
        EXPECT_NE(v.name, "submit_http");
    }
    for (const DataFormat format : {DataFormat::Fdf, DataFormat::Xfdf, DataFormat::Xml, DataFormat::Tsv}) {
        SCOPED_TRACE(forms::dataFormatName(format));
        const std::string bytes = forms::writeData(data, format);
        ASSERT_FALSE(bytes.empty());
        EXPECT_EQ(forms::detectDataFormat(bytes), format);
        const forms::FormData parsed = forms::readData(bytes, format);
        auto target = openForms();
        const forms::DataImportReport report = forms::importData(*target, parsed, "forms-all.pdf");
        EXPECT_TRUE(report.unknown.empty());
        EXPECT_TRUE(report.rejected.empty()) << (report.rejected.empty() ? "" : report.rejected.front());
        const forms::Form form = forms::readForm(*target);
        EXPECT_EQ(form.find("name")->value, kName);
        EXPECT_EQ(form.find("comment")->value, kComment);
        EXPECT_EQ(form.find("comment_ru")->value, "Привет, мир");
        EXPECT_FALSE(form.find("agree")->checked());
        EXPECT_EQ(form.find("color")->value, "blue");
        EXPECT_EQ(form.find("langs")->values, std::vector<std::string>({"en", "de"}));
        EXPECT_EQ(form.find("country")->value, "Италия");
        EXPECT_EQ(form.find("total")->value, "2000");  // только для чтения — не тронуто
    }
    // FDF — %FDF-1.2 и /F; XFDF — пространство имён Adobe и href.
    const std::string fdf = forms::writeData(data, DataFormat::Fdf);
    EXPECT_EQ(fdf.substr(0, 8), "%FDF-1.2");
    const std::string xfdf = forms::writeData(data, DataFormat::Xfdf);
    EXPECT_NE(xfdf.find("xmlns=\"http://ns.adobe.com/xfdf/\""), std::string::npos);
    EXPECT_NE(xfdf.find("<f href=\"forms-all.pdf\"/>"), std::string::npos);
    EXPECT_EQ(forms::readData(fdf, DataFormat::Fdf).file, "forms-all.pdf");
    EXPECT_EQ(forms::readData(xfdf, DataFormat::Xfdf).file, "forms-all.pdf");
}

TEST(form_data_io, hierarchical_names) {
    OT_FEATURE("forms-35");
    auto doc = Document::openOrThrow(fixture("xfa-stub.pdf"));
    const std::string name = "form1[0].page1[0].name[0]";
    ASSERT_TRUE(forms::setValue(*doc, name, "Пётр").accepted);
    const forms::FormData data = forms::collectData(*doc, "xfa-stub.pdf");
    for (const DataFormat format : {DataFormat::Fdf, DataFormat::Xfdf, DataFormat::Xml, DataFormat::Tsv}) {
        SCOPED_TRACE(forms::dataFormatName(format));
        const std::string bytes = forms::writeData(data, format);
        const forms::FormData parsed = forms::readData(bytes, format);
        bool found = false;
        for (const forms::FieldValue& v : parsed.fields) {
            if (v.name == name) {
                found = true;
                EXPECT_EQ(v.values, std::vector<std::string>({"Пётр"}));
            }
        }
        EXPECT_TRUE(found);
    }
    const std::string fdf = forms::writeData(data, DataFormat::Fdf);
    EXPECT_NE(fdf.find("/Kids"), std::string::npos);
    const std::string xfdf = forms::writeData(data, DataFormat::Xfdf);
    EXPECT_NE(xfdf.find("<field name=\"form1[0]\">"), std::string::npos);
}

TEST(form_data_io, report_unknown_read_only_and_buttons) {
    OT_FEATURE("forms-36");
    auto doc = openForms();
    const std::string xfdf =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xfdf xmlns=\"http://ns.adobe.com/xfdf/\" xml:space=\"preserve\"><fields>"
        "<field name=\"name\"><value>Мария</value></field>"
        "<field name=\"nope\"><value>x</value></field>"
        "<field name=\"total\"><value>1</value></field>"
        "<field name=\"reset\"><value>1</value></field>"
        "<field name=\"city\"><value>Paris</value></field>"
        "<field name=\"agree\"><value>Yes</value></field>"
        "</fields></xfdf>";
    const forms::FormData data = forms::readData(xfdf, forms::detectDataFormat(xfdf));
    ASSERT_EQ(data.fields.size(), 6u);
    const forms::DataImportReport r = forms::importData(*doc, data, "forms-all.pdf");
    EXPECT_EQ(r.updated, std::vector<std::string>({"name", "agree"}));
    EXPECT_EQ(r.unknown, std::vector<std::string>({"nope"}));
    EXPECT_EQ(r.readOnly, std::vector<std::string>({"total"}));
    EXPECT_EQ(r.rejected, std::vector<std::string>({"reset", "city"}));
    EXPECT_EQ(r.total(), 6);
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Мария");
    // Одна операция журнала.
    EXPECT_EQ(doc->undoStack().undoName(), "Import form data");
    doc->undoStack().undo();
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Ivan Petrov");
}

TEST(form_data_io, foreign_document_refused) {
    auto source = openForms();
    const forms::FormData data = forms::collectData(*source, "other.pdf");
    const std::string fdf = forms::writeData(data, DataFormat::Fdf);
    auto doc = openForms();
    try {
        forms::importData(*doc, forms::readData(fdf, DataFormat::Fdf), "forms-all.pdf");
        FAIL() << "foreign FDF imported";
    } catch (const forms::DataError& e) {
        EXPECT_EQ(e.kind(), forms::DataError::Kind::ForeignDocument);
        EXPECT_EQ(e.file(), "other.pdf");
    }
    EXPECT_FALSE(doc->undoStack().canUndo());
    // Без имени документа — без проверки.
    EXPECT_FALSE(forms::importData(*doc, forms::readData(fdf, DataFormat::Fdf), "").updated.empty());
}

TEST(form_data_io, tsv_pairs_and_utf16) {
    auto doc = openForms();
    const std::string pairs = "name\tМария\r\nqty\t5\r\n";
    forms::DataImportReport r = forms::importData(*doc, forms::readData(pairs, DataFormat::Tsv), "");
    EXPECT_EQ(r.updated, std::vector<std::string>({"name", "qty"}));
    EXPECT_EQ(forms::readField(*doc, "qty")->value, "5");
    // UTF-16LE с BOM (так пишут некоторые программы): «name\tЁж\r\n…» по горизонтали.
    const std::u16string text = u"name\tqty\r\nЁж\t7\r\n";
    std::string utf16 = "\xFF\xFE";
    for (const char16_t c : text) {
        utf16.push_back(static_cast<char>(c & 0xFF));
        utf16.push_back(static_cast<char>(c >> 8));
    }
    r = forms::importData(*doc, forms::readData(utf16, DataFormat::Tsv), "");
    EXPECT_EQ(r.updated, std::vector<std::string>({"name", "qty"}));
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Ёж");
    EXPECT_EQ(forms::readField(*doc, "qty")->value, "7");
}

TEST(form_data_io, formats_by_content_and_extension) {
    EXPECT_EQ(forms::detectDataFormat("%FDF-1.2\n..."), DataFormat::Fdf);
    EXPECT_EQ(forms::detectDataFormat("\xEF\xBB\xBF<?xml version=\"1.0\"?><xfdf xmlns=\"x\"/>"),
              DataFormat::Xfdf);
    EXPECT_EQ(forms::detectDataFormat("<?xml version=\"1.0\"?>\n<fields><a>1</a></fields>"), DataFormat::Xml);
    EXPECT_EQ(forms::detectDataFormat("a\tb\r\n1\t2\r\n"), DataFormat::Tsv);
    EXPECT_EQ(forms::dataFormatFromExtension(".XFDF"), DataFormat::Xfdf);
    EXPECT_EQ(forms::dataFormatFromExtension("txt"), DataFormat::Tsv);
    EXPECT_EQ(forms::dataFormatFromExtension(".tsv"), DataFormat::Tsv);
    EXPECT_FALSE(forms::dataFormatFromExtension(".pdf").has_value());
    EXPECT_THROW(forms::readData("%FDF-1.2 broken", DataFormat::Fdf), forms::DataError);
    EXPECT_THROW(forms::readData("<xfdf><fields><field", DataFormat::Xfdf), forms::DataError);
}
