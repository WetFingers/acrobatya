#include "core/sign/chain.h"

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <stdexcept>

namespace ot::sign {

namespace {

ChainStatus statusFromVerifyError(int err) {
    switch (err) {
        case X509_V_OK:
            return ChainStatus::Valid;
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT:
        case X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY:
        case X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE:
        case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
        case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
        case X509_V_ERR_CERT_UNTRUSTED:
        case X509_V_ERR_CERT_REJECTED:
        case X509_V_ERR_CERT_CHAIN_TOO_LONG:
            return ChainStatus::Untrusted;
        case X509_V_ERR_CERT_HAS_EXPIRED:
        case X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD:
            return ChainStatus::Expired;
        case X509_V_ERR_CERT_NOT_YET_VALID:
        case X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD:
            return ChainStatus::NotYetValid;
        case X509_V_ERR_CERT_REVOKED:
            return ChainStatus::Revoked;
        case X509_V_ERR_UNABLE_TO_GET_CRL:
        case X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER:
        case X509_V_ERR_CRL_NOT_YET_VALID:
        case X509_V_ERR_CRL_HAS_EXPIRED:
        case X509_V_ERR_CRL_SIGNATURE_FAILURE:
        case X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE:
        case X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD:
        case X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD:
        case X509_V_ERR_KEYUSAGE_NO_CRL_SIGN:
        case X509_V_ERR_DIFFERENT_CRL_SCOPE:
        case X509_V_ERR_UNHANDLED_CRITICAL_CRL_EXTENSION:
            return ChainStatus::RevocationUnknown;
        case X509_V_ERR_CERT_SIGNATURE_FAILURE:
        case X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE:
        case X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY:
            return ChainStatus::BadSignature;
        case X509_V_ERR_INVALID_PURPOSE:
        case X509_V_ERR_KEYUSAGE_NO_DIGITAL_SIGNATURE:
            return ChainStatus::InvalidPurpose;
        case X509_V_ERR_INVALID_CA:
        case X509_V_ERR_PATH_LENGTH_EXCEEDED:
        case X509_V_ERR_KEYUSAGE_NO_CERTSIGN:
        case X509_V_ERR_INVALID_EXTENSION:
        case X509_V_ERR_INVALID_POLICY_EXTENSION:
        case X509_V_ERR_NO_EXPLICIT_POLICY:
        case X509_V_ERR_PERMITTED_VIOLATION:
        case X509_V_ERR_EXCLUDED_VIOLATION:
        case X509_V_ERR_SUBTREE_MINMAX:
        case X509_V_ERR_UNSUPPORTED_CONSTRAINT_TYPE:
        case X509_V_ERR_UNSUPPORTED_NAME_SYNTAX:
        case X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION:
        case X509_V_ERR_PATH_LOOP:
            return ChainStatus::InvalidCa;
        default:
            return ChainStatus::Other;
    }
}

std::string generalNameUri(const GENERAL_NAME* gn) {
    if (gn == nullptr || gn->type != GEN_URI) {
        return {};
    }
    const ASN1_IA5STRING* s = gn->d.uniformResourceIdentifier;
    return {reinterpret_cast<const char*>(ASN1_STRING_get0_data(s)),
            static_cast<std::size_t>(ASN1_STRING_length(s))};
}

bool isHttp(const std::string& url) { return url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0; }

// Сертификаты из ответа caIssuers: DER, PKCS#7 certs-only (.p7c) или PEM.
std::vector<Ossl<X509>> certsFromAia(const std::vector<std::uint8_t>& body) {
    std::vector<Ossl<X509>> out;
    const unsigned char* p = body.data();
    if (X509* x = d2i_X509(nullptr, &p, static_cast<long>(body.size()))) {
        out.emplace_back(x);
        return out;
    }
    p = body.data();
    Ossl<PKCS7> p7(d2i_PKCS7(nullptr, &p, static_cast<long>(body.size())));
    if (p7 && PKCS7_type_is_signed(p7.get()) && p7->d.sign != nullptr && p7->d.sign->cert != nullptr) {
        for (int i = 0; i < sk_X509_num(p7->d.sign->cert); ++i) {
            X509* x = sk_X509_value(p7->d.sign->cert, i);
            X509_up_ref(x);
            out.emplace_back(x);
        }
        ERR_clear_error();
        return out;
    }
    out = loadCertificates(body);
    ERR_clear_error();
    return out;
}

}  // namespace

const char* chainStatusName(ChainStatus status) {
    switch (status) {
        case ChainStatus::Valid:
            return "valid";
        case ChainStatus::Untrusted:
            return "untrusted";
        case ChainStatus::Expired:
            return "expired";
        case ChainStatus::NotYetValid:
            return "not yet valid";
        case ChainStatus::Revoked:
            return "revoked";
        case ChainStatus::RevocationUnknown:
            return "revocation unknown";
        case ChainStatus::BadSignature:
            return "bad signature";
        case ChainStatus::InvalidPurpose:
            return "invalid purpose";
        case ChainStatus::InvalidCa:
            return "invalid CA";
        case ChainStatus::Other:
            return "other";
    }
    return "?";
}

bool allowedForDocumentSigning(X509* cert) {
    const std::uint32_t ku = X509_get_key_usage(cert);  // UINT32_MAX — расширения нет
    if (ku != UINT32_MAX && (ku & (KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION)) == 0) {
        return false;
    }
    if ((X509_get_extension_flags(cert) & EXFLAG_XKUSAGE) == 0) {
        return true;
    }
    static const char* const kAllowed[] = {
        "1.3.6.1.5.5.7.3.4",        // emailProtection
        "1.3.6.1.5.5.7.3.36",       // documentSigning (RFC 9336)
        "1.2.840.113583.1.1.5",     // Adobe Authentic Documents Trust
        "1.3.6.1.4.1.311.10.3.12",  // Microsoft Document Signing
        "2.5.29.37.0",              // anyExtendedKeyUsage
    };
    Ossl<EXTENDED_KEY_USAGE> eku(
        static_cast<EXTENDED_KEY_USAGE*>(X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr)));
    if (!eku) {
        ERR_clear_error();
        return false;
    }
    for (int i = 0; i < sk_ASN1_OBJECT_num(eku.get()); ++i) {
        char buf[64];
        OBJ_obj2txt(buf, sizeof buf, sk_ASN1_OBJECT_value(eku.get(), i), 1);
        for (const char* allowed : kAllowed) {
            if (std::strcmp(buf, allowed) == 0) {
                return true;
            }
        }
    }
    return false;
}

std::vector<std::string> ocspUrls(X509* cert) {
    std::vector<std::string> out;
    STACK_OF(OPENSSL_STRING)* list = X509_get1_ocsp(cert);
    for (int i = 0; list != nullptr && i < sk_OPENSSL_STRING_num(list); ++i) {
        out.emplace_back(sk_OPENSSL_STRING_value(list, i));
    }
    X509_email_free(list);
    return out;
}

std::vector<std::string> caIssuersUrls(X509* cert) {
    std::vector<std::string> out;
    Ossl<AUTHORITY_INFO_ACCESS> aia(
        static_cast<AUTHORITY_INFO_ACCESS*>(X509_get_ext_d2i(cert, NID_info_access, nullptr, nullptr)));
    for (int i = 0; aia && i < sk_ACCESS_DESCRIPTION_num(aia.get()); ++i) {
        const ACCESS_DESCRIPTION* ad = sk_ACCESS_DESCRIPTION_value(aia.get(), i);
        if (OBJ_obj2nid(ad->method) == NID_ad_ca_issuers) {
            if (std::string u = generalNameUri(ad->location); !u.empty()) {
                out.push_back(std::move(u));
            }
        }
    }
    ERR_clear_error();
    return out;
}

std::vector<std::string> crlUrls(X509* cert) {
    std::vector<std::string> out;
    Ossl<STACK_OF(DIST_POINT)> dps(static_cast<STACK_OF(DIST_POINT)*>(
        X509_get_ext_d2i(cert, NID_crl_distribution_points, nullptr, nullptr)));
    for (int i = 0; dps && i < sk_DIST_POINT_num(dps.get()); ++i) {
        const DIST_POINT* dp = sk_DIST_POINT_value(dps.get(), i);
        if (dp->distpoint == nullptr || dp->distpoint->type != 0) {
            continue;  // только fullName
        }
        const GENERAL_NAMES* names = dp->distpoint->name.fullname;
        for (int j = 0; j < sk_GENERAL_NAME_num(names); ++j) {
            if (std::string u = generalNameUri(sk_GENERAL_NAME_value(names, j)); !u.empty()) {
                out.push_back(std::move(u));
            }
        }
    }
    ERR_clear_error();
    return out;
}

ChainBuilder::ChainBuilder(X509_STORE* trustStore, std::optional<std::time_t> time, bool partialChain)
    : store_(ossl::checkPtr(X509_STORE_new(), "X509_STORE_new")),
      untrusted_(ossl::checkPtr(sk_X509_new_null(), "sk_X509_new_null")),
      crls_(ossl::checkPtr(sk_X509_CRL_new_null(), "sk_X509_CRL_new_null")),
      time_(time),
      partialChain_(partialChain) {
    if (trustStore != nullptr) {
        Ossl<STACK_OF(X509)> anchors(X509_STORE_get1_all_certs(trustStore));
        for (int i = 0; anchors && i < sk_X509_num(anchors.get()); ++i) {
            X509_STORE_add_cert(store_.get(), sk_X509_value(anchors.get(), i));
        }
    }
    ERR_clear_error();
}

ChainBuilder::~ChainBuilder() = default;

void ChainBuilder::addUntrusted(X509* cert) {
    if (cert != nullptr && X509_up_ref(cert) == 1 && sk_X509_push(untrusted_.get(), cert) <= 0) {
        X509_free(cert);
    }
}

void ChainBuilder::addCrl(X509_CRL* crl) {
    if (crl != nullptr && X509_CRL_up_ref(crl) == 1 && sk_X509_CRL_push(crls_.get(), crl) <= 0) {
        X509_CRL_free(crl);
    }
}

bool ChainBuilder::fetchIssuer(X509* cert, ChainResult& result) {
    bool added = false;
    for (const std::string& url : caIssuersUrls(cert)) {
        if (!isHttp(url) || !triedUrls_.insert(url).second) {
            continue;
        }
        net::Request req;
        req.method = "GET";
        req.url = url;
        req.headers.emplace_back("Accept", "application/pkix-cert, application/pkcs7-mime");
        req.sameHostRedirectsOnly = false;  // AIA часто на CDN
        req.maxResponseBytes = 1u << 20;
        req.purpose = net::Purpose::Signatures;
        const net::Response resp = fetcher_->fetch(req);
        if (!resp.ok()) {
            spdlog::info("chain: caIssuers {}: {} {}", url, net::statusName(resp.status), resp.error);
            continue;
        }
        for (Ossl<X509>& x : certsFromAia(resp.body)) {
            result.fetched.push_back(ossl::toDer(x.get(), i2d_X509, "i2d_X509"));
            addUntrusted(x.get());
            added = true;
        }
    }
    return added;
}

ChainResult ChainBuilder::build(X509* leaf) {
    if (leaf == nullptr) {
        throw std::invalid_argument("ChainBuilder::build: no certificate");
    }
    ChainResult r;
    for (int attempt = 0;; ++attempt) {
        ERR_clear_error();
        Ossl<X509_STORE_CTX> ctx(ossl::checkPtr(X509_STORE_CTX_new(), "X509_STORE_CTX_new"));
        ossl::check(X509_STORE_CTX_init(ctx.get(), store_.get(), leaf, untrusted_.get()),
                    "X509_STORE_CTX_init");
        X509_VERIFY_PARAM* param = X509_STORE_CTX_get0_param(ctx.get());
        if (time_) {
            X509_VERIFY_PARAM_set_time(param, *time_);
        }
        unsigned long flags = 0;
        if (partialChain_) {
            flags |= X509_V_FLAG_PARTIAL_CHAIN;
        }
        if (crlCheck_ != CrlCheck::None) {
            flags |= X509_V_FLAG_CRL_CHECK;
            if (crlCheck_ == CrlCheck::All) {
                flags |= X509_V_FLAG_CRL_CHECK_ALL;
            }
            X509_STORE_CTX_set0_crls(ctx.get(), crls_.get());
        }
        X509_VERIFY_PARAM_set_flags(param, flags);
        if (purpose_ == ChainPurpose::Timestamping) {
            X509_STORE_CTX_set_purpose(ctx.get(), X509_PURPOSE_TIMESTAMP_SIGN);
        }
        const int ok = X509_verify_cert(ctx.get());
        const int err = ok == 1 ? X509_V_OK : X509_STORE_CTX_get_error(ctx.get());
        if (ok != 1 && fetcher_ != nullptr && attempt < 4 &&
            (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY ||
             err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT ||
             err == X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE)) {
            X509* current = X509_STORE_CTX_get_current_cert(ctx.get());
            if (fetchIssuer(current != nullptr ? current : leaf, r)) {
                continue;
            }
        }
        r.opensslError = err;
        r.errorDepth = ok == 1 ? -1 : X509_STORE_CTX_get_error_depth(ctx.get());
        r.error = ok == 1 ? std::string() : X509_verify_cert_error_string(err);
        r.status = statusFromVerifyError(err);
        r.certs.reset(X509_STORE_CTX_get1_chain(ctx.get()));
        if (!r.certs) {
            r.certs.reset(sk_X509_new_null());
            X509_up_ref(leaf);
            sk_X509_push(r.certs.get(), leaf);
        }
        r.chain.clear();
        for (int i = 0; i < sk_X509_num(r.certs.get()); ++i) {
            r.chain.push_back(summarize(sk_X509_value(r.certs.get(), i)));
        }
        if (r.status == ChainStatus::Valid && purpose_ == ChainPurpose::DocumentSigning &&
            !allowedForDocumentSigning(leaf)) {
            r.status = ChainStatus::InvalidPurpose;
            r.opensslError = X509_V_ERR_INVALID_PURPOSE;
            r.error = "certificate is not allowed for document signing (keyUsage / extendedKeyUsage)";
            r.errorDepth = 0;
        }
        ERR_clear_error();
        return r;
    }
}

}  // namespace ot::sign
