#include "support/verify_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <regex>
#include <stdexcept>

#include <openssl/ocsp.h>

#include "core/util/paths.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::sign::Bytes;
using ot::sign::SignatureInfo;

namespace ot::test {

namespace {

fs::path corpusDir() {
    if (const char* env = std::getenv("OTTISK_CORPUS"); env != nullptr && *env != '\0') {
        return fs::path(ot::paths::fromUtf8(env));
    }
    return fs::path(L"C:\\Tools\\ottisk\\corpus");
}

std::string kindName(ot::sign::SignatureKind k) { return ot::sign::signatureKindName(k); }

bool hasIssueNamed(const SignatureInfo& sig, const std::string& name) {
    return std::any_of(sig.findings.begin(), sig.findings.end(),
                       [&](const ot::sign::Finding& f) { return name == ot::sign::issueName(f.issue); });
}

std::string findingsText(const SignatureInfo& sig) {
    std::string out;
    for (const ot::sign::Finding& f : sig.findings) {
        out += std::string(ot::sign::issueName(f.issue)) + " (" + f.detail + "); ";
    }
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

}  // namespace

std::optional<fs::path> dssFile(const std::string& rel) {
    const fs::path p = corpusDir() / L"dss" / L"dss-pades" / L"src" / L"test" / L"resources" / L"validation" /
                       ot::paths::fromUtf8(rel);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return std::nullopt;
    }
    return p;
}

const json& verifierExpectations() {
    static const json j = json::parse(
        ot::paths::readFile(sourceDir() / L"tests" / L"fixtures" / L"sig" / L"verifier_expected.json"));
    return j;
}

const SignatureInfo* findSignature(const ot::sign::DocumentVerification& v, const std::string& field) {
    for (const SignatureInfo& s : v.signatures) {
        if (s.field == field) {
            return &s;
        }
    }
    return nullptr;
}

void expectSignature(const SignatureInfo& sig, const json& want, const std::string& what) {
    SCOPED_TRACE(what + " / " + sig.field + ": " + findingsText(sig));
    if (want.contains("structureOk")) {
        EXPECT_EQ(sig.structureOk, want["structureOk"].get<bool>());
    }
    if (want.contains("intact")) {
        EXPECT_EQ(sig.intact, want["intact"].get<bool>());
    }
    if (want.contains("coversWholeFile")) {
        EXPECT_EQ(sig.coversWholeFile, want["coversWholeFile"].get<bool>());
    }
    if (want.contains("kind")) {
        EXPECT_EQ(kindName(sig.kind), want["kind"].get<std::string>());
    }
    if (want.contains("subFilter")) {
        EXPECT_EQ(sig.subFilter, want["subFilter"].get<std::string>());
    }
    if (want.contains("signedAttributes")) {
        EXPECT_EQ(sig.signedAttributes, want["signedAttributes"].get<bool>());
    }
    if (want.contains("verdict")) {
        EXPECT_EQ(std::string(ot::sign::verdictName(sig.verdict)), want["verdict"].get<std::string>());
    }
    for (const auto& name : want.value("issues", json::array())) {
        EXPECT_TRUE(hasIssueNamed(sig, name.get<std::string>())) << "нет " << name.get<std::string>();
    }
    for (const auto& name : want.value("noIssues", json::array())) {
        EXPECT_FALSE(hasIssueNamed(sig, name.get<std::string>())) << "лишнее " << name.get<std::string>();
    }
}

void patchContents(const fs::path& file, int sigIndex, const std::function<void(Bytes&)>& edit) {
    std::string bytes = ot::paths::readFile(file);
    static const std::regex kBr(R"(/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\])");
    auto it = std::sregex_iterator(bytes.begin(), bytes.end(), kBr);
    for (int i = 0; i < sigIndex && it != std::sregex_iterator(); ++i) {
        ++it;
    }
    if (it == std::sregex_iterator()) {
        throw std::runtime_error("no /ByteRange #" + std::to_string(sigIndex));
    }
    const std::size_t holeBegin = std::stoull((*it)[2].str()) + 1;  // после «<»
    const std::size_t holeEnd = std::stoull((*it)[3].str()) - 1;    // до «>»
    Bytes der;
    for (std::size_t i = holeBegin; i + 1 < holeEnd; i += 2) {
        der.push_back(static_cast<std::uint8_t>(hexValue(bytes[i]) * 16 + hexValue(bytes[i + 1])));
    }
    const std::size_t used = std::min(der.size(), derLength(der));
    der.resize(used);
    edit(der);
    if (der.size() * 2 > holeEnd - holeBegin) {
        throw std::runtime_error("patched DER does not fit /Contents");
    }
    static const char* kHex = "0123456789abcdef";
    for (std::size_t i = 0; i < (holeEnd - holeBegin) / 2; ++i) {
        const std::uint8_t b = i < der.size() ? der[i] : 0;
        bytes[holeBegin + 2 * i] = kHex[b >> 4];
        bytes[holeBegin + 2 * i + 1] = kHex[b & 15];
    }
    ot::paths::writeFile(file, bytes);
}

void patchBytes(const fs::path& file, const std::string& from, const std::string& to) {
    if (from.size() != to.size()) {
        throw std::invalid_argument("patchBytes: lengths differ");
    }
    std::string bytes = ot::paths::readFile(file);
    const std::size_t at = bytes.find(from);
    if (at == std::string::npos) {
        throw std::runtime_error("patchBytes: not found: " + from);
    }
    bytes.replace(at, from.size(), to);
    ot::paths::writeFile(file, bytes);
}

namespace {

Bytes pkiBytes(const char* name) {
    const std::string s = readPki(name);
    return Bytes(s.begin(), s.end());
}

ot::net::Response ok(Bytes body, const char* type, const std::string& url) {
    ot::net::Response resp;
    resp.status = ot::net::Status::Ok;
    resp.httpCode = 200;
    resp.contentType = type;
    resp.body = std::move(body);
    resp.finalUrl = url;
    return resp;
}

}  // namespace

std::shared_ptr<ot::net::FakeHttpFetcher> pkiResponder() {
    auto fake = std::make_shared<ot::net::FakeHttpFetcher>();
    fake->setHandler([](const ot::net::Request& r) {
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
            return ok(pkiBytes(file), "application/ocsp-response", r.url);
        }
        static const std::pair<const char*, const char*> kFiles[] = {
            {"http://pki.acrobatya.test/root.crl", "root.crl"},
            {"http://pki.acrobatya.test/inter.crl", "inter-revoked.crl"},
            {"http://pki.acrobatya.test/root.cer", "root.cer"},
            {"http://pki.acrobatya.test/inter.cer", "inter.cer"}};
        for (const auto& [url, file] : kFiles) {
            if (r.url == url) {
                return ok(pkiBytes(file), "application/octet-stream", r.url);
            }
        }
        ot::net::Response none;
        none.status = ot::net::Status::HttpError;
        none.httpCode = 404;
        return none;
    });
    return fake;
}

std::shared_ptr<ot::net::FakeHttpFetcher> offlineFetcher() {
    auto fake = std::make_shared<ot::net::FakeHttpFetcher>();
    fake->setHandler([](const ot::net::Request&) {
        ot::net::Response resp;
        resp.status = ot::net::Status::NetworkError;
        resp.error = "offline (test)";
        return resp;
    });
    return fake;
}

std::shared_ptr<const ot::sign::SigningKey> revokedIdentity() {
    auto k = std::make_shared<ot::sign::SigningKey>();
    k->cert = pkiCert("revoked.pem");
    k->key = pkiKey("revoked.key");
    k->chain.reset(sk_X509_new_null());
    sk_X509_push(k->chain.get(), pkiCert("inter.pem").release());
    return k;
}

ot::sign::TrustStore embeddedRoots(const ot::sign::DocumentVerification& v) {
    ot::sign::TrustStore store;
    for (const ot::sign::CertSummary& c : v.embeddedCertificates) {
        if (c.subject == c.issuer) {
            store.add(c.der, ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
        }
    }
    return store;
}

std::int64_t utc(int year, int month, int day) {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    return static_cast<std::int64_t>(_mkgmtime(&tm));
}

}  // namespace ot::test
