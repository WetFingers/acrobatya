#include "core/doc/xmp.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>

namespace ot::xmp {

namespace {

constexpr std::string_view kXpacketBegin =
    "<?xpacket begin=\"\xEF\xBB\xBF\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>";
constexpr std::string_view kXpacketEnd = "<?xpacket end=\"w\"?>";

bool isNameChar(unsigned char c) {
    return std::isalnum(c) != 0 || c == '_' || c == ':' || c == '.' || c == '-' || c >= 0x80;
}

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    explicit Parser(std::string_view s) : s_(s) {}

    std::vector<Node> document() {
        std::vector<Node> out;
        bool root = false;
        while (i_ < s_.size()) {
            if (s_[i_] == '<') {
                if (starts("</")) {
                    fail("unexpected end tag");
                }
                Node n = markupOrElement();
                if (n.kind == Node::Kind::Element) {
                    if (root) {
                        fail("second root element");
                    }
                    root = true;
                }
                out.push_back(std::move(n));
            } else {
                out.push_back(text());
            }
        }
        if (!root) {
            fail("no root element");
        }
        return out;
    }

private:
    [[noreturn]] void fail(const char* what) const {
        throw std::runtime_error(std::string("xmp: ") + what + " at byte " + std::to_string(i_));
    }

    bool starts(std::string_view p) const { return s_.substr(i_, p.size()) == p; }

    void skipSpace() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) {
            ++i_;
        }
    }

    Node raw(std::string_view end) {
        const size_t at = s_.find(end, i_);
        if (at == std::string_view::npos) {
            fail("unterminated markup");
        }
        Node n;
        n.kind = Node::Kind::Raw;
        n.text = std::string(s_.substr(i_, at + end.size() - i_));
        i_ = at + end.size();
        return n;
    }

    Node markupOrElement() {
        if (starts("<?")) {
            return raw("?>");
        }
        if (starts("<!--")) {
            return raw("-->");
        }
        if (starts("<![CDATA[")) {
            return raw("]]>");
        }
        if (starts("<!")) {
            return raw(">");
        }
        return element();
    }

    std::string name() {
        const size_t start = i_;
        while (i_ < s_.size() && isNameChar(static_cast<unsigned char>(s_[i_]))) {
            ++i_;
        }
        if (i_ == start) {
            fail("name expected");
        }
        return std::string(s_.substr(start, i_ - start));
    }

    std::string unescape(std::string_view v) const {
        std::string out;
        out.reserve(v.size());
        for (size_t k = 0; k < v.size(); ++k) {
            if (v[k] != '&') {
                out.push_back(v[k]);
                continue;
            }
            const size_t semi = v.find(';', k);
            if (semi == std::string_view::npos) {
                fail("bad entity");
            }
            const std::string_view ent = v.substr(k + 1, semi - k - 1);
            if (ent == "lt") {
                out.push_back('<');
            } else if (ent == "gt") {
                out.push_back('>');
            } else if (ent == "amp") {
                out.push_back('&');
            } else if (ent == "quot") {
                out.push_back('"');
            } else if (ent == "apos") {
                out.push_back('\'');
            } else if (ent.size() > 1 && ent[0] == '#') {
                const bool hex = ent[1] == 'x' || ent[1] == 'X';
                const std::string digits(ent.substr(hex ? 2 : 1));
                if (digits.empty()) {
                    fail("bad character reference");
                }
                std::size_t used = 0;
                const unsigned long cp = std::stoul(digits, &used, hex ? 16 : 10);
                if (used != digits.size() || cp > 0x10FFFF) {
                    fail("bad character reference");
                }
                appendUtf8(out, static_cast<std::uint32_t>(cp));
            } else {
                fail("unknown entity");
            }
            k = semi;
        }
        return out;
    }

    Node text() {
        const size_t at = s_.find('<', i_);
        const size_t end = at == std::string_view::npos ? s_.size() : at;
        Node n;
        n.kind = Node::Kind::Text;
        n.text = unescape(s_.substr(i_, end - i_));
        i_ = end;
        return n;
    }

    Node element() {
        ++i_;  // '<'
        Node n;
        n.kind = Node::Kind::Element;
        n.name = name();
        for (;;) {
            skipSpace();
            if (i_ >= s_.size()) {
                fail("unterminated start tag");
            }
            if (s_[i_] == '/') {
                if (!starts("/>")) {
                    fail("'/>' expected");
                }
                i_ += 2;
                return n;
            }
            if (s_[i_] == '>') {
                ++i_;
                break;
            }
            std::string attr = name();
            skipSpace();
            if (i_ >= s_.size() || s_[i_] != '=') {
                fail("'=' expected");
            }
            ++i_;
            skipSpace();
            if (i_ >= s_.size() || (s_[i_] != '"' && s_[i_] != '\'')) {
                fail("quoted attribute value expected");
            }
            const char quote = s_[i_++];
            const size_t close = s_.find(quote, i_);
            if (close == std::string_view::npos) {
                fail("unterminated attribute value");
            }
            n.attrs.emplace_back(std::move(attr), unescape(s_.substr(i_, close - i_)));
            i_ = close + 1;
        }
        for (;;) {
            if (i_ >= s_.size()) {
                fail("unclosed element");
            }
            if (starts("</")) {
                i_ += 2;
                const std::string end = name();
                skipSpace();
                if (i_ >= s_.size() || s_[i_] != '>') {
                    fail("'>' expected");
                }
                ++i_;
                if (end != n.name) {
                    fail("mismatched end tag");
                }
                return n;
            }
            n.children.push_back(s_[i_] == '<' ? markupOrElement() : text());
        }
    }

    std::string_view s_;
    size_t i_ = 0;
};

std::string escape(std::string_view v, bool attr) {
    std::string out;
    out.reserve(v.size());
    for (const char c : v) {
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
                out += attr ? "&quot;" : "\"";
                break;
            case '\n':
                out += attr ? "&#xA;" : "\n";
                break;
            case '\r':
                out += "&#xD;";
                break;
            case '\t':
                out += attr ? "&#x9;" : "\t";
                break;
            default:
                out.push_back(c);
        }
    }
    return out;
}

void write(std::string& out, const Node& n) {
    switch (n.kind) {
        case Node::Kind::Text:
            out += escape(n.text, false);
            return;
        case Node::Kind::Raw:
            out += n.text;
            return;
        case Node::Kind::Element:
            out += '<';
            out += n.name;
            for (const auto& [k, v] : n.attrs) {
                out += ' ';
                out += k;
                out += "=\"";
                out += escape(v, true);
                out += '"';
            }
            if (n.children.empty()) {
                out += "/>";
                return;
            }
            out += '>';
            for (const Node& c : n.children) {
                write(out, c);
            }
            out += "</";
            out += n.name;
            out += '>';
            return;
    }
}

std::pair<std::string_view, std::string_view> splitName(std::string_view qname) {
    const size_t colon = qname.find(':');
    if (colon == std::string_view::npos) {
        return {std::string_view(), qname};
    }
    return {qname.substr(0, colon), qname.substr(colon + 1)};
}

// Цепочка предков (внешний — первый) для разрешения префиксов.
using Chain = std::vector<const Node*>;

std::optional<std::string> uriOf(const Chain& chain, std::string_view prefix) {
    if (prefix == "xml") {
        return std::string("http://www.w3.org/XML/1998/namespace");
    }
    const std::string key = prefix.empty() ? std::string("xmlns") : "xmlns:" + std::string(prefix);
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        for (const auto& [k, v] : (*it)->attrs) {
            if (k == key) {
                return v;
            }
        }
    }
    return std::nullopt;
}

bool elementIs(const Node& n, const Chain& chainWithN, std::string_view ns, std::string_view local) {
    if (n.kind != Node::Kind::Element) {
        return false;
    }
    const auto [prefix, name] = splitName(n.name);
    if (name != local) {
        return false;
    }
    const auto uri = uriOf(chainWithN, prefix);
    return uri && *uri == ns;
}

bool attrIs(std::string_view qname, const Chain& chain, std::string_view ns, std::string_view local) {
    const auto [prefix, name] = splitName(qname);
    if (prefix.empty() || prefix == "xmlns" || name != local) {
        return false;  // атрибут без префикса — без пространства имён
    }
    const auto uri = uriOf(chain, prefix);
    return uri && *uri == ns;
}

struct Description {
    Node* node = nullptr;
    Chain chain;  // с самим node последним
};

void findDescriptions(std::vector<Node>& nodes, Chain& chain, std::vector<Description>& out) {
    for (Node& n : nodes) {
        if (n.kind != Node::Kind::Element) {
            continue;
        }
        chain.push_back(&n);
        const bool parentIsRdf =
            chain.size() >= 2 &&
            elementIs(*chain[chain.size() - 2], Chain(chain.begin(), chain.end() - 1), kRdf, "RDF");
        if (parentIsRdf && elementIs(n, chain, kRdf, "Description")) {
            out.push_back({&n, chain});
        } else {
            findDescriptions(n.children, chain, out);
        }
        chain.pop_back();
    }
}

std::string textOf(const Node& n) {
    std::string out;
    for (const Node& c : n.children) {
        if (c.kind == Node::Kind::Text) {
            out += c.text;
        }
    }
    return out;
}

// Первый дочерний элемент-контейнер rdf:Alt/Seq/Bag.
Node* container(Node& prop, const Chain& chain, std::string_view* kind) {
    Chain c = chain;
    c.push_back(&prop);
    for (Node& child : prop.children) {
        for (const std::string_view k :
             {std::string_view("Alt"), std::string_view("Seq"), std::string_view("Bag")}) {
            Chain cc = c;
            cc.push_back(&child);
            if (elementIs(child, cc, kRdf, k)) {
                *kind = k;
                return &child;
            }
        }
    }
    return nullptr;
}

std::string valueOf(Node& prop, const Chain& chain) {
    std::string_view kind;
    Node* box = container(prop, chain, &kind);
    if (box == nullptr) {
        return textOf(prop);
    }
    std::vector<std::string> items;
    std::string first;
    bool haveFirst = false;
    for (const Node& li : box->children) {
        if (li.kind != Node::Kind::Element || splitName(li.name).second != "li") {
            continue;
        }
        const std::string t = textOf(li);
        if (kind == "Alt") {
            for (const auto& [k, v] : li.attrs) {
                if (k == "xml:lang" && v == "x-default") {
                    return t;
                }
            }
            if (!haveFirst) {
                first = t;
                haveFirst = true;
            }
        } else {
            items.push_back(t);
        }
    }
    if (kind == "Alt") {
        return first;
    }
    std::string joined;
    for (size_t k = 0; k < items.size(); ++k) {
        joined += (k > 0 ? "; " : "") + items[k];
    }
    return joined;
}

std::string defaultPrefix(std::string_view ns) {
    if (ns == kDc) {
        return "dc";
    }
    if (ns == kXmp) {
        return "xmp";
    }
    if (ns == kPdf) {
        return "pdf";
    }
    if (ns == kPdfx) {
        return "pdfx";
    }
    if (ns == kPdfaId) {
        return "pdfaid";
    }
    return "ns";
}

// Префикс для ns в описании: объявленный в цепочке или новый xmlns на самом описании.
std::string ensurePrefix(Description& d, std::string_view ns) {
    for (auto it = d.chain.rbegin(); it != d.chain.rend(); ++it) {
        for (const auto& [k, v] : (*it)->attrs) {
            if (v == ns && k.rfind("xmlns:", 0) == 0) {
                const std::string prefix = k.substr(6);
                if (uriOf(d.chain, prefix) == std::string(ns)) {
                    return prefix;  // не перекрыт ближе
                }
            }
        }
    }
    const std::string base = defaultPrefix(ns);
    std::string prefix = base;
    for (int n = 1; uriOf(d.chain, prefix).has_value(); ++n) {
        prefix = base + std::to_string(n);
    }
    d.node->attrs.emplace_back("xmlns:" + prefix, std::string(ns));
    return prefix;
}

Node textNode(std::string t) {
    Node n;
    n.kind = Node::Kind::Text;
    n.text = std::move(t);
    return n;
}

Node elementNode(std::string name) {
    Node n;
    n.kind = Node::Kind::Element;
    n.name = std::move(name);
    return n;
}

// Описание для нового свойства: то, где пространство ns объявлено само (xmlns на описании), иначе первое.
Description& pickTarget(std::vector<Description>& descs, std::string_view ns) {
    for (Description& d : descs) {
        for (const auto& [k, v] : d.node->attrs) {
            if (v == ns && k.rfind("xmlns:", 0) == 0) {
                return d;
            }
        }
    }
    return descs.front();
}

// Добавить элемент свойства в описание с отступом как у соседей.
void appendProperty(Description& d, Node prop) {
    std::vector<Node>& kids = d.node->children;
    std::string indent = "\n   ";
    for (const Node& c : kids) {
        if (c.kind == Node::Kind::Text && !c.text.empty() &&
            c.text.find_first_not_of(" \t\r\n") == std::string::npos) {
            indent = c.text;
            break;
        }
    }
    if (kids.empty()) {
        kids.push_back(textNode(indent));
        kids.push_back(std::move(prop));
        kids.push_back(textNode("\n  "));
        return;
    }
    auto pos = kids.end();
    if (kids.back().kind == Node::Kind::Text &&
        kids.back().text.find_first_not_of(" \t\r\n") == std::string::npos) {
        pos = kids.end() - 1;
    }
    pos = kids.insert(pos, std::move(prop));
    kids.insert(pos, textNode(indent));
}

}  // namespace

Packet Packet::parse(std::string_view xml) {
    if (xml.substr(0, 3) == "\xEF\xBB\xBF") {
        xml.remove_prefix(3);
    }
    Packet p;
    p.nodes_ = Parser(xml).document();
    Chain chain;
    std::vector<Description> found;
    findDescriptions(p.nodes_, chain, found);
    if (found.empty()) {
        throw std::runtime_error("xmp: no rdf:RDF/rdf:Description");
    }
    return p;
}

Packet Packet::create() {
    return parse(
        std::string(kXpacketBegin) +
        "\n<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">\n <rdf:RDF "
        "xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">\n  <rdf:Description rdf:about=\"\"/>\n "
        "</rdf:RDF>\n</x:xmpmeta>\n" +
        std::string(kXpacketEnd));
}

std::string Packet::serialize() const {
    std::string out;
    for (const Node& n : nodes_) {
        write(out, n);
    }
    return out;
}

std::optional<std::string> Packet::get(std::string_view ns, std::string_view local) const {
    auto& nodes = const_cast<std::vector<Node>&>(nodes_);  // поиск не меняет дерево
    Chain chain;
    std::vector<Description> descs;
    findDescriptions(nodes, chain, descs);
    for (Description& d : descs) {
        for (const auto& [k, v] : d.node->attrs) {
            if (attrIs(k, d.chain, ns, local)) {
                return v;
            }
        }
        for (Node& child : d.node->children) {
            Chain c = d.chain;
            c.push_back(&child);
            if (elementIs(child, c, ns, local)) {
                return valueOf(child, d.chain);
            }
        }
    }
    return std::nullopt;
}

void Packet::remove(std::string_view ns, std::string_view local) {
    Chain chain;
    std::vector<Description> descs;
    findDescriptions(nodes_, chain, descs);
    for (Description& d : descs) {
        auto& attrs = d.node->attrs;
        std::erase_if(attrs, [&](const auto& a) { return attrIs(a.first, d.chain, ns, local); });
        auto& kids = d.node->children;
        for (size_t k = 0; k < kids.size();) {
            Chain c = d.chain;
            c.push_back(&kids[k]);
            if (elementIs(kids[k], c, ns, local)) {
                kids.erase(kids.begin() + static_cast<std::ptrdiff_t>(k));
                // Отступ перед удалённым — тоже.
                if (k > 0 && kids[k - 1].kind == Node::Kind::Text &&
                    kids[k - 1].text.find_first_not_of(" \t\r\n") == std::string::npos) {
                    kids.erase(kids.begin() + static_cast<std::ptrdiff_t>(k - 1));
                    --k;
                }
            } else {
                ++k;
            }
        }
    }
}

void Packet::setSimple(std::string_view ns, std::string_view local, const std::string& value) {
    Chain chain;
    std::vector<Description> descs;
    findDescriptions(nodes_, chain, descs);
    // Было атрибутом — остаётся атрибутом на том же месте; иначе — элемент в подходящем описании.
    Node* host = nullptr;
    size_t pos = 0;
    std::string attrName;
    for (Description& d : descs) {
        for (size_t k = 0; k < d.node->attrs.size() && host == nullptr; ++k) {
            if (attrIs(d.node->attrs[k].first, d.chain, ns, local)) {
                host = d.node;
                pos = k;
                attrName = d.node->attrs[k].first;
            }
        }
    }
    remove(ns, local);  // узлы описаний остаются на местах — указатели в descs верны
    if (host != nullptr) {
        host->attrs.insert(host->attrs.begin() + static_cast<std::ptrdiff_t>(pos), {attrName, value});
        return;
    }
    Description& target = pickTarget(descs, ns);
    const std::string prefix = ensurePrefix(target, ns);
    Node prop = elementNode(prefix + ":" + std::string(local));
    prop.children.push_back(textNode(value));
    appendProperty(target, std::move(prop));
}

void Packet::setAlt(std::string_view ns, std::string_view local, const std::string& value) {
    Chain chain;
    std::vector<Description> descs;
    findDescriptions(nodes_, chain, descs);
    for (Description& d : descs) {
        for (Node& child : d.node->children) {
            Chain c = d.chain;
            c.push_back(&child);
            if (!elementIs(child, c, ns, local)) {
                continue;
            }
            std::string_view kind;
            Node* box = container(child, d.chain, &kind);
            if (box == nullptr || kind != "Alt") {
                break;  // не rdf:Alt — заменить целиком ниже
            }
            for (Node& li : box->children) {
                if (li.kind != Node::Kind::Element || splitName(li.name).second != "li") {
                    continue;
                }
                for (const auto& [k, v] : li.attrs) {
                    if (k == "xml:lang" && v == "x-default") {
                        li.children.clear();
                        li.children.push_back(textNode(value));
                        return;
                    }
                }
            }
            const std::string rdf = splitName(box->name).first.empty()
                                        ? std::string("rdf")
                                        : std::string(splitName(box->name).first);
            Node li = elementNode(rdf + ":li");
            li.attrs.emplace_back("xml:lang", "x-default");
            li.children.push_back(textNode(value));
            box->children.insert(box->children.begin(), std::move(li));
            return;
        }
    }
    remove(ns, local);
    Description& target = pickTarget(descs, ns);
    const std::string prefix = ensurePrefix(target, ns);
    const std::string rdf = ensurePrefix(target, kRdf);
    Node li = elementNode(rdf + ":li");
    li.attrs.emplace_back("xml:lang", "x-default");
    li.children.push_back(textNode(value));
    Node alt = elementNode(rdf + ":Alt");
    alt.children.push_back(std::move(li));
    Node prop = elementNode(prefix + ":" + std::string(local));
    prop.children.push_back(std::move(alt));
    appendProperty(target, std::move(prop));
}

void Packet::setSeq(std::string_view ns, std::string_view local, const std::vector<std::string>& values) {
    remove(ns, local);
    Chain chain;
    std::vector<Description> descs;
    findDescriptions(nodes_, chain, descs);
    Description& target = pickTarget(descs, ns);
    const std::string prefix = ensurePrefix(target, ns);
    const std::string rdf = ensurePrefix(target, kRdf);
    Node seq = elementNode(rdf + ":Seq");
    for (const std::string& v : values) {
        Node li = elementNode(rdf + ":li");
        li.children.push_back(textNode(v));
        seq.children.push_back(std::move(li));
    }
    Node prop = elementNode(prefix + ":" + std::string(local));
    prop.children.push_back(std::move(seq));
    appendProperty(target, std::move(prop));
}

std::vector<std::pair<std::string, std::string>> Packet::simpleProperties(std::string_view ns) const {
    std::vector<std::pair<std::string, std::string>> out;
    auto& nodes = const_cast<std::vector<Node>&>(nodes_);
    Chain chain;
    std::vector<Description> descs;
    findDescriptions(nodes, chain, descs);
    for (Description& d : descs) {
        for (const auto& [k, v] : d.node->attrs) {
            const auto [prefix, name] = splitName(k);
            if (!prefix.empty() && prefix != "xmlns" && uriOf(d.chain, prefix) == std::string(ns)) {
                out.emplace_back(std::string(name), v);
            }
        }
        for (Node& child : d.node->children) {
            if (child.kind != Node::Kind::Element) {
                continue;
            }
            const auto [prefix, name] = splitName(child.name);
            Chain c = d.chain;
            c.push_back(&child);
            if (uriOf(c, prefix) == std::string(ns)) {
                out.emplace_back(std::string(name), valueOf(child, d.chain));
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------- даты

std::string pdfDateToIso(std::string_view d) {
    if (d.substr(0, 2) == "D:") {
        d.remove_prefix(2);
    }
    auto digits = [&](size_t at, size_t n) {
        if (at + n > d.size()) {
            return false;
        }
        for (size_t k = at; k < at + n; ++k) {
            if (std::isdigit(static_cast<unsigned char>(d[k])) == 0) {
                return false;
            }
        }
        return true;
    };
    if (!digits(0, 4)) {
        return {};
    }
    std::string out(d.substr(0, 4));
    size_t at = 4;
    static constexpr const char* kSep[] = {"-", "-", "T", ":", ":"};
    for (int part = 0; part < 5 && digits(at, 2); ++part) {
        out += kSep[part];
        out += d.substr(at, 2);
        at += 2;
    }
    const bool hasTime = out.size() > 10;
    if (hasTime && out.size() == 13) {
        out += ":00";  // только час — минуты обязательны в ISO
    }
    if (hasTime && at < d.size()) {
        if (d[at] == 'Z') {
            out += 'Z';
        } else if ((d[at] == '+' || d[at] == '-') && digits(at + 1, 2)) {
            out += d[at];
            out += d.substr(at + 1, 2);
            size_t m = at + 3;
            if (m < d.size() && d[m] == '\'') {
                ++m;
            }
            out += ':';
            out += digits(m, 2) ? std::string(d.substr(m, 2)) : std::string("00");
        }
    }
    return out;
}

std::string isoToPdfDate(std::string_view iso) {
    std::string digits;
    size_t at = 0;
    for (; at < iso.size() && digits.size() < 14; ++at) {
        const char c = iso[at];
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            digits.push_back(c);
        } else if (c == '-' && digits.size() >= 4 && digits.size() < 8) {
            continue;
        } else if (c == 'T' || c == ':') {
            continue;
        } else {
            break;
        }
    }
    if (digits.size() < 4) {
        return {};
    }
    while (at < iso.size() && (iso[at] == '.' || std::isdigit(static_cast<unsigned char>(iso[at])) != 0)) {
        ++at;  // доли секунды
    }
    std::string out = "D:" + digits;
    if (at < iso.size()) {
        if (iso[at] == 'Z') {
            out += 'Z';
        } else if ((iso[at] == '+' || iso[at] == '-') && at + 3 <= iso.size()) {
            out += iso[at];
            out += iso.substr(at + 1, 2);
            out += '\'';
            const size_t m = at + 3 < iso.size() && iso[at + 3] == ':' ? at + 4 : at + 3;
            out += m + 2 <= iso.size() ? std::string(iso.substr(m, 2)) : std::string("00");
            out += '\'';
        }
    }
    return out;
}

bool isXmlName(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(name[0]);
    if (std::isalpha(first) == 0 && first != '_' && first < 0x80) {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        const auto u = static_cast<unsigned char>(c);
        return u != ':' && isNameChar(u);
    });
}

}  // namespace ot::xmp
