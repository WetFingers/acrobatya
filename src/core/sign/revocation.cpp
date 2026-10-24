#include "core/sign/revocation.h"

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

#include <spdlog/spdlog.h>

#include <stdexcept>

namespace ot::sign {

namespace {

// Без nextUpdate OCSP-ответ считается свежим неделю после thisUpdate.
constexpr std::time_t kOcspMaxAge = 7 * 24 * 3600;

std::optional<std::time_t> revAsnTime(const ASN1_TIME* t) {
    std::tm tm{};
    if (t == nullptr || ASN1_TIME_to_tm(t, &tm) != 1) {
        ERR_clear_error();
        return std::nullopt;
    }
    return _mkgmtime(&tm);
}

std::string revIso(std::time_t t) {
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

bool revHttp(const std::string& url) { return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0; }

// Пусто — данные действуют на момент t; иначе — почему нет.
std::string windowProblem(std::optional<std::time_t> thisUpdate, std::optional<std::time_t> nextUpdate,
                          std::time_t t, bool retroactive, int leeway, bool ocsp) {
    if (!thisUpdate) {
        return "has no thisUpdate";
    }
    if (!retroactive && t + leeway < *thisUpdate) {
        return "not yet valid at " + revIso(t) + " (thisUpdate " + revIso(*thisUpdate) + ")";
    }
    if (nextUpdate) {
        if (t > *nextUpdate + leeway) {
            return "expired at " + revIso(t) + " (nextUpdate " + revIso(*nextUpdate) + ")";
        }
    } else if (ocsp && t > *thisUpdate + kOcspMaxAge) {
        return "too old at " + revIso(t) + " (no nextUpdate, thisUpdate " + revIso(*thisUpdate) + ")";
    }
    return {};
}

bool hasOcspNoCheck(X509* cert) { return X509_get_ext_by_NID(cert, NID_id_pkix_OCSP_noCheck, -1) >= 0; }

}  // namespace

// Итог одного этапа (OCSP или CRL): отзыв главнее, затем «действует» с самыми свежими данными; данные о
// сертификате, которые нельзя использовать, — причина для Unknown.
struct RevocationChecker::Verdict {
    std::optional<RevocationResult> revoked;
    std::optional<RevocationResult> good;
    std::optional<RevocationResult> pending;
    std::vector<std::string> notes;

    void add(RevocationResult r) {
        if (r.status == RevocationStatus::Revoked) {
            if (!revoked) {
                revoked = std::move(r);
            }
        } else if (r.status == RevocationStatus::Good) {
            if (!good || r.thisUpdate > good->thisUpdate) {
                good = std::move(r);
            }
        } else if (!pending) {
            pending = std::move(r);
        }
    }
};

const char* revocationStatusName(RevocationStatus status) {
    switch (status) {
        case RevocationStatus::Good:
            return "good";
        case RevocationStatus::Revoked:
            return "revoked";
        case RevocationStatus::Unknown:
            return "unknown";
    }
    return "?";
}

const char* revocationReasonName(RevocationReason reason) {
    switch (reason) {
        case RevocationReason::None:
            return "";
        case RevocationReason::Unspecified:
            return "unspecified";
        case RevocationReason::KeyCompromise:
            return "keyCompromise";
        case RevocationReason::CaCompromise:
            return "cACompromise";
        case RevocationReason::AffiliationChanged:
            return "affiliationChanged";
        case RevocationReason::Superseded:
            return "superseded";
        case RevocationReason::CessationOfOperation:
            return "cessationOfOperation";
        case RevocationReason::CertificateHold:
            return "certificateHold";
        case RevocationReason::RemoveFromCrl:
            return "removeFromCRL";
        case RevocationReason::PrivilegeWithdrawn:
            return "privilegeWithdrawn";
        case RevocationReason::AaCompromise:
            return "aACompromise";
    }
    return "";
}

RevocationStatus worstStatus(const std::vector<RevocationResult>& results) {
    RevocationStatus worst = RevocationStatus::Good;
    for (const RevocationResult& r : results) {
        if (r.status == RevocationStatus::Revoked) {
            return RevocationStatus::Revoked;
        }
        if (r.status == RevocationStatus::Unknown) {
            worst = RevocationStatus::Unknown;
        }
    }
    return worst;
}

RevocationChecker::RevocationChecker(RevocationData known, net::HttpFetcher* fetcher)
    : known_(std::move(known)), fetcher_(fetcher) {}

std::time_t RevocationChecker::when() const { return time_.value_or(std::time(nullptr)); }

void RevocationChecker::judgeOcsp(const Bytes& der, X509* cert, X509* issuer, OCSP_REQUEST* sent,
                                  Verdict& v) const {
    ERR_clear_error();
    const unsigned char* p = der.data();
    Ossl<OCSP_RESPONSE> resp(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(der.size())));
    Ossl<OCSP_BASICRESP> bs;
    if (resp) {
        const int st = OCSP_response_status(resp.get());
        if (st != OCSP_RESPONSE_STATUS_SUCCESSFUL) {
            v.notes.push_back(std::string("OCSP responder status: ") + OCSP_response_status_str(st));
            return;
        }
        bs.reset(OCSP_response_get1_basic(resp.get()));
    } else {
        p = der.data();
        bs.reset(d2i_OCSP_BASICRESP(nullptr, &p, static_cast<long>(der.size())));  // DSS иногда хранит basic
    }
    if (!bs) {
        ERR_clear_error();
        v.notes.emplace_back("OCSP: not an OCSP response");
        return;
    }
    // Ответ о нашем сертификате: CertID по алгоритму самого ответа.
    OCSP_SINGLERESP* single = nullptr;
    for (int i = 0; i < OCSP_resp_count(bs.get()) && single == nullptr; ++i) {
        OCSP_SINGLERESP* s = OCSP_resp_get0(bs.get(), i);
        auto* cid = const_cast<OCSP_CERTID*>(OCSP_SINGLERESP_get0_id(s));
        ASN1_OBJECT* md = nullptr;
        if (OCSP_id_get0_info(nullptr, &md, nullptr, nullptr, cid) != 1 || md == nullptr) {
            continue;
        }
        const EVP_MD* evp = EVP_get_digestbyobj(md);
        if (evp == nullptr) {
            continue;
        }
        Ossl<OCSP_CERTID> mine(OCSP_cert_to_id(evp, cert, issuer));
        if (mine && OCSP_id_cmp(mine.get(), cid) == 0) {
            single = s;
        }
    }
    ERR_clear_error();
    if (single == nullptr) {
        return;  // не о нём
    }
    RevocationResult r;
    r.source = RevocationSource::Ocsp;
    r.ocspResponse = der;
    X509* signer = nullptr;
    if (OCSP_resp_get0_signer(bs.get(), &signer, nullptr) == 1 && signer != nullptr) {
        r.responder = commonName(X509_get_subject_name(signer));
    }
    // Подпись: сам издатель или делегированный респондер (EKU OCSPSigning), выданный издателем; на момент
    // ответа.
    Ossl<X509_STORE> store(ossl::checkPtr(X509_STORE_new(), "X509_STORE_new"));
    X509_STORE_add_cert(store.get(), issuer);
    X509_STORE_set_flags(store.get(), X509_V_FLAG_PARTIAL_CHAIN);
    if (const auto produced = revAsnTime(OCSP_resp_get0_produced_at(bs.get()))) {
        X509_VERIFY_PARAM_set_time(X509_STORE_get0_param(store.get()), *produced);
    }
    if (OCSP_basic_verify(bs.get(), nullptr, store.get(), 0) != 1) {
        r.detail =
            std::string("OCSP response not verified: ") + OsslError::fromQueue("OCSP_basic_verify").what();
        v.add(std::move(r));
        return;
    }
    if (sent != nullptr && OCSP_check_nonce(sent, bs.get()) == 0) {
        r.detail = "OCSP response nonce does not match the request";
        v.add(std::move(r));
        return;
    }
    int reason = -1;
    ASN1_GENERALIZEDTIME* revoked = nullptr;
    ASN1_GENERALIZEDTIME* thisUpdate = nullptr;
    ASN1_GENERALIZEDTIME* nextUpdate = nullptr;
    const int status = OCSP_single_get0_status(single, &reason, &revoked, &thisUpdate, &nextUpdate);
    r.thisUpdate = timeToIso(thisUpdate);
    r.nextUpdate = timeToIso(nextUpdate);
    const std::time_t t = when();
    const std::string problem =
        windowProblem(revAsnTime(thisUpdate), nextUpdate != nullptr ? revAsnTime(nextUpdate) : std::nullopt,
                      t, retroactive_, leeway_, true);
    if (!problem.empty()) {
        r.detail = "OCSP response " + problem;
        v.add(std::move(r));
        return;
    }
    switch (status) {
        case V_OCSP_CERTSTATUS_GOOD:
            r.status = RevocationStatus::Good;
            r.detail = "OCSP: good";
            break;
        case V_OCSP_CERTSTATUS_REVOKED: {
            r.revocationTime = timeToIso(revoked);
            r.reason = reason >= 0 ? static_cast<RevocationReason>(reason) : RevocationReason::None;
            const auto at = revAsnTime(revoked);
            if (at && *at > t) {
                r.status = RevocationStatus::Good;
                r.detail = "OCSP: revoked after the validation time (" + r.revocationTime + ")";
            } else {
                r.status = RevocationStatus::Revoked;
                r.detail = "OCSP: revoked";
            }
            break;
        }
        default:
            r.detail = "OCSP: responder reports status unknown";
            break;
    }
    v.add(std::move(r));
}

void RevocationChecker::judgeCrl(const Bytes& der, X509* cert, X509* issuer, Verdict& v) const {
    ERR_clear_error();
    const unsigned char* p = der.data();
    Ossl<X509_CRL> crl(d2i_X509_CRL(nullptr, &p, static_cast<long>(der.size())));
    if (!crl) {
        Ossl<BIO> bio = ossl::memBio(der);
        crl.reset(PEM_read_bio_X509_CRL(bio.get(), nullptr, nullptr, nullptr));
    }
    if (!crl) {
        ERR_clear_error();
        v.notes.emplace_back("CRL: not a CRL");
        return;
    }
    if (X509_NAME_cmp(X509_CRL_get_issuer(crl.get()), X509_get_issuer_name(cert)) != 0) {
        return;  // другой издатель
    }
    RevocationResult r;
    r.source = RevocationSource::Crl;
    r.crl = der;
    r.thisUpdate = timeToIso(X509_CRL_get0_lastUpdate(crl.get()));
    r.nextUpdate = timeToIso(X509_CRL_get0_nextUpdate(crl.get()));
    if (X509_CRL_verify(crl.get(), X509_get0_pubkey(issuer)) != 1) {
        ERR_clear_error();
        r.detail = "CRL signature does not match the issuer";
        v.add(std::move(r));
        return;
    }
    const std::time_t t = when();
    const ASN1_TIME* next = X509_CRL_get0_nextUpdate(crl.get());
    const std::string problem =
        windowProblem(revAsnTime(X509_CRL_get0_lastUpdate(crl.get())),
                      next != nullptr ? revAsnTime(next) : std::nullopt, t, retroactive_, leeway_, false);
    if (!problem.empty()) {
        r.detail = "CRL " + problem;
        v.add(std::move(r));
        return;
    }
    X509_REVOKED* entry = nullptr;
    if (X509_CRL_get0_by_cert(crl.get(), &entry, cert) == 1 && entry != nullptr) {
        r.revocationTime = timeToIso(X509_REVOKED_get0_revocationDate(entry));
        int critical = 0;
        Ossl<ASN1_STRING> code(
            static_cast<ASN1_STRING*>(X509_REVOKED_get_ext_d2i(entry, NID_crl_reason, &critical, nullptr)));
        r.reason =
            code ? static_cast<RevocationReason>(ASN1_ENUMERATED_get(code.get())) : RevocationReason::None;
        const auto at = revAsnTime(X509_REVOKED_get0_revocationDate(entry));
        if (at && *at > t) {
            r.status = RevocationStatus::Good;
            r.detail = "CRL: revoked after the validation time (" + r.revocationTime + ")";
        } else {
            r.status = RevocationStatus::Revoked;
            r.detail = "CRL: revoked";
        }
    } else {
        r.status = RevocationStatus::Good;
        r.detail = "CRL: not listed";
    }
    ERR_clear_error();
    v.add(std::move(r));
}

RevocationResult RevocationChecker::check(X509* cert, X509* issuer) {
    if (cert == nullptr || issuer == nullptr) {
        throw std::invalid_argument("RevocationChecker::check: certificate and issuer are required");
    }
    if (hasOcspNoCheck(cert)) {
        RevocationResult r;
        r.status = RevocationStatus::Good;
        r.detail = "id-pkix-ocsp-nocheck: OCSP responder certificate is trusted for its lifetime";
        return r;
    }
    std::optional<RevocationResult> pending;
    std::vector<std::string> notes;
    // Решение этапа или запомнить причину неизвестности.
    const auto decided = [&](Verdict& v) -> std::optional<RevocationResult> {
        if (v.revoked) {
            return v.revoked;
        }
        if (v.good) {
            return v.good;
        }
        if (!pending && v.pending) {
            pending = v.pending;
        }
        notes.insert(notes.end(), v.notes.begin(), v.notes.end());
        return std::nullopt;
    };

    if (ocsp_) {
        Verdict v;
        for (const Bytes& der : known_.ocspResponses) {
            judgeOcsp(der, cert, issuer, nullptr, v);
        }
        if (auto r = decided(v)) {
            return *r;
        }
        for (const std::string& url : fetcher_ != nullptr ? ocspUrls(cert) : std::vector<std::string>{}) {
            if (!revHttp(url)) {
                continue;
            }
            Ossl<OCSP_REQUEST> req(ossl::checkPtr(OCSP_REQUEST_new(), "OCSP_REQUEST_new"));
            OCSP_CERTID* id = ossl::checkPtr(OCSP_cert_to_id(nullptr, cert, issuer), "OCSP_cert_to_id");
            if (OCSP_request_add0_id(req.get(), id) == nullptr) {
                OCSP_CERTID_free(id);
                throw OsslError::fromQueue("OCSP_request_add0_id");
            }
            ossl::check(OCSP_request_add1_nonce(req.get(), nullptr, -1), "OCSP_request_add1_nonce");
            net::Request http;
            http.method = "POST";
            http.url = url;
            http.contentType = "application/ocsp-request";
            http.body = ossl::toDer(req.get(), i2d_OCSP_REQUEST, "i2d_OCSP_REQUEST");
            http.headers.emplace_back("Accept", "application/ocsp-response");
            http.sameHostRedirectsOnly = true;
            http.maxResponseBytes = 1u << 20;
            http.purpose = net::Purpose::Signatures;
            const net::Response resp = fetcher_->fetch(http);
            if (!resp.ok()) {
                notes.push_back("OCSP " + url + ": " + net::statusName(resp.status) +
                                (resp.error.empty() ? std::string() : " (" + resp.error + ")"));
                continue;
            }
            Verdict w;
            judgeOcsp(resp.body, cert, issuer, req.get(), w);
            if (auto r = decided(w)) {
                return *r;
            }
        }
    }
    if (crl_) {
        Verdict v;
        for (const Bytes& der : known_.crls) {
            judgeCrl(der, cert, issuer, v);
        }
        if (auto r = decided(v)) {
            return *r;
        }
        for (const std::string& url : fetcher_ != nullptr ? crlUrls(cert) : std::vector<std::string>{}) {
            if (!revHttp(url)) {
                continue;
            }
            net::Request http;
            http.method = "GET";
            http.url = url;
            http.headers.emplace_back("Accept", "application/pkix-crl");
            http.sameHostRedirectsOnly = false;  // CRL часто на CDN
            http.maxResponseBytes = 32u << 20;
            http.purpose = net::Purpose::Signatures;
            const net::Response resp = fetcher_->fetch(http);
            if (!resp.ok()) {
                notes.push_back("CRL " + url + ": " + net::statusName(resp.status) +
                                (resp.error.empty() ? std::string() : " (" + resp.error + ")"));
                continue;
            }
            Verdict w;
            judgeCrl(resp.body, cert, issuer, w);
            if (auto r = decided(w)) {
                return *r;
            }
        }
    }
    RevocationResult out = pending.value_or(RevocationResult{});
    out.status = RevocationStatus::Unknown;
    if (out.detail.empty()) {
        out.detail = "no revocation data";
    }
    for (const std::string& n : notes) {
        out.detail += "; " + n;
    }
    spdlog::debug("revocation: {}: unknown ({})", commonName(X509_get_subject_name(cert)), out.detail);
    return out;
}

std::vector<RevocationResult> RevocationChecker::checkChain(const ChainResult& chain) {
    std::vector<RevocationResult> out;
    if (!chain.certs) {
        return out;
    }
    const int n = sk_X509_num(chain.certs.get());
    for (int i = 0; i + 1 < n; ++i) {
        out.push_back(check(sk_X509_value(chain.certs.get(), i), sk_X509_value(chain.certs.get(), i + 1)));
    }
    return out;
}

}  // namespace ot::sign
