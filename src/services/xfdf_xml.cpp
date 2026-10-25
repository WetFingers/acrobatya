#include "services/xfdf_xml.h"

#include <QString>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <string>
#include <vector>

namespace ot::xfdf {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

std::string pointsText(const std::vector<PointF>& points) {
    std::string out;
    for (const PointF& p : points) {
        if (!out.empty()) {
            out += ';';
        }
        out += annot::formatNumbers({p.x, p.y});
    }
    return out;
}

std::vector<PointF> parsePoints(const QString& text) {
    const std::vector<float> v = annot::parseNumbers(text.toStdString());
    std::vector<PointF> out;
    for (std::size_t i = 0; i + 1 < v.size(); i += 2) {
        out.push_back({v[i], v[i + 1]});
    }
    return out;
}

QString flagsText(int flags) {
    // Как атрибут flags аннотации (порядок Adobe).
    static const std::pair<int, const char*> kNames[] = {{annot::kAnnotInvisible, "invisible"},
                                                         {annot::kAnnotHidden, "hidden"},
                                                         {annot::kAnnotPrint, "print"},
                                                         {annot::kAnnotNoZoom, "nozoom"},
                                                         {annot::kAnnotNoRotate, "norotate"},
                                                         {annot::kAnnotNoView, "noview"},
                                                         {annot::kAnnotReadOnly, "readonly"},
                                                         {annot::kAnnotLocked, "locked"},
                                                         {annot::kAnnotToggleNoView, "togglenoview"},
                                                         {annot::kAnnotLockedContents, "lockedcontents"}};
    QStringList out;
    for (const auto& [bit, name] : kNames) {
        if ((flags & bit) != 0) {
            out << QLatin1String(name);
        }
    }
    return out.join(QLatin1Char(','));
}

int parseFlags(const QString& text) {
    static const std::pair<const char*, int> kNames[] = {{"invisible", annot::kAnnotInvisible},
                                                         {"hidden", annot::kAnnotHidden},
                                                         {"print", annot::kAnnotPrint},
                                                         {"nozoom", annot::kAnnotNoZoom},
                                                         {"norotate", annot::kAnnotNoRotate},
                                                         {"noview", annot::kAnnotNoView},
                                                         {"readonly", annot::kAnnotReadOnly},
                                                         {"locked", annot::kAnnotLocked},
                                                         {"togglenoview", annot::kAnnotToggleNoView},
                                                         {"lockedcontents", annot::kAnnotLockedContents}};
    int flags = 0;
    for (const QString& item : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        for (const auto& [name, bit] : kNames) {
            if (item.trimmed() == QLatin1String(name)) {
                flags |= bit;
            }
        }
    }
    return flags;
}

// /RC — разметкой (токены XHTML как есть); не XML — ничего (текст останется в <contents>).
void writeRichText(QXmlStreamWriter& w, const std::string& rc) {
    QXmlStreamReader r(QByteArray::fromStdString(rc));
    r.setNamespaceProcessing(false);
    QString inner;
    QXmlStreamWriter copy(&inner);
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartDocument() || r.isEndDocument() || r.isDTD()) {
            continue;
        }
        copy.writeCurrentToken(r);
    }
    if (r.hasError() || inner.isEmpty()) {
        return;
    }
    // Тот же разбор во внешний писатель (writeCurrentToken держит его состояние согласованным).
    QXmlStreamReader again(inner);
    again.setNamespaceProcessing(false);
    w.writeStartElement(QStringLiteral("contents-richtext"));
    while (!again.atEnd()) {
        again.readNext();
        if (again.isStartDocument() || again.isEndDocument()) {
            continue;
        }
        w.writeCurrentToken(again);
    }
    w.writeEndElement();
}

// Разметка внутри текущего элемента — строкой (до его конца).
std::string innerXml(QXmlStreamReader& r) {
    QString inner;
    QXmlStreamWriter w(&inner);
    int depth = 0;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isEndElement() && depth == 0) {
            break;
        }
        if (r.isStartElement()) {
            ++depth;
        } else if (r.isEndElement()) {
            --depth;
        }
        w.writeCurrentToken(r);
    }
    return inner.trimmed().toStdString();
}

annot::XfdfAnnot readAnnot(QXmlStreamReader& r) {
    annot::XfdfAnnot a;
    a.element = r.name().toString().toStdString();
    for (const QXmlStreamAttribute& attr : r.attributes()) {
        a.set(attr.qualifiedName().toString().toStdString(), attr.value().toString().toStdString());
    }
    while (r.readNextStartElement()) {
        const QStringView name = r.name();
        if (name == QLatin1String("contents")) {
            a.contents = r.readElementText(QXmlStreamReader::IncludeChildElements).toStdString();
        } else if (name == QLatin1String("contents-richtext")) {
            a.richText = innerXml(r);
        } else if (name == QLatin1String("defaultappearance")) {
            a.defaultAppearance = r.readElementText().toStdString();
        } else if (name == QLatin1String("defaultstyle")) {
            a.defaultStyle = r.readElementText().toStdString();
        } else if (name == QLatin1String("vertices")) {
            a.vertices = parsePoints(r.readElementText());
        } else if (name == QLatin1String("inklist")) {
            while (r.readNextStartElement()) {
                if (r.name() == QLatin1String("gesture")) {
                    a.ink.push_back(parsePoints(r.readElementText()));
                } else {
                    r.skipCurrentElement();
                }
            }
        } else if (name == QLatin1String("popup")) {
            const QXmlStreamAttributes attrs = r.attributes();
            const std::vector<float> rect =
                annot::parseNumbers(attrs.value(QLatin1String("rect")).toString().toStdString());
            if (rect.size() == 4) {
                a.hasPopup = true;
                a.popupRect = {rect[0], rect[1], rect[2], rect[3]};
                a.popupOpen = attrs.value(QLatin1String("open")) == QLatin1String("yes");
                a.popupFlags = parseFlags(attrs.value(QLatin1String("flags")).toString());
            }
            r.skipCurrentElement();
        } else if (name == QLatin1String("data")) {
            const QXmlStreamAttributes attrs = r.attributes();
            const QString encoding = attrs.value(QLatin1String("encoding")).toString().toLower();
            const bool filtered = attrs.hasAttribute(QLatin1String("filter")) &&
                                  !attrs.value(QLatin1String("filter")).isEmpty();
            const QByteArray text = r.readElementText().toLatin1();
            if (!filtered) {  // сжатые данные Acrobat (FlateDecode) не разбираем — вложения не будет
                const QByteArray bytes = encoding == QLatin1String("base64") ? QByteArray::fromBase64(text)
                                                                             : QByteArray::fromHex(text);
                a.fileData = bytes.toStdString();
                a.hasFileData = true;
            }
        } else {
            r.skipCurrentElement();
        }
    }
    return a;
}

}  // namespace

QByteArray write(const annot::XfdfDocument& doc) {
    QByteArray out;
    QXmlStreamWriter w(&out);
    w.writeStartDocument(QStringLiteral("1.0"));
    w.writeStartElement(QStringLiteral("xfdf"));
    w.writeAttribute(QStringLiteral("xmlns"), QString::fromLatin1(annot::kXfdfNamespace));
    w.writeAttribute(QStringLiteral("xml:space"), QStringLiteral("preserve"));
    w.writeStartElement(QStringLiteral("annots"));
    for (const annot::XfdfAnnot& a : doc.annots) {
        if (a.element.empty()) {
            continue;
        }
        w.writeStartElement(qs(a.element));
        for (const auto& [k, v] : a.attributes) {
            w.writeAttribute(qs(k), qs(v));
        }
        if (!a.contents.empty()) {
            w.writeTextElement(QStringLiteral("contents"), qs(a.contents));
        }
        if (!a.richText.empty()) {
            writeRichText(w, a.richText);
        }
        if (!a.defaultAppearance.empty()) {
            w.writeTextElement(QStringLiteral("defaultappearance"), qs(a.defaultAppearance));
        }
        if (!a.defaultStyle.empty()) {
            w.writeTextElement(QStringLiteral("defaultstyle"), qs(a.defaultStyle));
        }
        if (!a.vertices.empty()) {
            w.writeTextElement(QStringLiteral("vertices"), qs(pointsText(a.vertices)));
        }
        if (!a.ink.empty()) {
            w.writeStartElement(QStringLiteral("inklist"));
            for (const std::vector<PointF>& stroke : a.ink) {
                w.writeTextElement(QStringLiteral("gesture"), qs(pointsText(stroke)));
            }
            w.writeEndElement();
        }
        if (a.hasPopup) {
            w.writeEmptyElement(QStringLiteral("popup"));
            if (a.popupFlags != 0) {
                w.writeAttribute(QStringLiteral("flags"), flagsText(a.popupFlags));
            }
            w.writeAttribute(QStringLiteral("open"),
                             a.popupOpen ? QStringLiteral("yes") : QStringLiteral("no"));
            w.writeAttribute(QStringLiteral("page"), qs(a.attribute("page")));
            w.writeAttribute(
                QStringLiteral("rect"),
                qs(annot::formatNumbers({a.popupRect.x0, a.popupRect.y0, a.popupRect.x1, a.popupRect.y1})));
        }
        if (a.hasFileData) {
            const QByteArray data = QByteArray::fromStdString(a.fileData);
            w.writeStartElement(QStringLiteral("data"));
            w.writeAttribute(QStringLiteral("MODE"), QStringLiteral("RAW"));
            w.writeAttribute(QStringLiteral("encoding"), QStringLiteral("base64"));
            w.writeAttribute(QStringLiteral("length"), QString::number(data.size()));
            w.writeCharacters(QString::fromLatin1(data.toBase64()));
            w.writeEndElement();
        }
        w.writeEndElement();
    }
    w.writeEndElement();  // annots
    if (!doc.file.empty()) {
        w.writeEmptyElement(QStringLiteral("f"));
        w.writeAttribute(QStringLiteral("href"), qs(doc.file));
    }
    w.writeEndElement();  // xfdf
    w.writeEndDocument();
    return out;
}

annot::XfdfDocument read(const QByteArray& xml) {
    QXmlStreamReader r(xml);
    r.setNamespaceProcessing(false);
    if (!r.readNextStartElement() || r.name() != QLatin1String("xfdf")) {
        throw annot::ExchangeError(annot::ExchangeError::Kind::Malformed, "not an XFDF file");
    }
    annot::XfdfDocument doc;
    while (r.readNextStartElement()) {
        if (r.name() == QLatin1String("annots")) {
            while (r.readNextStartElement()) {
                doc.annots.push_back(readAnnot(r));
            }
        } else if (r.name() == QLatin1String("f")) {
            doc.file = r.attributes().value(QLatin1String("href")).toString().toStdString();
            r.skipCurrentElement();
        } else {
            r.skipCurrentElement();
        }
    }
    if (r.hasError() && r.error() != QXmlStreamReader::PrematureEndOfDocumentError) {
        throw annot::ExchangeError(annot::ExchangeError::Kind::Malformed,
                                   "XFDF cannot be read: " + r.errorString().toStdString());
    }
    return doc;
}

}  // namespace ot::xfdf
