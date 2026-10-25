// unit.fields — модель полей формы (план 6.1, п. 1; эталон 3.6; forms-01, forms-06): поля фикстуры
// tests/fixtures/forms/forms-all.pdf (ожидания forms.json генератора tools/gen/make_forms.py) — типы, флаги,
// значения, /DV, подсказки с кириллицей, варианты, виджеты переключателя с экспортными значениями, /CO,
// /Tabs; текстовое поле принимает «Привет» (встроенный TrueType в /DA и /DR вместо невстроенного «HelvCYR»),
// переживает инкрементальную запись и читается qpdf --json --json-key=acroform; флажок и переключатель
// переключаются; combo и список выбирают, редактируемый combo принимает своё; только для чтения — отказ;
// сброс возвращает /DV; Tab идёт по рядам (/Tabs /R), а не по /Annots; без JavaScript формат и вычисления не
// идут; undo возвращает значение; XFA-заглушка — признак и AcroForm-часть.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/forms/fields.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
namespace forms = ot::forms;
using forms::FieldType;
using ot::Document;

namespace {

fs::path fixtures() { return ot::test::sourceDir() / "tests" / "fixtures" / "forms"; }

const nlohmann::json& expected() {
    static const nlohmann::json all = [] {
        std::ifstream in(fixtures() / "forms.json", std::ios::binary);
        return nlohmann::json::parse(in);
    }();
    return all;
}

std::unique_ptr<Document> openForms() { return Document::openOrThrow(fixtures() / "forms-all.pdf"); }

FieldType typeOf(const nlohmann::json& f) {
    const std::string t = f["type"].get<std::string>();
    const int flags = f["ff"].get<int>();
    if (t == "Tx") {
        return FieldType::Text;
    }
    if (t == "Sig") {
        return FieldType::Signature;
    }
    if (t == "Ch") {
        return (flags & forms::ff::kCombo) != 0 ? FieldType::ComboBox : FieldType::ListBox;
    }
    if ((flags & forms::ff::kPushButton) != 0) {
        return FieldType::PushButton;
    }
    return (flags & forms::ff::kRadio) != 0 ? FieldType::RadioButton : FieldType::CheckBox;
}

const forms::Widget& widgetOf(const forms::Field& f, std::size_t i = 0) { return f.widgets.at(i); }

// /DA виджета и подтип шрифта ресурса /DA в /AcroForm /DR.
struct DaInfo {
    std::string da;
    std::string fontSubtype;
};

DaInfo daOf(const Document& doc, int objnum) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    DaInfo out;
    char da[256] = {};
    char font[64] = {};
    char subtype[64] = {};
    ot::mu::call(ctx, [&] {
        pdf_obj* w = pdf_load_object(ctx, pdf, objnum);
        const char* s = pdf_dict_get_text_string(ctx, w, PDF_NAME(DA));
        fz_strlcpy(da, s, sizeof da);
        float size = 0;
        int n = 0;
        float color[4] = {};
        pdf_parse_default_appearance_unmapped(ctx, s, font, sizeof font, &size, &n, color);
        pdf_obj* f =
            pdf_dict_gets(ctx, pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/DR/Font"), font);
        fz_strlcpy(subtype, pdf_dict_get_name(ctx, f, PDF_NAME(Subtype)), sizeof subtype);
        pdf_drop_obj(ctx, w);
    });
    out.da = da;
    out.fontSubtype = subtype;
    return out;
}

}  // namespace

TEST(fields, read_forms_all) {
    OT_FEATURE("forms-01");
    const auto doc = openForms();
    const forms::Form form = forms::readForm(*doc);
    const nlohmann::json& want = expected()["forms-all.pdf"];
    ASSERT_EQ(form.fields.size(), want["fields"].size());
    EXPECT_FALSE(form.xfa);
    EXPECT_FALSE(form.needAppearances);
    EXPECT_TRUE(form.javaScript);
    EXPECT_EQ(form.calculationOrder, std::vector<std::string>({"total"}));
    for (const nlohmann::json& f : want["fields"]) {
        const std::string name = f["name"].get<std::string>();
        SCOPED_TRACE(name);
        const forms::Field* field = form.find(name);
        ASSERT_NE(field, nullptr);
        EXPECT_EQ(field->type, typeOf(f));
        EXPECT_EQ(field->flags, f["ff"].get<int>());
        EXPECT_EQ(field->tooltip, f["tooltip"].get<std::string>());
        if (f.contains("value")) {
            if (f["value"].is_array()) {
                EXPECT_EQ(field->values, f["value"].get<std::vector<std::string>>());
            } else {
                EXPECT_EQ(field->value, f["value"].get<std::string>());
            }
        }
        if (f.contains("dv") && f["dv"].is_string()) {
            EXPECT_EQ(field->defaultValue, f["dv"].get<std::string>());
        }
        if (f.contains("maxlen")) {
            EXPECT_EQ(field->maxLen, f["maxlen"].get<int>());
            EXPECT_TRUE(field->comb());
        }
        if (f.contains("options")) {
            ASSERT_EQ(field->options.size(), f["options"].size());
            for (std::size_t i = 0; i < field->options.size(); ++i) {
                const nlohmann::json& o = f["options"][i];
                if (o.is_array()) {
                    EXPECT_EQ(field->options[i].exportValue, o[0].get<std::string>());
                    EXPECT_EQ(field->options[i].label, o[1].get<std::string>());
                } else {
                    EXPECT_EQ(field->options[i].exportValue, o.get<std::string>());
                    EXPECT_EQ(field->options[i].label, o.get<std::string>());
                }
            }
        }
        const std::size_t widgets = f.contains("widgets") ? f["widgets"].get<std::size_t>() : 1;
        ASSERT_EQ(field->widgets.size(), widgets);
        for (const forms::Widget& w : field->widgets) {
            EXPECT_EQ(w.page, 0);
            EXPECT_GT(w.rect.width(), 0.0f);
            EXPECT_GT(w.rect.height(), 0.0f);
        }
        if (f.contains("exports")) {
            for (std::size_t i = 0; i < widgets; ++i) {
                EXPECT_EQ(field->widgets[i].exportValue, f["exports"][i].get<std::string>());
            }
        }
        if (f.contains("action")) {
            EXPECT_STREQ(forms::actionKindName(field->action.kind), f["action"].get<std::string>().c_str());
        }
        if (f.contains("submit_url")) {
            EXPECT_EQ(field->action.target, f["submit_url"].get<std::string>());
            EXPECT_EQ(field->action.flags, f["submit_flags"].get<int>());
        }
        if (f.contains("js")) {
            const nlohmann::json& js = f["js"];
            EXPECT_EQ(field->formatScript, js.value("F", ""));
            EXPECT_EQ(field->keystrokeScript, js.value("K", ""));
            EXPECT_EQ(field->validateScript, js.value("V", ""));
            EXPECT_EQ(field->calculateScript, js.value("C", ""));
        }
    }
    EXPECT_EQ(form.find("a")->format, forms::TextFormat::Number);
    EXPECT_EQ(form.find("date")->format, forms::TextFormat::Date);
    EXPECT_EQ(form.find("name")->style.font, "Helv");
    EXPECT_FLOAT_EQ(form.find("name")->style.size, 10.0f);
    EXPECT_TRUE(form.find("total")->readOnly());
    EXPECT_TRUE(form.find("agree")->checked());
    EXPECT_EQ(form.find("color")->value, "green");
    EXPECT_TRUE(form.find("color")->widgets[1].on);
    EXPECT_FALSE(form.find("color")->widgets[0].on);
    EXPECT_EQ(form.find("reset")->widgets[0].caption, "Reset");
    EXPECT_TRUE(form.find("country")->editable());
    EXPECT_TRUE(form.find("langs")->multiSelect());
    // Виджет → поле.
    const forms::Widget& kid = form.find("color")->widgets[2];
    ASSERT_NE(form.fieldOf(kid.page, kid.objnum), nullptr);
    EXPECT_EQ(form.fieldOf(kid.page, kid.objnum)->name, "color");
    EXPECT_EQ(forms::pageWidgets(*doc, 0).size(), 22u);
}

TEST(fields, text_cyrillic_saved_incrementally) {
    OT_FEATURE("forms-01");
    const ot::test::TempDir dir;
    const fs::path file = dir.path() / "форма.pdf";
    fs::copy_file(fixtures() / "forms-all.pdf", file);
    auto doc = Document::openOrThrow(file);
    const forms::SetResult r = forms::setValue(*doc, "name", "Привет");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(r.value, "Привет");
    EXPECT_EQ(r.pages, std::vector<int>({0}));
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Привет");
    // /DA — встроенный TrueType (Type0 в /DR), не невстроенный Helvetica.
    const int objnum = forms::readField(*doc, "name")->widgets[0].objnum;
    const DaInfo da = daOf(*doc, objnum);
    EXPECT_EQ(da.fontSubtype, "Type0") << da.da;
    EXPECT_NE(da.da.find(" 10 Tf"), std::string::npos) << da.da;  // кегль поля прежний

    const ot::SaveReport saved = ot::Saver::save(*doc);
    ASSERT_TRUE(saved.ok()) << saved.message;
    EXPECT_EQ(saved.mode, ot::SaveMode::Incremental);
    doc.reset();
    auto again = Document::openOrThrow(file);
    EXPECT_EQ(forms::readField(*again, "name")->value, "Привет");
    EXPECT_EQ(daOf(*again, objnum).fontSubtype, "Type0");

    const ot::QpdfTool qpdf;
    if (!qpdf.available()) {
        GTEST_SKIP() << "qpdf.exe not found";
    }
    const ot::ToolResult json = qpdf.json(file, "acroform");
    ASSERT_TRUE(json.ok()) << json.output;
    const nlohmann::json parsed = nlohmann::json::parse(json.output);
    bool found = false;
    for (const nlohmann::json& f : parsed["acroform"]["fields"]) {
        if (f["fullname"] == "name") {
            EXPECT_EQ(f["value"], "u:Привет");
            found = true;
        }
    }
    EXPECT_TRUE(found) << json.output.substr(0, 2000);
    EXPECT_EQ(qpdf.check(file).exitCode, 0);
}

TEST(fields, checkbox_and_radio_toggle) {
    OT_FEATURE("forms-01");
    auto doc = openForms();
    forms::SetResult r = forms::setValue(*doc, "agree", "Off");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_FALSE(forms::readField(*doc, "agree")->checked());
    const forms::Widget agree = widgetOf(*forms::readField(*doc, "agree"));
    r = forms::toggle(*doc, agree.page, agree.objnum);
    ASSERT_TRUE(r.accepted);
    EXPECT_TRUE(forms::readField(*doc, "agree")->checked());
    EXPECT_EQ(forms::readField(*doc, "agree")->value, "Yes");
    EXPECT_EQ(r.pages, std::vector<int>({0}));

    r = forms::setValue(*doc, "color", "blue");
    ASSERT_TRUE(r.accepted) << r.error;
    forms::Field color = *forms::readField(*doc, "color");
    EXPECT_EQ(color.value, "blue");
    EXPECT_FALSE(color.widgets[0].on);
    EXPECT_FALSE(color.widgets[1].on);
    EXPECT_TRUE(color.widgets[2].on);
    r = forms::toggle(*doc, color.widgets[0].page, color.widgets[0].objnum);
    ASSERT_TRUE(r.accepted);
    color = *forms::readField(*doc, "color");
    EXPECT_EQ(color.value, "red");
    EXPECT_TRUE(color.widgets[0].on);
    EXPECT_FALSE(color.widgets[2].on);
    // Неизвестное состояние — отказ.
    r = forms::setValue(*doc, "color", "purple");
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.error, "not-an-option");
    EXPECT_EQ(forms::readField(*doc, "color")->value, "red");
}

TEST(fields, choice_values) {
    OT_FEATURE("forms-01");
    auto doc = openForms();
    forms::SetResult r = forms::setValue(*doc, "city", "Sochi");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(forms::readField(*doc, "city")->value, "Sochi");
    // Не редактируемый combo не принимает своё.
    r = forms::setValue(*doc, "city", "Paris");
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.error, "not-an-option");
    EXPECT_EQ(forms::readField(*doc, "city")->value, "Sochi");
    // Редактируемый — принимает.
    r = forms::setValue(*doc, "country", "Italy");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(forms::readField(*doc, "country")->value, "Italy");
    // Список с мультивыбором: экспортные значения или подписи.
    r = forms::setValues(*doc, "langs", {"en", "German"});
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(forms::readField(*doc, "langs")->values, std::vector<std::string>({"en", "de"}));
    r = forms::setValues(*doc, "langs", {"fr"});
    EXPECT_FALSE(r.accepted);
}

TEST(fields, read_only_and_unknown_rejected) {
    auto doc = openForms();
    forms::SetResult r = forms::setValue(*doc, "total", "5");
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.error, "read-only");
    EXPECT_EQ(forms::readField(*doc, "total")->value, "2000");
    r = forms::setValue(*doc, "nope", "5");
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.error, "unknown-field");
    r = forms::setValue(*doc, "reset", "5");
    EXPECT_FALSE(r.accepted);
    EXPECT_EQ(r.error, "not-a-value-field");
}

TEST(fields, reset_returns_default_values) {
    OT_FEATURE("forms-06");
    auto doc = openForms();
    ASSERT_TRUE(forms::setValue(*doc, "name", "Мария").accepted);
    ASSERT_TRUE(forms::setValue(*doc, "comment", "что-то").accepted);
    ASSERT_TRUE(forms::setValue(*doc, "agree", "Off").accepted);
    ASSERT_TRUE(forms::setValue(*doc, "color", "red").accepted);
    // Сброс одного поля.
    std::vector<int> pages = forms::resetForm(*doc, {"name"});
    EXPECT_EQ(pages, std::vector<int>({0}));
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Ivan Petrov");
    EXPECT_EQ(forms::readField(*doc, "comment")->value, "что-то");
    // Всё: /DV или пусто.
    forms::resetForm(*doc);
    const forms::Form form = forms::readForm(*doc);
    EXPECT_EQ(form.find("comment")->value, "");
    EXPECT_TRUE(form.find("agree")->checked());
    EXPECT_EQ(form.find("color")->value, "green");
    EXPECT_TRUE(form.find("color")->widgets[1].on);
    EXPECT_FALSE(form.find("color")->widgets[0].on);
}

TEST(fields, tab_order_by_rows) {
    auto doc = openForms();
    EXPECT_EQ(forms::tabOrder(*doc, 0), forms::TabOrder::Row);
    const forms::Form form = forms::readForm(*doc);
    std::vector<std::string> names;
    for (const int objnum : forms::tabSequence(*doc, 0)) {
        const forms::Field* f = form.fieldOf(0, objnum);
        ASSERT_NE(f, nullptr);
        names.push_back(f->name);
    }
    // Ряды сверху вниз: «Языки» (y 280) раньше «Страны» (y 154), хотя в /Annots страна идёт перед ними;
    // «Итого» только для чтения — без остановки; переключатель — одна остановка (выбранный виджет).
    const std::vector<std::string> want = {"name",    "comment", "comment_ru",  "date",       "a",
                                           "b",       "secret",  "pin",         "agree",      "color",
                                           "city",    "langs",   "reset",       "hello",      "sig",
                                           "country", "qty",     "submit_mail", "submit_http"};
    EXPECT_EQ(names, want);

    // Чистая сортировка: ряды и столбцы.
    const std::vector<std::pair<int, ot::RectF>> w = {
        {1, ot::RectF{200, 100, 260, 120}}, {2, ot::RectF{10, 102, 60, 118}}, {3, ot::RectF{10, 10, 60, 30}}};
    EXPECT_EQ(forms::orderWidgets(w, forms::TabOrder::Row), std::vector<int>({3, 2, 1}));
    EXPECT_EQ(forms::orderWidgets(w, forms::TabOrder::Column), std::vector<int>({3, 2, 1}));
    const std::vector<std::pair<int, ot::RectF>> cols = {
        {1, ot::RectF{10, 100, 60, 120}}, {2, ot::RectF{200, 10, 260, 30}}, {3, ot::RectF{12, 10, 60, 30}}};
    EXPECT_EQ(forms::orderWidgets(cols, forms::TabOrder::Column), std::vector<int>({3, 1, 2}));
    EXPECT_EQ(forms::orderWidgets(cols, forms::TabOrder::Row), std::vector<int>({3, 2, 1}));
    EXPECT_EQ(forms::orderWidgets(cols, forms::TabOrder::Annots), std::vector<int>({1, 2, 3}));
}

TEST(fields, without_javascript_no_format_no_calculation) {
    auto doc = openForms();
    const forms::SetResult r = forms::setValue(*doc, "a", "1234.5");
    ASSERT_TRUE(r.accepted) << r.error;
    EXPECT_EQ(forms::formattedValue(*doc, "a"), "1234.5");
    EXPECT_EQ(forms::readField(*doc, "total")->value, "2000");  // вычисление не шло
    EXPECT_TRUE(forms::recalculate(*doc).empty());
    // Проверка сценарием тоже не идёт: «abc» принимается.
    EXPECT_TRUE(forms::setValue(*doc, "qty", "abc").accepted);
}

TEST(fields, undo_restores_value) {
    auto doc = openForms();
    {
        ot::Journal::Operation op(*doc, "Fill field");
        ASSERT_TRUE(forms::setValue(*doc, "name", "Мария").accepted);
        op.commit();
    }
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Мария");
    ASSERT_TRUE(doc->undoStack().canUndo());
    EXPECT_EQ(doc->undoStack().undoName(), "Fill field");
    doc->undoStack().undo();
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Ivan Petrov");
    doc->undoStack().redo();
    EXPECT_EQ(forms::readField(*doc, "name")->value, "Мария");
}

TEST(fields, password_field_hides_text) {
    auto doc = openForms();
    ASSERT_TRUE(forms::setValue(*doc, "secret", "s3cret").accepted);
    // Вид — шесть одинаковых глифов («*» встроенного шрифта), не сам пароль (синтез MuPDF писал бы его
    // открыто).
    const int objnum = forms::readField(*doc, "secret")->widgets[0].objnum;
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    ot::mu::BufferHandle buf(ctx);
    ot::mu::call(ctx, [&] {
        pdf_obj* w = pdf_load_object(ctx, pdf, objnum);
        buf.reset(pdf_load_stream(ctx, pdf_dict_getp(ctx, w, "AP/N")));
        pdf_drop_obj(ctx, w);
    });
    const std::string content(reinterpret_cast<const char*>(buf->data), buf->len);
    EXPECT_EQ(content.find("s3cret"), std::string::npos) << content;
    const std::size_t open = content.find('<');
    const std::size_t close = content.find("> Tj");
    ASSERT_NE(open, std::string::npos) << content;
    ASSERT_NE(close, std::string::npos) << content;
    const std::string hex = content.substr(open + 1, close - open - 1);
    ASSERT_EQ(hex.size(), 24u) << content;
    for (std::size_t i = 4; i < hex.size(); i += 4) {
        EXPECT_EQ(hex.substr(i, 4), hex.substr(0, 4));
    }
    EXPECT_EQ(daOf(*doc, objnum).fontSubtype, "Type0");
}

TEST(fields, xfa_stub_shows_acroform_part) {
    const auto doc = Document::openOrThrow(fixtures() / "xfa-stub.pdf");
    const forms::Form form = forms::readForm(*doc);
    EXPECT_TRUE(form.xfa);
    ASSERT_FALSE(form.fields.empty());
    EXPECT_NE(form.find("form1[0].page1[0].name[0]"), nullptr);
}
