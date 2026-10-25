// Данные формы: FDF, XFDF, XML, TSV (см. data_io.h).
#include "core/forms/data_io.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <map>
#include <utility>

#include <spdlog/spdlog.h>

#include "core/annot/fdf.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/forms/fields.h"
#include "core/forms/forms_internal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/utf8.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace ot::forms {

namespace data_impl {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string_view withoutBom(std::string_view s) {
    if (s.size() >= 3 && s.substr(0, 3) == "\xEF\xBB\xBF") {
        s.remove_prefix(3);
    }
    return s;
}

std::vector<std::string> splitName(const std::string& name) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t dot = name.find('.', start);
        parts.push_back(name.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    return parts;
}

// Дерево имён «a.b.c» для иерархических форматов (порядок — как в данных).
struct Node {
    std::string part;
    const FieldValue* value = nullptr;
    std::vector<Node> kids;
};

Node buildTree(const std::vector<FieldValue>& fields) {
    Node root;
    for (const FieldValue& f : fields) {
        Node* node = &root;
        for (const std::string& part : splitName(f.name)) {
            auto it = std::find_if(node->kids.begin(), node->kids.end(),
                                   [&part](const Node& n) { return n.part == part; });
            if (it == node->kids.end()) {
                node->kids.push_back(Node{part, nullptr, {}});
                it = node->kids.end() - 1;
            }
            node = &*it;
        }
        node->value = &f;
    }
    return root;
}

std::string join(const std::string& prefix, const std::string& part) {
    if (prefix.empty()) {
        return part;
    }
    return part.empty() ? prefix : prefix + "." + part;
}

// ---------------------------------------------------------------- XML

std::string xmlEscape(std::string_view s, bool attribute) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&':
                out += "&amp;";
                break;
            case '<':
                out += "&lt;";
                break;
            case '>':
                out += "&gt;";
                break;
            case '"':
                out += attribute ? "&quot;" : "\"";
                break;
            case '\r':
                out += "&#13;";
                break;
            case '\n':
                out += attribute ? "&#10;" : "\n";
                break;
            case '\t':
                out += attribute ? "&#9;" : "\t";
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

bool nameStart(unsigned char c) { return std::isalpha(c) != 0 || c == '_' || c >= 0x80; }
bool nameChar(unsigned char c) { return nameStart(c) || std::isdigit(c) != 0 || c == '-' || c == '.'; }

// Имя элемента XML для части имени поля: недопустимые знаки — «_» (оригинал — в xfdf:original).
std::string xmlName(const std::string& part, bool& changed) {
    std::string out;
    changed = false;
    for (std::size_t i = 0; i < part.size(); ++i) {
        const auto c = static_cast<unsigned char>(part[i]);
        const bool ok = i == 0 ? nameStart(c) : nameChar(c);
        if (ok && c != '.') {
            out.push_back(part[i]);
        } else {
            out.push_back('_');
            changed = true;
        }
    }
    if (out.empty() || lower(out).rfind("xml", 0) == 0) {
        out.insert(out.begin(), '_');
        changed = true;
    }
    return out;
}

void writeXfdfNode(std::string& out, const Node& node, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    out += indent + "<field name=\"" + xmlEscape(node.part, true) + "\">";
    if (node.value != nullptr) {
        for (const std::string& v : node.value->values) {
            out += "<value>" + xmlEscape(v, false) + "</value>";
        }
    }
    if (!node.kids.empty()) {
        out += "\n";
        for (const Node& k : node.kids) {
            writeXfdfNode(out, k, depth + 1);
        }
        out += indent;
    }
    out += "</field>\n";
}

void writeXmlNode(std::string& out, const Node& node, int depth) {
    const std::string indent(static_cast<std::size_t>(depth) * 2, ' ');
    bool changed = false;
    const std::string tag = xmlName(node.part, changed);
    const std::string open =
        "<" + tag + (changed ? " xfdf:original=\"" + xmlEscape(node.part, true) + "\"" : std::string()) + ">";
    if (node.kids.empty()) {
        const std::vector<std::string> values = node.value != nullptr && !node.value->values.empty()
                                                    ? node.value->values
                                                    : std::vector<std::string>{""};
        for (const std::string& v : values) {
            out += indent + open + xmlEscape(v, false) + "</" + tag + ">\n";
        }
        return;
    }
    out += indent + open + "\n";
    for (const Node& k : node.kids) {
        writeXmlNode(out, k, depth + 1);
    }
    out += indent + "</" + tag + ">\n";
}

// Разобранный XML MuPDF на время чтения.
class Xml {
public:
    explicit Xml(std::string_view bytes) : ctx_(MuContext::forThisThread().get()), buf_(ctx_) {
        fz_context* ctx = ctx_;
        fz_xml* doc = nullptr;
        const std::string_view text = withoutBom(bytes);
        try {
            mu::call(ctx, [&] {
                buf_.reset(fz_new_buffer_from_copied_data(
                    ctx, reinterpret_cast<const unsigned char*>(text.data()), text.size()));
                doc = fz_parse_xml(ctx, buf_, 0);
            });
        } catch (const MuError& e) {
            throw DataError(DataError::Kind::Malformed, std::string("XML cannot be read: ") + e.what());
        }
        doc_ = doc;
        root_ = doc_ != nullptr ? fz_xml_root(doc_) : nullptr;
    }
    ~Xml() {
        if (doc_ != nullptr) {
            fz_drop_xml(ctx_, doc_);
        }
    }
    Xml(const Xml&) = delete;
    Xml& operator=(const Xml&) = delete;

    fz_xml* root() const noexcept { return root_; }

private:
    fz_context* ctx_;
    mu::BufferHandle buf_;
    fz_xml* doc_ = nullptr;
    fz_xml* root_ = nullptr;
};

// Имя элемента без префикса пространства имён.
std::string localName(fz_xml* node) {
    const char* tag = fz_xml_tag(node);
    if (tag == nullptr) {
        return {};
    }
    const char* colon = std::strrchr(tag, ':');
    return colon != nullptr ? colon + 1 : tag;
}

std::string textOf(fz_xml* node) {
    std::string out;
    for (fz_xml* c = fz_xml_down(node); c != nullptr; c = fz_xml_next(c)) {
        if (fz_xml_tag(c) == nullptr) {
            const char* t = fz_xml_text(c);
            out += t != nullptr ? t : "";
        }
    }
    return out;
}

bool hasElements(fz_xml* node) {
    for (fz_xml* c = fz_xml_down(node); c != nullptr; c = fz_xml_next(c)) {
        if (fz_xml_tag(c) != nullptr) {
            return true;
        }
    }
    return false;
}

void readXfdfFields(fz_xml* parent, const std::string& prefix, std::vector<FieldValue>& out) {
    for (fz_xml* c = fz_xml_down(parent); c != nullptr; c = fz_xml_next(c)) {
        if (localName(c) != "field") {
            continue;
        }
        const char* name = fz_xml_att(c, "name");
        const std::string full = join(prefix, name != nullptr ? name : "");
        FieldValue v;
        v.name = full;
        bool hasValue = false;
        for (fz_xml* k = fz_xml_down(c); k != nullptr; k = fz_xml_next(k)) {
            if (localName(k) == "value") {
                v.values.push_back(textOf(k));
                hasValue = true;
            }
        }
        if (hasValue) {
            out.push_back(std::move(v));
        }
        readXfdfFields(c, full, out);
    }
}

void readXmlFields(fz_xml* parent, const std::string& prefix, std::vector<FieldValue>& out) {
    for (fz_xml* c = fz_xml_down(parent); c != nullptr; c = fz_xml_next(c)) {
        if (fz_xml_tag(c) == nullptr) {
            continue;
        }
        const char* original = fz_xml_att(c, "xfdf:original");
        const std::string part = original != nullptr ? original : localName(c);
        const std::string full = join(prefix, part);
        if (hasElements(c)) {
            readXmlFields(c, full, out);
            continue;
        }
        const std::string value = textOf(c);
        auto it =
            std::find_if(out.begin(), out.end(), [&full](const FieldValue& f) { return f.name == full; });
        if (it != out.end()) {
            it->values.push_back(value);  // повтор элемента — ещё одно значение (список с мультивыбором)
        } else {
            out.push_back(FieldValue{full, {value}, false});
        }
    }
}

// ---------------------------------------------------------------- TSV

std::string tsvCell(const std::string& s) {
    if (s.find_first_of("\t\r\n\"") == std::string::npos) {
        return s;
    }
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"') {
            out += "\"\"";
        } else {
            out.push_back(c);
        }
    }
    return out + "\"";
}

// UTF-16 (BOM) → UTF-8; UTF-8 — без BOM.
std::string tsvText(std::string_view bytes) {
    const bool le = bytes.size() >= 2 && bytes.substr(0, 2) == "\xFF\xFE";
    const bool be = bytes.size() >= 2 && bytes.substr(0, 2) == "\xFE\xFF";
    if (!le && !be) {
        return std::string(withoutBom(bytes));
    }
    std::u32string text;
    for (std::size_t i = 2; i + 1 < bytes.size(); i += 2) {
        const auto lo = static_cast<unsigned char>(bytes[le ? i : i + 1]);
        const auto hi = static_cast<unsigned char>(bytes[le ? i + 1 : i]);
        char32_t unit = static_cast<char32_t>(lo | (hi << 8));
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < bytes.size()) {
            const auto lo2 = static_cast<unsigned char>(bytes[le ? i + 2 : i + 3]);
            const auto hi2 = static_cast<unsigned char>(bytes[le ? i + 3 : i + 2]);
            const auto low = static_cast<char32_t>(lo2 | (hi2 << 8));
            if (low >= 0xDC00 && low <= 0xDFFF) {
                unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        text.push_back(unit);
    }
    return utf8::encode(text);
}

std::vector<std::vector<std::string>> tsvRows(const std::string& text) {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> row;
    std::string cell;
    bool quoted = false;
    bool any = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    cell.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell.push_back(c);
            }
            continue;
        }
        if (c == '"' && cell.empty()) {
            quoted = true;
            any = true;
        } else if (c == '\t') {
            row.push_back(std::move(cell));
            cell.clear();
            any = true;
        } else if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            if (any || !cell.empty()) {
                row.push_back(std::move(cell));
                rows.push_back(std::move(row));
            }
            row.clear();
            cell.clear();
            any = false;
        } else {
            cell.push_back(c);
            any = true;
        }
    }
    if (any || !cell.empty()) {
        row.push_back(std::move(cell));
        rows.push_back(std::move(row));
    }
    return rows;
}

// ---------------------------------------------------------------- FDF

void putValue(fz_context* ctx, pdf_obj* dict, const FieldValue& v) {
    std::vector<const char*> raw;
    for (const std::string& s : v.values) {
        raw.push_back(s.c_str());
    }
    const int n = static_cast<int>(raw.size());
    const char* const* items = raw.data();
    const bool state = v.state;
    mu::call(ctx, [&] {
        if (n == 1 && state) {
            pdf_dict_put_name(ctx, dict, PDF_NAME(V), items[0]);
        } else if (n == 1) {
            pdf_dict_put_text_string(ctx, dict, PDF_NAME(V), items[0]);
        } else if (n > 1) {
            pdf_obj* arr = pdf_dict_put_array(ctx, dict, PDF_NAME(V), n);
            for (int i = 0; i < n; ++i) {
                pdf_array_push_text_string(ctx, arr, items[i]);
            }
        }
    });
}

void writeFdfNode(fz_context* ctx, pdf_document* doc, pdf_obj* array, const Node& node) {
    pdf_obj* dict = nullptr;
    const char* part = node.part.c_str();
    mu::call(ctx, [&] {
        dict = pdf_array_push_dict(ctx, array, 3);
        pdf_dict_put_text_string(ctx, dict, PDF_NAME(T), part);
    });
    if (node.value != nullptr) {
        putValue(ctx, dict, *node.value);
    }
    if (!node.kids.empty()) {
        pdf_obj* kids = nullptr;
        const int n = static_cast<int>(node.kids.size());
        mu::call(ctx, [&] { kids = pdf_dict_put_array(ctx, dict, PDF_NAME(Kids), n); });
        for (const Node& k : node.kids) {
            writeFdfNode(ctx, doc, kids, k);
        }
    }
}

std::string writeFdf(const FormData& data) {
    fz_context* ctx = MuContext::forThisThread().get();
    mu::PdfDocumentHandle doc(ctx);
    mu::BufferHandle buf(ctx);
    mu::OutputHandle out(ctx);
    pdf_obj* fields = nullptr;
    const char* file = data.file.c_str();
    mu::call(ctx, [&] {
        doc.reset(pdf_create_document(ctx));
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
        pdf_dict_del(ctx, root, PDF_NAME(Type));
        pdf_dict_del(ctx, root, PDF_NAME(Pages));
        pdf_dict_puts_drop(ctx, root, "FDF", pdf_new_dict(ctx, doc, 2));
        pdf_obj* fdf = pdf_dict_gets(ctx, root, "FDF");
        fields = pdf_dict_put_array(ctx, fdf, PDF_NAME(Fields), 8);
        if (*file != 0) {
            pdf_dict_put_text_string(ctx, fdf, PDF_NAME(F), file);
        }
    });
    const Node tree = buildTree(data.fields);
    for (const Node& n : tree.kids) {
        writeFdfNode(ctx, doc, fields, n);
    }
    mu::call(ctx, [&] {
        buf.reset(fz_new_buffer(ctx, 4096));
        out.reset(fz_new_output_with_buffer(ctx, buf));
        pdf_write_options opts = pdf_default_write_options;
        opts.do_garbage = 1;
        pdf_write_document(ctx, doc, out, &opts);
        fz_close_output(ctx, out);
    });
    unsigned char* bytes = nullptr;
    const std::size_t len = fz_buffer_storage(ctx, buf, &bytes);
    std::string result(reinterpret_cast<const char*>(bytes), len);
    if (result.size() >= 8 && result.compare(0, 5, "%PDF-") == 0) {
        result.replace(0, 8, "%FDF-1.2");  // та же длина — смещения таблицы ссылок прежние
    }
    return result;
}

void readFdfFields(fz_context* ctx, pdf_obj* array, const std::string& prefix, std::vector<FieldValue>& out,
                   int depth) {
    if (depth > 32) {
        return;
    }
    int n = 0;
    mu::call(ctx, [&] { n = pdf_array_len(ctx, array); });
    for (int i = 0; i < n; ++i) {
        pdf_obj* dict = nullptr;
        const char* t = nullptr;
        pdf_obj* v = nullptr;
        pdf_obj* kids = nullptr;
        int isName = 0;
        int isArray = 0;
        mu::call(ctx, [&] {
            dict = pdf_array_get(ctx, array, i);
            t = pdf_dict_get_text_string(ctx, dict, PDF_NAME(T));
            v = pdf_dict_get(ctx, dict, PDF_NAME(V));
            kids = pdf_dict_get(ctx, dict, PDF_NAME(Kids));
            isName = pdf_is_name(ctx, v);
            isArray = pdf_is_array(ctx, v);
        });
        const std::string full = join(prefix, t != nullptr ? t : "");
        if (v != nullptr) {
            FieldValue fv;
            fv.name = full;
            fv.state = isName != 0;
            if (isArray != 0) {
                int m = 0;
                mu::call(ctx, [&] { m = pdf_array_len(ctx, v); });
                for (int k = 0; k < m; ++k) {
                    pdf_obj* item = nullptr;
                    mu::call(ctx, [&] { item = pdf_array_get(ctx, v, k); });
                    fv.values.push_back(detail::textOf(ctx, item));
                }
            } else {
                fv.values.push_back(detail::textOf(ctx, v));
            }
            out.push_back(std::move(fv));
        }
        if (kids != nullptr) {
            readFdfFields(ctx, kids, full, out, depth + 1);
        }
    }
}

FormData readFdf(std::string_view bytes) {
    if (bytes.substr(0, 5) != "%FDF-") {
        throw DataError(DataError::Kind::Malformed, "not an FDF file");
    }
    fz_context* ctx = MuContext::forThisThread().get();
    mu::BufferHandle buffer(ctx);
    mu::StreamHandle stream(ctx);
    mu::PdfDocumentHandle doc(ctx);
    pdf_obj* fields = nullptr;
    pdf_obj* file = nullptr;
    int ok = 0;
    try {
        mu::call(ctx, [&] {
            buffer.reset(fz_new_buffer_from_copied_data(
                ctx, reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size()));
            stream.reset(fz_open_buffer(ctx, buffer));
            doc.reset(pdf_open_document_with_stream(ctx, stream));
            pdf_obj* fdf = pdf_dict_getp(ctx, pdf_trailer(ctx, doc), "Root/FDF");
            ok = pdf_is_dict(ctx, fdf);
            fields = pdf_dict_get(ctx, fdf, PDF_NAME(Fields));
            file = pdf_dict_get(ctx, fdf, PDF_NAME(F));
            if (pdf_is_dict(ctx, file)) {
                pdf_obj* uf = pdf_dict_get(ctx, file, PDF_NAME(UF));
                file = uf != nullptr ? uf : pdf_dict_get(ctx, file, PDF_NAME(F));
            }
        });
    } catch (const MuError& e) {
        throw DataError(DataError::Kind::Malformed, std::string("FDF cannot be read: ") + e.what());
    }
    if (ok == 0) {
        throw DataError(DataError::Kind::Malformed, "FDF has no /FDF dictionary");
    }
    FormData data;
    data.file = detail::textOf(ctx, file);
    readFdfFields(ctx, fields, std::string(), data.fields, 0);
    return data;
}

}  // namespace data_impl

const char* dataFormatName(DataFormat format) noexcept {
    switch (format) {
        case DataFormat::Fdf:
            return "fdf";
        case DataFormat::Xfdf:
            return "xfdf";
        case DataFormat::Xml:
            return "xml";
        case DataFormat::Tsv:
            break;
    }
    return "txt";
}

std::optional<DataFormat> dataFormatFromExtension(std::string_view extension) {
    std::string e = data_impl::lower(extension);
    if (!e.empty() && e.front() == '.') {
        e.erase(e.begin());
    }
    if (e == "fdf") {
        return DataFormat::Fdf;
    }
    if (e == "xfdf") {
        return DataFormat::Xfdf;
    }
    if (e == "xml") {
        return DataFormat::Xml;
    }
    if (e == "txt" || e == "tsv") {
        return DataFormat::Tsv;
    }
    return std::nullopt;
}

DataFormat detectDataFormat(std::string_view bytes) {
    const std::string_view s = data_impl::withoutBom(bytes);
    if (s.substr(0, 5) == "%FDF-") {
        return DataFormat::Fdf;
    }
    const std::size_t first = s.find_first_not_of(" \t\r\n");
    if (first != std::string_view::npos && s[first] == '<') {
        const std::string head = data_impl::lower(s.substr(0, std::min<std::size_t>(s.size(), 512)));
        return head.find("<xfdf") != std::string::npos ? DataFormat::Xfdf : DataFormat::Xml;
    }
    return DataFormat::Tsv;
}

FormData collectData(const Document& doc, const std::string& fileName) {
    FormData data;
    data.file = fileName;
    for (const Field& f : readForm(doc).fields) {
        if (f.type == FieldType::PushButton || f.type == FieldType::Signature ||
            f.type == FieldType::Unknown || f.noExport() || f.password()) {
            continue;
        }
        FieldValue v;
        v.name = f.name;
        v.state = f.type == FieldType::CheckBox || f.type == FieldType::RadioButton;
        if (v.state) {
            v.values = {f.value.empty() ? std::string("Off") : f.value};
        } else if (f.type == FieldType::ListBox && f.values.size() != 1) {
            v.values = f.values;
        } else {
            v.values = {f.value};
        }
        if (v.values.empty()) {
            v.values = {std::string()};
        }
        data.fields.push_back(std::move(v));
    }
    return data;
}

std::string writeData(const FormData& data, DataFormat format) {
    using namespace data_impl;
    switch (format) {
        case DataFormat::Fdf:
            return writeFdf(data);
        case DataFormat::Xfdf: {
            std::string out =
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<xfdf xmlns=\"http://ns.adobe.com/xfdf/\" xml:space=\"preserve\">\n<fields>\n";
            for (const Node& n : buildTree(data.fields).kids) {
                writeXfdfNode(out, n, 1);
            }
            out += "</fields>\n";
            if (!data.file.empty()) {
                out += "<f href=\"" + xmlEscape(data.file, true) + "\"/>\n";
            }
            return out + "</xfdf>\n";
        }
        case DataFormat::Xml: {
            std::string out =
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                "<fields xmlns:xfdf=\"http://ns.adobe.com/xfdf-transition/\">\n";
            for (const Node& n : buildTree(data.fields).kids) {
                writeXmlNode(out, n, 1);
            }
            return out + "</fields>\n";
        }
        case DataFormat::Tsv:
            break;
    }
    std::string names;
    std::string values;
    for (std::size_t i = 0; i < data.fields.size(); ++i) {
        const FieldValue& f = data.fields[i];
        std::string joined;
        for (std::size_t k = 0; k < f.values.size(); ++k) {
            joined += (k > 0 ? "\n" : "") + f.values[k];  // список с мультивыбором — построчно в ячейке
        }
        names += (i > 0 ? "\t" : "") + tsvCell(f.name);
        values += (i > 0 ? "\t" : "") + tsvCell(joined);
    }
    return "\xEF\xBB\xBF" + names + "\r\n" + values + "\r\n";
}

FormData readData(std::string_view bytes, DataFormat format) {
    using namespace data_impl;
    FormData data;
    switch (format) {
        case DataFormat::Fdf:
            return readFdf(bytes);
        case DataFormat::Xfdf: {
            const Xml xml(bytes);
            if (xml.root() == nullptr || localName(xml.root()) != "xfdf") {
                throw DataError(DataError::Kind::Malformed, "not an XFDF document");
            }
            bool fieldsFound = false;
            for (fz_xml* c = fz_xml_down(xml.root()); c != nullptr; c = fz_xml_next(c)) {
                const std::string tag = localName(c);
                if (tag == "fields") {
                    fieldsFound = true;
                    readXfdfFields(c, std::string(), data.fields);
                } else if (tag == "f") {
                    const char* href = fz_xml_att(c, "href");
                    data.file = href != nullptr ? href : "";
                }
            }
            if (!fieldsFound && data.file.empty()) {
                throw DataError(DataError::Kind::Malformed, "XFDF has no <fields>");
            }
            return data;
        }
        case DataFormat::Xml: {
            const Xml xml(bytes);
            if (xml.root() == nullptr) {
                throw DataError(DataError::Kind::Malformed, "XML has no root element");
            }
            readXmlFields(xml.root(), std::string(), data.fields);
            return data;
        }
        case DataFormat::Tsv:
            break;
    }
    const std::vector<std::vector<std::string>> rows = tsvRows(tsvText(bytes));
    if (rows.size() >= 2) {
        const std::vector<std::string>& names = rows[0];
        const std::vector<std::string>& values = rows[1];
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (!names[i].empty()) {
                data.fields.push_back(
                    FieldValue{names[i], {i < values.size() ? values[i] : std::string()}, false});
            }
        }
    }
    const bool pairs =
        !rows.empty() && std::all_of(rows.begin(), rows.end(), [](const auto& r) { return r.size() == 2; });
    if (pairs) {
        for (const auto& r : rows) {
            if (!r[0].empty()) {
                data.pairs.push_back(FieldValue{r[0], {r[1]}, false});
            }
        }
    }
    return data;
}

DataImportReport importData(Document& doc, const FormData& data, const std::string& documentFile) {
    if (!documentFile.empty() && !data.file.empty() && !annot::sameDocumentName(data.file, documentFile)) {
        throw DataError(DataError::Kind::ForeignDocument,
                        "form data belongs to another document: " + data.file, data.file);
    }
    const Form form = readForm(doc);
    const auto known = [&form](const std::vector<FieldValue>& list) {
        return std::count_if(list.begin(), list.end(),
                             [&form](const FieldValue& v) { return form.find(v.name) != nullptr; });
    };
    const std::vector<FieldValue>& chosen =
        !data.pairs.empty() && known(data.pairs) > known(data.fields) ? data.pairs : data.fields;
    std::vector<Assignment> list;
    list.reserve(chosen.size());
    for (const FieldValue& v : chosen) {
        Assignment a{v.name, v.values};
        const Field* f = form.find(v.name);
        if (f != nullptr && f->multiSelect() && a.values.size() == 1 &&
            a.values.front().find('\n') != std::string::npos) {
            // TSV: значения списка с мультивыбором — строками ячейки.
            std::vector<std::string> split;
            std::string current;
            for (const char c : a.values.front()) {
                if (c == '\n') {
                    split.push_back(std::move(current));
                    current.clear();
                } else if (c != '\r') {
                    current.push_back(c);
                }
            }
            split.push_back(std::move(current));
            a.values = std::move(split);
        }
        list.push_back(std::move(a));
    }
    Journal::Operation op(doc, "Import form data");
    BatchResult batch = setMany(doc, list);
    op.commit();
    DataImportReport report;
    report.updated = std::move(batch.updated);
    report.unknown = std::move(batch.unknown);
    report.readOnly = std::move(batch.readOnly);
    report.rejected = std::move(batch.rejected);
    report.pages = std::move(batch.pages);
    spdlog::info("forms: imported {} of {} values ({} unknown, {} read-only, {} rejected)",
                 report.updated.size(), report.total(), report.unknown.size(), report.readOnly.size(),
                 report.rejected.size());
    return report;
}

}  // namespace ot::forms
