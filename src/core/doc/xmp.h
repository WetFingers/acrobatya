// XMP (эталон 3.13, view-46): свой разбор и сериализатор пакета XMP без библиотек XML. Пакет — дерево узлов
// (элементы с атрибутами по порядку, текст, «сырое» — инструкции обработки <?xpacket?>, комментарии, CDATA);
// свойства ищутся по URI пространства имён в rdf:Description (атрибутом или элементом: простое значение,
// rdf:Alt, rdf:Seq, rdf:Bag). Правятся только запрошенные свойства — pdfaid, dc:subject (rdf:Bag), чужие
// схемы, комментарии и обёртка xpacket переносятся как есть. Неверный XML — std::runtime_error.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ot::xmp {

inline constexpr std::string_view kRdf = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
inline constexpr std::string_view kMeta = "adobe:ns:meta/";
inline constexpr std::string_view kDc = "http://purl.org/dc/elements/1.1/";
inline constexpr std::string_view kXmp = "http://ns.adobe.com/xap/1.0/";
inline constexpr std::string_view kPdf = "http://ns.adobe.com/pdf/1.3/";
inline constexpr std::string_view kPdfx = "http://ns.adobe.com/pdfx/1.3/";  // пользовательские свойства /Info
inline constexpr std::string_view kPdfaId = "http://www.aiim.org/pdfa/ns/id/";

struct Node {
    enum class Kind { Element, Text, Raw };
    Kind kind = Kind::Text;
    std::string name;                                        // элемент: имя с префиксом
    std::vector<std::pair<std::string, std::string>> attrs;  // имя → значение (без экранирования)
    std::string text;                                        // текст (без экранирования) или сырой кусок
    std::vector<Node> children;
};

class Packet {
public:
    static Packet parse(std::string_view xml);
    // Новый пакет: <?xpacket?>, x:xmpmeta, rdf:RDF, пустой rdf:Description rdf:about="".
    static Packet create();
    std::string serialize() const;

    // Значение свойства: простое — текст; rdf:Alt — x-default (или первый); rdf:Seq/rdf:Bag — элементы через
    // «; ».
    std::optional<std::string> get(std::string_view ns, std::string_view local) const;
    void setSimple(std::string_view ns, std::string_view local, const std::string& value);
    // rdf:Alt: меняется x-default, прочие языки остаются.
    void setAlt(std::string_view ns, std::string_view local, const std::string& value);
    void setSeq(std::string_view ns, std::string_view local, const std::vector<std::string>& values);
    void remove(std::string_view ns, std::string_view local);
    // Простые свойства пространства ns: (локальное имя, значение).
    std::vector<std::pair<std::string, std::string>> simpleProperties(std::string_view ns) const;

private:
    std::vector<Node> nodes_;
};

// Даты: PDF «D:YYYYMMDDHHmmSSOHH'mm'» ↔ ISO 8601 «YYYY-MM-DDThh:mm:ss±hh:mm» (точность — сколько задано);
// не разобрать — пусто.
std::string pdfDateToIso(std::string_view pdfDate);
std::string isoToPdfDate(std::string_view iso);

// Годится ли строка в локальное имя XML (пользовательский ключ /Info → pdfx:<ключ>).
bool isXmlName(std::string_view name);

}  // namespace ot::xmp
