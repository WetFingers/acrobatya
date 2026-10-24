#include "support/sign_ui.h"

#include <QDir>
#include <QFile>

#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/ts.h>

#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/sign/ossl.h"
#include "core/util/paths.h"
#include "services/trust_service.h"

namespace ot::test {

namespace {

std::string pkiFile(const char* name) {
    QFile f(fixturePath(QStringLiteral("pki/") + QLatin1String(name)));
    if (!f.open(QIODevice::ReadOnly)) {
        throw std::runtime_error(std::string("no PKI fixture ") + name);
    }
    const QByteArray bytes = f.readAll();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

ot::sign::Ossl<X509> pemCert(const char* name) {
    const std::string pem = pkiFile(name);
    ot::sign::Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    return ot::sign::Ossl<X509>(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

ot::sign::Ossl<EVP_PKEY> pemKey(const char* name) {
    const std::string pem = pkiFile(name);
    ot::sign::Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    return ot::sign::Ossl<EVP_PKEY>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

// Ответ службы времени PKI (tsa.pem/tsa.key, политика 1.2.3.4.1) — как tests/unit/support/pki.cpp.
std::vector<std::uint8_t> tsaReply(std::span<const std::uint8_t> query) {
    ot::sign::Ossl<TS_RESP_CTX> ctx(TS_RESP_CTX_new());
    const auto cert = pemCert("tsa.pem");
    const auto key = pemKey("tsa.key");
    ot::sign::Ossl<STACK_OF(X509)> certs(sk_X509_new_null());
    sk_X509_push(certs.get(), pemCert("inter.pem").release());
    sk_X509_push(certs.get(), pemCert("root.pem").release());
    ot::sign::Ossl<ASN1_OBJECT> policy(OBJ_txt2obj("1.2.3.4.1", 1));
    if (!ctx || TS_RESP_CTX_set_signer_cert(ctx.get(), cert.get()) != 1 ||
        TS_RESP_CTX_set_signer_key(ctx.get(), key.get()) != 1 ||
        TS_RESP_CTX_set_certs(ctx.get(), certs.get()) != 1 ||
        TS_RESP_CTX_set_def_policy(ctx.get(), policy.get()) != 1 ||
        TS_RESP_CTX_set_signer_digest(ctx.get(), EVP_sha256()) != 1 ||
        TS_RESP_CTX_set_ess_cert_id_digest(ctx.get(), EVP_sha256()) != 1 ||
        TS_RESP_CTX_set_accuracy(ctx.get(), 1, 0, 0) != 1) {
        throw ot::sign::OsslError::fromQueue("TS_RESP_CTX");
    }
    for (const EVP_MD* md : {EVP_sha1(), EVP_sha256(), EVP_sha384(), EVP_sha512()}) {
        TS_RESP_CTX_add_md(ctx.get(), md);
    }
    TS_RESP_CTX_add_flags(ctx.get(), TS_TSA_NAME | TS_ORDERING);
    auto in = ot::sign::ossl::memBio(query);
    ot::sign::Ossl<TS_RESP> resp(TS_RESP_create_response(ctx.get(), in.get()));
    if (!resp) {
        throw ot::sign::OsslError::fromQueue("TS_RESP_create_response");
    }
    return ot::sign::ossl::toDer(resp.get(), i2d_TS_RESP, "i2d_TS_RESP");
}

ot::net::Response okResponse(const std::string& body, const char* type, const std::string& url) {
    ot::net::Response r;
    r.status = ot::net::Status::Ok;
    r.httpCode = 200;
    r.contentType = type;
    r.body.assign(body.begin(), body.end());
    r.finalUrl = url;
    return r;
}

}  // namespace

QString fixturePath(const QString& rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + rel; }

QString copyFixture(const QString& rel, const QString& dir, const QString& name) {
    const QString to = QDir(dir).filePath(name);
    QFile::remove(to);
    if (!QFile::copy(fixturePath(rel), to)) {
        return {};
    }
    QFile(to).setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return to;
}

QString pkiPfx(const char* name) { return fixturePath(QStringLiteral("pki/") + QLatin1String(name)); }

PkiNetwork::PkiNetwork() {
    fake_.setHandler([this](const ot::net::Request& r) {
        if (r.url == "http://pki.acrobatya.test/ocsp") {
            const unsigned char* p = r.body.data();
            ot::sign::Ossl<OCSP_REQUEST> req(d2i_OCSP_REQUEST(nullptr, &p, static_cast<long>(r.body.size())));
            long serial = 0;
            if (req && OCSP_request_onereq_count(req.get()) == 1) {
                ASN1_INTEGER* s = nullptr;
                OCSP_id_get0_info(nullptr, nullptr, nullptr, &s,
                                  OCSP_onereq_get0_id(OCSP_request_onereq_get0(req.get(), 0)));
                serial = ASN1_INTEGER_get(s);
            }
            const char* file = serial == 0x2000   ? "ocsp-good.der"
                               : serial == 0x2001 ? "ocsp-ec-good.der"
                               : serial == 0x2002 ? "ocsp-revoked.der"
                                                  : "ocsp-unknown.der";
            return okResponse(pkiFile(file), "application/ocsp-response", r.url);
        }
        static const std::pair<const char*, const char*> kFiles[] = {
            {"http://pki.acrobatya.test/root.crl", "root.crl"},
            {"http://pki.acrobatya.test/inter.crl", "inter-empty.crl"},
            {"http://pki.acrobatya.test/root.cer", "root.cer"},
            {"http://pki.acrobatya.test/inter.cer", "inter.cer"}};
        for (const auto& [url, file] : kFiles) {
            if (r.url == url) {
                return okResponse(pkiFile(file), "application/octet-stream", r.url);
            }
        }
        if (r.contentType == "application/timestamp-query") {
            ++tsaRequests_;
            if (tsaDown_) {
                ot::net::Response down;
                down.status = ot::net::Status::NetworkError;
                down.error = "TSA is down (test)";
                return down;
            }
            const std::vector<std::uint8_t> reply = tsaReply(r.body);
            return okResponse(std::string(reply.begin(), reply.end()), "application/timestamp-reply", r.url);
        }
        ot::net::Response none;
        none.status = ot::net::Status::HttpError;
        none.httpCode = 404;
        return none;
    });
    scope_ = std::make_unique<ot::ScopedHttpFetcher>(fake_);
}

PkiNetwork::~PkiNetwork() = default;

ot::sign::TrustStore pkiTrust() {
    ot::sign::TrustStore t;
    const std::string root = pkiFile("root.cer");
    t.add(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(root.data()), root.size()),
          ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    return t;
}

bool trustPkiRoot() {
    const std::string root = pkiFile("root.cer");
    return ot::TrustService::shared().edit([&](ot::sign::TrustStore& s) {
        return s.add(
            std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(root.data()), root.size()),
            ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    });
}

ot::sign::DocumentVerification verifyFile(const QString& path, const ot::sign::TrustStore* trust) {
    auto doc = ot::Document::openOrThrow(std::filesystem::path(path.toStdWString()));
    ot::sign::VerifyOptions o;
    o.trust = trust;
    o.revocation = ot::sign::RevocationPolicy::Never;
    return ot::sign::verifyDocument(*doc, o);
}

}  // namespace ot::test
