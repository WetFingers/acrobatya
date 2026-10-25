// Данные формы (план 6.1, п. 3; эталон 3.6 «Данные», 7.3; forms-35, forms-36, security-36): экспорт и импорт
// значений полей в форматах Acrobat:
//   FDF  — %FDF-1.2, каталог /FDF << /Fields [<< /T (часть) /V … /Kids [...] >>] /F (файл.pdf) >>: имена
//          иерархией /Kids, текст — строкой PDF (UTF-16BE с BOM вне PDFDocEncoding), флажок — именем, список
//          с мультивыбором — массивом;
//   XFDF — <xfdf xmlns="http://ns.adobe.com/xfdf/"><fields><field name="часть"><field …><value>…</value>
//          </field></field></fields><f href="файл.pdf"/></xfdf>;
//   XML  — плоский «Экспорт данных → XML» Acrobat: <fields><часть><часть>значение</часть></часть></fields>,
//   имя,
//          недопустимое в XML, — с атрибутом xfdf:original;
//   TSV/TXT — «Текст (с разделителями табуляции)» Acrobat: первая строка — имена, вторая — значения (UTF-8 с
//   BOM,
//          CRLF, кавычки у значений с табуляцией, переводом строки и кавычкой); при импорте — и пары «имя →
//          значение» построчно; UTF-16 с BOM тоже читается.
// Экспорт — все поля со значением, кроме кнопок, подписей и /Ff NoExport (как у Acrobat); значения флажков и
// переключателей — имена состояний. Импорт — по полному имени одной операцией журнала «Import form data»
// (core/forms/fields.h setMany: флажки — по имени состояния или экспортному значению); неизвестные поля,
// только для чтения и отказы — в отчёте. FDF/XFDF с /F на другой документ — отказ (эталон 7.3).
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ot {
class Document;
}

namespace ot::forms {

enum class DataFormat { Fdf, Xfdf, Xml, Tsv };
const char* dataFormatName(DataFormat format) noexcept;  // "fdf", "xfdf", "xml", "txt"
// По расширению файла (".fdf", ".xfdf", ".xml", ".txt", ".tsv"; без учёта регистра); незнакомое — nullopt.
std::optional<DataFormat> dataFormatFromExtension(std::string_view extension);
// По содержимому: «%FDF» — FDF, XML с <xfdf — XFDF, иной XML — XML, прочее — TSV.
DataFormat detectDataFormat(std::string_view bytes);

class DataError : public std::runtime_error {
public:
    enum class Kind { Malformed, ForeignDocument };
    DataError(Kind kind, const std::string& message, std::string file = {})
        : std::runtime_error(message), kind_(kind), file_(std::move(file)) {}
    Kind kind() const noexcept { return kind_; }
    const std::string& file() const noexcept { return file_; }  // ForeignDocument — имя документа из файла

private:
    Kind kind_;
    std::string file_;
};

struct FieldValue {
    std::string name;                 // полное имя
    std::vector<std::string> values;  // одно значение; у списка с мультивыбором — несколько
    bool state = false;               // имя состояния флажка или переключателя (FDF — /Name)
};

struct FormData {
    std::vector<FieldValue> fields;
    std::string file;  // документ (FDF /F, XFDF <f href>); пусто — нет
    // TSV, где в каждой строке две ячейки, — ещё и пары «имя → значение» построчно: какое прочтение верное,
    // решает импорт (больше известных полей).
    std::vector<FieldValue> pairs;
};

// Значения документа для экспорта; fileName — /F и <f href> (имя PDF без пути).
FormData collectData(const Document& doc, const std::string& fileName);
std::string writeData(const FormData& data, DataFormat format);
// Разобрать файл данных; испорчен — DataError Malformed.
FormData readData(std::string_view bytes, DataFormat format);

struct DataImportReport {
    std::vector<std::string> updated;
    std::vector<std::string> unknown;
    std::vector<std::string> readOnly;
    std::vector<std::string> rejected;
    std::vector<int> pages;  // страницы, вид которых изменился
    int total() const noexcept {
        return static_cast<int>(updated.size() + unknown.size() + readOnly.size() + rejected.size());
    }
};

// Импорт значений (операция «Import form data»). documentFile — имя файла документа для проверки /F (пусто —
// без проверки); чужой — DataError ForeignDocument, документ не меняется.
DataImportReport importData(Document& doc, const FormData& data, const std::string& documentFile);

}  // namespace ot::forms
