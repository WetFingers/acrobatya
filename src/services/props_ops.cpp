#include "services/props_ops.h"

#include <spdlog/spdlog.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/doc/xmp.h"
#include "services/task_context.h"

namespace ot::props {

namespace {

std::string* field(DocProperties& p, const QString& key) {
    if (key == QLatin1String("title")) {
        return &p.title;
    }
    if (key == QLatin1String("author")) {
        return &p.author;
    }
    if (key == QLatin1String("subject")) {
        return &p.subject;
    }
    if (key == QLatin1String("keywords")) {
        return &p.keywords;
    }
    if (key == QLatin1String("creator")) {
        return &p.creator;
    }
    if (key == QLatin1String("producer")) {
        return &p.producer;
    }
    return nullptr;
}

}  // namespace

QStringList editableKeys() {
    return {QStringLiteral("title"),    QStringLiteral("author"),  QStringLiteral("subject"),
            QStringLiteral("keywords"), QStringLiteral("creator"), QStringLiteral("producer")};
}

QFuture<DocProperties> read(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return tc.document().properties(); }, TaskPriority::High,
                        QStringLiteral("read properties"));
}

QFuture<void> setProperty(DocumentSession& session, const QString& key, const QString& value) {
    return session.edit(QStringLiteral("Set document property"), [key, value](TaskContext& tc) {
        Document& doc = tc.document();
        DocProperties p = doc.properties();
        std::string* target = field(p, key);
        if (target == nullptr) {
            throw std::invalid_argument("unknown document property: " + key.toStdString());
        }
        *target = value.toStdString();
        doc.setProperties(p);
    });
}

QFuture<Sheet> readSheet(DocumentSession& session) {
    const QString path = session.path();
    return session.post(
        [path](TaskContext& tc) {
            Document& doc = tc.document();
            Sheet sheet;
            sheet.path = path;
            sheet.isPdf = doc.isPdf();
            sheet.doc = doc.properties();
            if (doc.pageCount() > 0) {
                const PageSize size = doc.pageSize(0);
                sheet.firstPageSize = QSizeF(size.width, size.height);
            }
            const std::pair<bool*, Perm> rights[] = {
                {&sheet.rights.print, Perm::Print},
                {&sheet.rights.printHighRes, Perm::PrintHighRes},
                {&sheet.rights.copy, Perm::Copy},
                {&sheet.rights.edit, Perm::Edit},
                {&sheet.rights.annotate, Perm::Annotate},
                {&sheet.rights.fillForms, Perm::FillForms},
                {&sheet.rights.accessibility, Perm::Accessibility},
                {&sheet.rights.assemble, Perm::Assemble},
            };
            for (const auto& [flag, perm] : rights) {
                *flag = doc.allowed(perm);
            }
            if (!sheet.isPdf) {
                return sheet;
            }
            sheet.info = readInfo(doc);
            sheet.xmp = xmpPacket(doc);
            sheet.lang = lang(doc);
            sheet.baseUrl = baseUrl(doc);
            sheet.initial = readInitialView(doc);
            sheet.advanced = advanced(doc, false);
            return sheet;
        },
        TaskPriority::High, QStringLiteral("read properties sheet"));
}

QFuture<std::vector<FontInfo>> fonts(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return ot::props::fonts(tc.document()); }, TaskPriority::Normal,
                        QStringLiteral("list fonts"));
}

QFuture<Advanced> check(DocumentSession& session) {
    return session.post([](TaskContext& tc) { return advanced(tc.document(), true); }, TaskPriority::Normal,
                        QStringLiteral("check document"));
}

QFuture<void> apply(DocumentSession& session, const Changes& changes) {
    return session.edit(QStringLiteral("Document properties"), [changes](TaskContext& tc) {
        Document& doc = tc.document();
        if (changes.info) {
            writeInfo(doc, *changes.info);
        }
        if (changes.initial) {
            writeInitialView(doc, *changes.initial);
        }
        if (changes.lang) {
            setLang(doc, *changes.lang);
        }
        if (changes.baseUrl) {
            setBaseUrl(doc, *changes.baseUrl);
        }
    });
}

QVector<XmpProperty> xmpProperties(const std::string& packet) {
    QVector<XmpProperty> out;
    if (packet.empty()) {
        return out;
    }
    const std::pair<std::string_view, const char*> schemas[] = {
        {xmp::kDc, "dc"},
        {xmp::kXmp, "xmp"},
        {xmp::kPdf, "pdf"},
        {xmp::kPdfx, "pdfx"},
        {xmp::kPdfaId, "pdfaid"},
        {"http://ns.adobe.com/xap/1.0/mm/", "xmpMM"},
        {"http://ns.adobe.com/xap/1.0/rights/", "xmpRights"},
        {"http://ns.adobe.com/photoshop/1.0/", "photoshop"},
    };
    try {
        const xmp::Packet p = xmp::Packet::parse(packet);
        for (const auto& [ns, prefix] : schemas) {
            for (const auto& [name, value] : p.simpleProperties(ns)) {
                out << XmpProperty{QString::fromUtf8(ns.data(), static_cast<qsizetype>(ns.size())),
                                   QLatin1String(prefix), QString::fromStdString(name),
                                   QString::fromStdString(value)};
            }
            // Составные свойства Dublin Core (Alt, Seq, Bag) simpleProperties не отдаёт — по именам.
            if (ns == xmp::kDc) {
                for (const char* name :
                     {"title", "creator", "description", "subject", "rights", "language"}) {
                    const auto value = p.get(ns, name);
                    bool listed = false;
                    for (const XmpProperty& x : out) {
                        listed = listed || (x.prefix == QLatin1String("dc") && x.name == QLatin1String(name));
                    }
                    if (value && !listed) {
                        out << XmpProperty{QString::fromUtf8(ns.data(), static_cast<qsizetype>(ns.size())),
                                           QStringLiteral("dc"), QLatin1String(name),
                                           QString::fromStdString(*value)};
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("props: XMP packet not parsed: {}", e.what());
        out.clear();
    }
    return out;
}

}  // namespace ot::props
