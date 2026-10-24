#include "support/verify_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <regex>
#include <stdexcept>

#include <openssl/ocsp.h>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/sign_field.h"
#include "core/sign/signer.h"
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
    if (want.contains("level")) {
        EXPECT_EQ(std::string(ot::sign::padesLevelName(sig.level)), want["level"].get<std::string>());
    }
    if (want.contains("timestamped")) {
        EXPECT_EQ(sig.timestamp.has_value(), want["timestamped"].get<bool>());
    }
    if (want.contains("secureTime")) {
        EXPECT_EQ(sig.timeSource == ot::sign::TimeSource::Timestamp && sig.timestampTrusted,
                  want["secureTime"].get<bool>());
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

void certify(const fs::path& file, int p) {
    auto doc = ot::Document::openOrThrow(file);
    auto signer = std::make_shared<ot::sign::AcroBatyaSigner>(pkiIdentity());
    ot::sign::detail::FieldJob job;
    job.operation = "Certify document";
    job.placement.field = "Certification";
    job.time = static_cast<std::int64_t>(std::time(nullptr));
    job.patch = [p](fz_context* ctx, pdf_document* pdf, pdf_obj* v, bool) {
        ot::mu::call(ctx, [&] {
            pdf_dict_put(ctx, v, PDF_NAME(SubFilter), PDF_NAME(adbe_pkcs7_detached));
            pdf_obj* refs = pdf_dict_put_array(ctx, v, PDF_NAME(Reference), 1);
            pdf_obj* ref = pdf_array_push_dict(ctx, refs, 3);
            pdf_dict_put(ctx, ref, PDF_NAME(Type), PDF_NAME(SigRef));
            pdf_dict_put(ctx, ref, PDF_NAME(TransformMethod), PDF_NAME(DocMDP));
            pdf_obj* params = pdf_dict_put_dict(ctx, ref, PDF_NAME(TransformParams), 3);
            pdf_dict_put(ctx, params, PDF_NAME(Type), PDF_NAME(TransformParams));
            pdf_dict_put_int(ctx, params, PDF_NAME(P), p);
            pdf_dict_put_name(ctx, params, PDF_NAME(V), "1.2");
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
            pdf_obj* perms = pdf_dict_put_dict(ctx, root, PDF_NAME(Perms), 1);
            pdf_dict_put(ctx, perms, PDF_NAME(DocMDP), v);
        });
    };
    (void)ot::sign::detail::signField(*doc, job, *signer, {});
}

void editIncrementally(const fs::path& file,
                       const std::function<void(fz_context*, pdf_document*, pdf_page*)>& edit) {
    auto doc = ot::Document::openOrThrow(file);
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    {
        ot::Journal::Operation op(*doc, "Edit after signing");
        ot::mu::PageHandle page(ctx);
        ot::mu::call(ctx, [&] { page.reset(fz_load_page(ctx, &pdf->super, 0)); });
        ot::mu::call(ctx, [&] { edit(ctx, pdf, pdf_page_from_fz_page(ctx, page)); });
        op.commit();
    }
    ot::SaveOptions options;
    options.mode = ot::SaveMode::Incremental;
    const ot::SaveReport r = ot::Saver::save(*doc, options);
    if (!r.ok()) {
        throw std::runtime_error("incremental save failed: " + r.message);
    }
}

void setTextField(fz_context* ctx, pdf_page* page, const char* field, const char* value) {
    for (pdf_annot* w = pdf_first_widget(ctx, page); w != nullptr; w = pdf_next_widget(ctx, w)) {
        char* name = pdf_load_field_name(ctx, pdf_annot_obj(ctx, w));
        const bool match = name != nullptr && std::strcmp(name, field) == 0;
        fz_free(ctx, name);
        if (match) {
            pdf_set_text_field_value(ctx, w, value);
            return;
        }
    }
    fz_throw(ctx, FZ_ERROR_ARGUMENT, "no field %s", field);
}

void addTextNote(fz_context* ctx, pdf_page* page) {
    pdf_annot* a = pdf_create_annot(ctx, page, PDF_ANNOT_TEXT);
    pdf_set_annot_rect(ctx, a, fz_make_rect(20, 20, 40, 40));
    pdf_set_annot_contents(ctx, a, "Note after signing");
    pdf_update_annot(ctx, a);
    pdf_drop_annot(ctx, a);
}

}  // namespace ot::test
