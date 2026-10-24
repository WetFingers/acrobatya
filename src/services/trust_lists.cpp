#include "services/trust_lists.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStringList>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <spdlog/spdlog.h>

#include <cstdlib>
#include <stdexcept>

#include "core/sign/ossl.h"
#include "core/sign/trust_store.h"

namespace ot {

namespace {

const QString kXmlNs = QStringLiteral("http://www.w3.org/XML/1998/namespace");

// Общий разбор TrustServiceStatusList: LOTL и национальный TL — одна схема.
struct Parsed {
    sign::Lotl lotl;
    sign::TrustedList tl;
};

class ListParser {
public:
    explicit ListParser(const QByteArray& xml) : r_(xml) {}

    Parsed run() {
        while (!r_.atEnd()) {
            const QXmlStreamReader::TokenType t = r_.readNext();
            if (t == QXmlStreamReader::StartElement) {
                start();
            } else if (t == QXmlStreamReader::EndElement) {
                end();
            }
        }
        if (r_.hasError()) {
            throw std::runtime_error("trust list XML, line " + std::to_string(r_.lineNumber()) + ": " +
                                     r_.errorString().toStdString());
        }
        if (!root_) {
            throw std::runtime_error("not a TrustServiceStatusList");
        }
        out_.tl.signers = out_.lotl.signers;
        out_.tl.sequence = out_.lotl.sequence;
        out_.tl.issued = out_.lotl.issued;
        out_.tl.nextUpdate = out_.lotl.nextUpdate;
        return std::move(out_);
    }

private:
    bool in(const char* name) const { return stack_.contains(QLatin1String(name)); }

    void start() {
        const QString name = r_.name().toString();
        if (stack_.isEmpty()) {
            if (name != QLatin1String("TrustServiceStatusList")) {
                throw std::runtime_error("not a TrustServiceStatusList: <" + name.toStdString() + ">");
            }
            root_ = true;
        }
        stack_.push_back(name);
        if (name == QLatin1String("OtherTSLPointer")) {
            pointer_ = {};
        } else if (name == QLatin1String("TrustServiceProvider")) {
            provider_.clear();
            providerEn_ = false;
        } else if (name == QLatin1String("TSPService")) {
            service_ = {};
            serviceEn_ = false;
        }
        static const QStringList kLeaves = {QStringLiteral("X509Certificate"),
                                            QStringLiteral("TSLSequenceNumber"),
                                            QStringLiteral("ListIssueDateTime"),
                                            QStringLiteral("dateTime"),
                                            QStringLiteral("TSLType"),
                                            QStringLiteral("SchemeTerritory"),
                                            QStringLiteral("MimeType"),
                                            QStringLiteral("TSLLocation"),
                                            QStringLiteral("ServiceTypeIdentifier"),
                                            QStringLiteral("ServiceStatus"),
                                            QStringLiteral("StatusStartingTime"),
                                            QStringLiteral("Name")};
        if (!kLeaves.contains(name)) {
            return;
        }
        const QString lang = r_.attributes().value(kXmlNs, QStringLiteral("lang")).toString();
        const QString text = r_.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        leaf(name, text.toStdString(), lang);
        stack_.pop_back();  // readElementText съел конец элемента
    }

    void end() {
        if (stack_.isEmpty()) {
            return;
        }
        const QString name = stack_.takeLast();
        if (name == QLatin1String("OtherTSLPointer")) {
            out_.lotl.pointers.push_back(std::move(pointer_));
            pointer_ = {};
        } else if (name == QLatin1String("TSPService")) {
            service_.provider = provider_;
            out_.tl.services.push_back(std::move(service_));
            service_ = {};
        }
    }

    void leaf(const QString& name, std::string text, const QString& lang) {
        const bool pointer = in("OtherTSLPointer");
        const bool history = in("ServiceHistory");
        const bool scheme = in("SchemeInformation") && !pointer;
        if (name == QLatin1String("X509Certificate")) {
            sign::Bytes der;
            try {
                der = sign::ossl::base64Decode(text);
            } catch (const std::exception&) {
                return;
            }
            if (in("Signature")) {
                out_.lotl.signers.push_back(std::move(der));
            } else if (pointer) {
                pointer_.signers.push_back(std::move(der));
            } else if (in("TSPService") && in("ServiceDigitalIdentity") && !history) {
                service_.certificates.push_back(std::move(der));
            }
        } else if (name == QLatin1String("TSLSequenceNumber") && scheme) {
            out_.lotl.sequence = std::atoi(text.c_str());
        } else if (name == QLatin1String("ListIssueDateTime") && scheme) {
            out_.lotl.issued = std::move(text);
        } else if (name == QLatin1String("dateTime") && scheme && in("NextUpdate")) {
            out_.lotl.nextUpdate = std::move(text);
        } else if (name == QLatin1String("TSLType")) {
            (pointer ? pointer_.type : out_.tl.type) = std::move(text);
        } else if (name == QLatin1String("SchemeTerritory")) {
            (pointer ? pointer_.territory : out_.tl.territory) = std::move(text);
        } else if (name == QLatin1String("MimeType") && pointer) {
            pointer_.mimeType = std::move(text);
        } else if (name == QLatin1String("TSLLocation") && pointer) {
            pointer_.location = std::move(text);
        } else if (in("TSPService") && !history) {
            if (name == QLatin1String("ServiceTypeIdentifier") && in("ServiceInformation")) {
                service_.type = std::move(text);
            } else if (name == QLatin1String("ServiceStatus") && in("ServiceInformation")) {
                service_.status = std::move(text);
            } else if (name == QLatin1String("StatusStartingTime") && in("ServiceInformation")) {
                service_.statusStarting = std::move(text);
            } else if (name == QLatin1String("Name") && in("ServiceName")) {
                const bool en = lang == QLatin1String("en");
                if (service_.name.empty() || (en && !serviceEn_)) {
                    service_.name = std::move(text);
                    serviceEn_ = en;
                }
            }
        } else if (name == QLatin1String("Name") && in("TSPName") && in("TSPInformation")) {
            const bool en = lang == QLatin1String("en");
            if (provider_.empty() || (en && !providerEn_)) {
                provider_ = std::move(text);
                providerEn_ = en;
            }
        }
    }

    QXmlStreamReader r_;
    QStringList stack_;
    bool root_ = false;
    Parsed out_;
    sign::ListPointer pointer_;
    sign::TrustService service_;
    bool serviceEn_ = false;
    std::string provider_;
    bool providerEn_ = false;
};

QJsonArray certificatesToJson(const std::vector<sign::ListedCertificate>& certs) {
    QJsonArray a;
    for (const sign::ListedCertificate& c : certs) {
        QJsonObject o;
        o[QStringLiteral("der")] = QString::fromStdString(sign::ossl::base64Encode(c.der));
        o[QStringLiteral("flags")] = static_cast<int>(c.flags);
        o[QStringLiteral("label")] = QString::fromStdString(c.label);
        a.push_back(o);
    }
    return a;
}

}  // namespace

sign::Lotl parseLotl(const QByteArray& xml) { return ListParser(xml).run().lotl; }

sign::TrustedList parseTrustedList(const QByteArray& xml) { return ListParser(xml).run().tl; }

sign::LotlPins lotlPins() {
    QFile f(QStringLiteral(":/trust/lotl_pins.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("LOTL pins resource is missing");
    }
    return sign::LotlPins::parse(f.readAll().toStdString());
}

HttpTrustListSource::HttpTrustListSource(net::HttpFetcher& fetcher, std::string lotlUrl)
    : fetcher_(fetcher), lotlUrl_(std::move(lotlUrl)) {}

QByteArray HttpTrustListSource::get(const std::string& url, std::size_t limit) {
    net::Request req;
    req.method = "GET";
    req.url = url;
    req.timeoutMs = 60000;
    req.sameHostRedirectsOnly = false;
    req.maxResponseBytes = limit;
    req.purpose = net::Purpose::Signatures;
    req.headers.emplace_back("Accept", "application/vnd.etsi.tsl+xml, application/xml, text/xml");
    const net::Response resp = fetcher_.fetch(req);
    if (!resp.ok()) {
        throw std::runtime_error(url + ": " + net::statusName(resp.status) +
                                 (resp.error.empty() ? std::string() : " (" + resp.error + ")"));
    }
    return QByteArray(reinterpret_cast<const char*>(resp.body.data()),
                      static_cast<qsizetype>(resp.body.size()));
}

sign::Lotl HttpTrustListSource::lotl() { return parseLotl(get(lotlUrl_, 8u << 20)); }

sign::TrustedList HttpTrustListSource::list(const sign::ListPointer& pointer) {
    if (pointer.location.rfind("https://", 0) != 0 && pointer.location.rfind("http://", 0) != 0) {
        throw std::runtime_error("unsupported trusted list location " + pointer.location);
    }
    return parseTrustedList(get(pointer.location, 64u << 20));
}

int TrustListUpdate::aatlCount() const {
    return aatl && aatl->status == sign::AatlStatus::Ok ? static_cast<int>(aatl->certificates.size()) : 0;
}

TrustListUpdate downloadTrustLists(net::HttpFetcher& fetcher, bool eutl, bool aatl, std::time_t now) {
    TrustListUpdate u;
    u.checked =
        now != 0 ? QDateTime::fromSecsSinceEpoch(now, QTimeZone::UTC) : QDateTime::currentDateTimeUtc();
    if (eutl) {
        try {
            HttpTrustListSource source(fetcher);
            sign::EutlOptions options;
            options.now = now;
            u.eutl = sign::buildEutl(source, lotlPins(), options);
        } catch (const std::exception& e) {
            u.eutlError = e.what();
            spdlog::info("trust lists: EUTL: {}", e.what());
        }
    }
    if (aatl) {
        net::Request req;
        req.method = "GET";
        req.url = kAatlUrl;
        req.timeoutMs = 60000;
        req.sameHostRedirectsOnly = false;
        req.maxResponseBytes = 32u << 20;
        req.purpose = net::Purpose::Signatures;
        const net::Response resp = fetcher.fetch(req);
        if (resp.ok()) {
            u.aatl = sign::parseAatl(std::string(resp.body.begin(), resp.body.end()));
        } else {
            u.aatlError = std::string(kAatlUrl) + ": " + net::statusName(resp.status);
            spdlog::info("trust lists: AATL: {}", u.aatlError);
        }
    }
    return u;
}

void saveTrustListCache(const QString& dir, const QString& source, const TrustListCache& cache) {
    QJsonObject o;
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("source")] = source;
    o[QStringLiteral("fetched")] = cache.fetched.toUTC().toString(Qt::ISODate);
    o[QStringLiteral("sequence")] = cache.sequence;
    o[QStringLiteral("nextUpdate")] = cache.nextUpdate;
    o[QStringLiteral("signer")] = cache.signer;
    o[QStringLiteral("certificates")] = certificatesToJson(cache.certificates);
    QDir().mkpath(dir);
    QSaveFile f(QDir(dir).filePath(source + QStringLiteral(".json")));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) < 0 ||
        !f.commit()) {
        throw std::runtime_error("cannot write " + f.fileName().toStdString());
    }
}

std::optional<TrustListCache> loadTrustListCache(const QString& dir, const QString& source) {
    QFile f(QDir(dir).filePath(source + QStringLiteral(".json")));
    if (!f.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        spdlog::warn("trust lists: {} is damaged", f.fileName().toStdString());
        return std::nullopt;
    }
    const QJsonObject o = doc.object();
    TrustListCache c;
    c.fetched = QDateTime::fromString(o.value(QStringLiteral("fetched")).toString(), Qt::ISODate);
    c.sequence = o.value(QStringLiteral("sequence")).toInt();
    c.nextUpdate = o.value(QStringLiteral("nextUpdate")).toString();
    c.signer = o.value(QStringLiteral("signer")).toString();
    for (const QJsonValue& v : o.value(QStringLiteral("certificates")).toArray()) {
        const QJsonObject co = v.toObject();
        sign::ListedCertificate lc;
        try {
            lc.der = sign::ossl::base64Decode(co.value(QStringLiteral("der")).toString().toStdString());
        } catch (const std::exception&) {
            continue;
        }
        lc.flags = static_cast<unsigned>(co.value(QStringLiteral("flags")).toInt(sign::kTrustSignatures));
        lc.label = co.value(QStringLiteral("label")).toString().toStdString();
        c.certificates.push_back(std::move(lc));
    }
    return c;
}

int installTrustLists(const TrustListUpdate& update, sign::TrustStore& store, const QString& dir) {
    int installed = 0;
    if (update.eutlCount() > 0) {
        TrustListCache c;
        c.fetched = update.checked;
        c.sequence = update.eutl->lotlSequence;
        c.nextUpdate = QString::fromStdString(update.eutl->lotlNextUpdate);
        c.certificates = update.eutl->certificates;
        saveTrustListCache(dir, QStringLiteral("eutl"), c);
        installed += store.replaceSource(sign::TrustSource::Eutl, c.certificates);
    }
    if (update.aatlCount() > 0) {
        TrustListCache c;
        c.fetched = update.checked;
        c.signer = QString::fromStdString(update.aatl->signer);
        c.certificates = update.aatl->certificates;
        saveTrustListCache(dir, QStringLiteral("aatl"), c);
        installed += store.replaceSource(sign::TrustSource::Aatl, c.certificates);
    }
    store.save(std::filesystem::path(dir.toStdWString()));
    spdlog::info("trust lists: {} anchors installed (EUTL {}, AATL {})", installed, update.eutlCount(),
                 update.aatlCount());
    return installed;
}

}  // namespace ot
