#include "core/sign/tsa.h"

#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ts.h>
#include <openssl/x509v3.h>

#include <spdlog/spdlog.h>

#include <cstring>
#include <ctime>
#include <optional>

namespace ot::sign {

namespace {

constexpr std::size_t kMaxReply = 1u << 20;

std::string objText(const ASN1_OBJECT* o) {
    if (o == nullptr) {
        return {};
    }
    char buf[128];
    const int n = OBJ_obj2txt(buf, sizeof buf, o, 1);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(std::min<int>(n, sizeof buf - 1)))
                 : std::string();
}

Bytes stringBytes(const ASN1_STRING* s) {
    if (s == nullptr) {
        return {};
    }
    const unsigned char* d = ASN1_STRING_get0_data(s);
    return {d, d + ASN1_STRING_length(s)};
}

Bytes integerBytes(const ASN1_INTEGER* i) {
    if (i == nullptr) {
        return {};
    }
    Ossl<BIGNUM> bn(ASN1_INTEGER_to_BN(i, nullptr));
    if (!bn) {
        ERR_clear_error();
        return {};
    }
    Bytes out(static_cast<std::size_t>(BN_num_bytes(bn.get())));
    BN_bn2bin(bn.get(), out.data());
    return out;
}

std::string base64(std::string_view text) {
    std::string out(4 * ((text.size() + 2) / 3) + 1, '\0');
    const int n =
        EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                        reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size()));
    out.resize(static_cast<std::size_t>(std::max(n, 0)));
    return out;
}

std::optional<std::time_t> toTimeT(const ASN1_TIME* t) {
    std::tm tm{};
    if (t == nullptr || ASN1_TIME_to_tm(t, &tm) != 1) {
        ERR_clear_error();
        return std::nullopt;
    }
    return _mkgmtime(&tm);
}

void fillFromTstInfo(TS_TST_INFO* tst, TimestampInfo& info) {
    info.genTime = timeToIso(TS_TST_INFO_get_time(tst));
    info.serial = serialHex(TS_TST_INFO_get_serial(tst));
    info.policy = objText(TS_TST_INFO_get_policy_id(tst));
    TS_MSG_IMPRINT* mi = TS_TST_INFO_get_msg_imprint(tst);
    const ASN1_OBJECT* alg = nullptr;
    X509_ALGOR_get0(&alg, nullptr, nullptr, TS_MSG_IMPRINT_get_algo(mi));
    info.hashAlgorithm = objText(alg);
    info.imprint = stringBytes(TS_MSG_IMPRINT_get_msg(mi));
    info.nonce = integerBytes(TS_TST_INFO_get_nonce(tst));
    if (TS_ACCURACY* acc = TS_TST_INFO_get_accuracy(tst)) {
        const auto part = [](const ASN1_INTEGER* i) {
            return i != nullptr ? static_cast<double>(ASN1_INTEGER_get(i)) : 0.0;
        };
        info.accuracySeconds = part(TS_ACCURACY_get_seconds(acc)) + part(TS_ACCURACY_get_millis(acc)) / 1e3 +
                               part(TS_ACCURACY_get_micros(acc)) / 1e6;
    }
    if (const GENERAL_NAME* gn = TS_TST_INFO_get_tsa(tst); gn != nullptr && gn->type == GEN_DIRNAME) {
        info.tsaName = nameToString(gn->d.directoryName);
    }
}

void fillSigner(std::span<const std::uint8_t> token, TimestampInfo& info) {
    try {
        const CmsInfo cms = parseCms(token);
        if (cms.signerCertIndex >= 0) {
            info.signer = cms.certificates[static_cast<std::size_t>(cms.signerCertIndex)];
        }
    } catch (const std::exception&) {
        // Сведения о подписанте необязательны: токен уже проверен OpenSSL.
    }
}

// Хранилище для проверки: копия якорей (с временем проверки) или, без них, сертификаты самого токена с
// PARTIAL_CHAIN — подпись проверяется по сертификату из токена, доверия нет.
Ossl<X509_STORE> verificationStore(X509_STORE* anchors, PKCS7* token, std::optional<std::time_t> at) {
    Ossl<X509_STORE> st(ossl::checkPtr(X509_STORE_new(), "X509_STORE_new"));
    if (anchors != nullptr) {
        Ossl<STACK_OF(X509)> all(X509_STORE_get1_all_certs(anchors));
        for (int i = 0; all && i < sk_X509_num(all.get()); ++i) {
            X509_STORE_add_cert(st.get(), sk_X509_value(all.get(), i));
        }
    } else {
        STACK_OF(X509)* certs = PKCS7_type_is_signed(token) ? token->d.sign->cert : nullptr;
        for (int i = 0; certs != nullptr && i < sk_X509_num(certs); ++i) {
            X509_STORE_add_cert(st.get(), sk_X509_value(certs, i));
        }
        X509_STORE_set_flags(st.get(), X509_V_FLAG_PARTIAL_CHAIN);
    }
    if (at) {
        X509_VERIFY_PARAM_set_time(X509_STORE_get0_param(st.get()), *at);
    }
    ERR_clear_error();
    return st;
}

std::string statusText(TS_STATUS_INFO* si) {
    std::string out = "PKIStatus " + std::to_string(ASN1_INTEGER_get(TS_STATUS_INFO_get0_status(si)));
    if (const STACK_OF(ASN1_UTF8STRING)* text = TS_STATUS_INFO_get0_text(si)) {
        for (int i = 0; i < sk_ASN1_UTF8STRING_num(text); ++i) {
            const Bytes b = stringBytes(sk_ASN1_UTF8STRING_value(text, i));
            out += i == 0 ? ": " : "; ";
            out.append(b.begin(), b.end());
        }
    }
    if (const ASN1_BIT_STRING* fail = TS_STATUS_INFO_get0_failure_info(si)) {
        static constexpr std::pair<int, const char*> kBits[] = {{0, "badAlg"},
                                                                {2, "badRequest"},
                                                                {5, "badDataFormat"},
                                                                {14, "timeNotAvailable"},
                                                                {15, "unacceptedPolicy"},
                                                                {16, "unacceptedExtension"},
                                                                {17, "addInfoNotAvailable"},
                                                                {25, "systemFailure"}};
        for (const auto& [bit, name] : kBits) {
            if (ASN1_BIT_STRING_get_bit(fail, bit) == 1) {
                out += std::string(" [") + name + "]";
            }
        }
    }
    return out;
}

}  // namespace

TsaRequest makeTsaRequest(std::span<const std::uint8_t> imprint, DigestAlgorithm digest,
                          std::string_view policy, bool certReq) {
    if (imprint.size() != digestSize(digest)) {
        throw std::invalid_argument("TSA request: imprint length " + std::to_string(imprint.size()) +
                                    ", expected " + std::to_string(digestSize(digest)));
    }
    ERR_clear_error();
    TsaRequest out;
    out.imprint.assign(imprint.begin(), imprint.end());
    out.digest = digest;
    out.policy = std::string(policy);
    out.certReq = certReq;

    Ossl<TS_REQ> req(ossl::checkPtr(TS_REQ_new(), "TS_REQ_new"));
    ossl::check(TS_REQ_set_version(req.get(), 1), "TS_REQ_set_version");
    Ossl<X509_ALGOR> algo(ossl::checkPtr(X509_ALGOR_new(), "X509_ALGOR_new"));
    X509_ALGOR_set_md(algo.get(), evpDigest(digest));
    Ossl<TS_MSG_IMPRINT> mi(ossl::checkPtr(TS_MSG_IMPRINT_new(), "TS_MSG_IMPRINT_new"));
    ossl::check(TS_MSG_IMPRINT_set_algo(mi.get(), algo.get()), "TS_MSG_IMPRINT_set_algo");
    ossl::check(TS_MSG_IMPRINT_set_msg(mi.get(), out.imprint.data(), static_cast<int>(out.imprint.size())),
                "TS_MSG_IMPRINT_set_msg");
    ossl::check(TS_REQ_set_msg_imprint(req.get(), mi.get()), "TS_REQ_set_msg_imprint");
    if (!policy.empty()) {
        const std::string text(policy);
        Ossl<ASN1_OBJECT> obj(ossl::checkPtr(OBJ_txt2obj(text.c_str(), 1), "OBJ_txt2obj"));
        ossl::check(TS_REQ_set_policy_id(req.get(), obj.get()), "TS_REQ_set_policy_id");
    }
    // nonce — 64 бита, положительный, старший байт не ноль (длина постоянна).
    unsigned char nonce[8];
    ossl::check(RAND_bytes(nonce, sizeof nonce), "RAND_bytes");
    nonce[0] = static_cast<unsigned char>((nonce[0] & 0x7f) | 0x40);
    Ossl<BIGNUM> bn(ossl::checkPtr(BN_bin2bn(nonce, sizeof nonce, nullptr), "BN_bin2bn"));
    Ossl<ASN1_INTEGER> n(ossl::checkPtr(BN_to_ASN1_INTEGER(bn.get(), nullptr), "BN_to_ASN1_INTEGER"));
    ossl::check(TS_REQ_set_nonce(req.get(), n.get()), "TS_REQ_set_nonce");
    out.nonce.assign(nonce, nonce + sizeof nonce);
    ossl::check(TS_REQ_set_cert_req(req.get(), certReq ? 1 : 0), "TS_REQ_set_cert_req");
    out.der = ossl::toDer(req.get(), i2d_TS_REQ, "i2d_TS_REQ");
    return out;
}

TimestampInfo parseTimestampToken(std::span<const std::uint8_t> token) {
    const unsigned char* p = token.data();
    Ossl<PKCS7> p7(d2i_PKCS7(nullptr, &p, static_cast<long>(token.size())));
    if (!p7) {
        throw std::runtime_error(OsslError::fromQueue("time-stamp token").what());
    }
    Ossl<TS_TST_INFO> tst(PKCS7_to_TS_TST_INFO(p7.get()));
    if (!tst) {
        throw std::runtime_error(OsslError::fromQueue("time-stamp token: TSTInfo").what());
    }
    TimestampInfo info;
    fillFromTstInfo(tst.get(), info);
    fillSigner(token, info);
    return info;
}

Bytes checkTsaResponse(std::span<const std::uint8_t> response, const TsaRequest& request, X509_STORE* store,
                       TimestampInfo* info) {
    ERR_clear_error();
    const unsigned char* p = response.data();
    Ossl<TS_RESP> resp(d2i_TS_RESP(nullptr, &p, static_cast<long>(response.size())));
    if (!resp) {
        throw TsaError(TsaStatus::BadResponse, OsslError::fromQueue("TimeStampResp").what());
    }
    TS_STATUS_INFO* si = TS_RESP_get_status_info(resp.get());
    const long status = si != nullptr ? ASN1_INTEGER_get(TS_STATUS_INFO_get0_status(si)) : -1;
    if (status != 0 && status != 1) {  // granted, grantedWithMods
        throw TsaError(TsaStatus::Rejected, "time-stamp request rejected: " +
                                                (si != nullptr ? statusText(si) : std::string("no status")));
    }
    PKCS7* token = TS_RESP_get_token(resp.get());
    TS_TST_INFO* tst = TS_RESP_get_tst_info(resp.get());
    if (token == nullptr || tst == nullptr) {
        throw TsaError(TsaStatus::BadResponse, "time-stamp response without token");
    }
    const unsigned char* q = request.der.data();
    Ossl<TS_REQ> req(d2i_TS_REQ(nullptr, &q, static_cast<long>(request.der.size())));
    if (!req) {
        throw std::invalid_argument(OsslError::fromQueue("TimeStampReq").what());
    }
    Ossl<TS_VERIFY_CTX> ctx(
        ossl::checkPtr(TS_REQ_to_TS_VERIFY_CTX(req.get(), nullptr), "TS_REQ_to_TS_VERIFY_CTX"));
    TS_VERIFY_CTX_add_flags(
        ctx.get(), TS_VFY_SIGNATURE | TS_VFY_VERSION | TS_VFY_IMPRINT | TS_VFY_NONCE | TS_VFY_SIGNER);
    TS_VERIFY_CTX_set0_store(ctx.get(), verificationStore(store, token, std::nullopt).release());
    if (TS_RESP_verify_response(ctx.get(), resp.get()) != 1) {
        throw TsaError(TsaStatus::VerifyFailed, OsslError::fromQueue("TS_RESP_verify_response").what());
    }
    Bytes der = ossl::toDer(token, i2d_PKCS7, "i2d_PKCS7");
    if (info != nullptr) {
        *info = {};
        fillFromTstInfo(tst, *info);
        fillSigner(der, *info);
        info->trusted = store != nullptr;
    }
    return der;
}

bool verifyTimestampToken(std::span<const std::uint8_t> token, std::span<const std::uint8_t> imprint,
                          X509_STORE* store, std::string* error, TimestampInfo* info) {
    ERR_clear_error();
    const auto fail = [&](const std::string& why) {
        if (error != nullptr) {
            *error = why;
        }
        ERR_clear_error();
        return false;
    };
    const unsigned char* p = token.data();
    Ossl<PKCS7> p7(d2i_PKCS7(nullptr, &p, static_cast<long>(token.size())));
    if (!p7) {
        return fail(OsslError::fromQueue("time-stamp token").what());
    }
    Ossl<TS_TST_INFO> tst(PKCS7_to_TS_TST_INFO(p7.get()));
    if (!tst) {
        return fail(OsslError::fromQueue("time-stamp token: TSTInfo").what());
    }
    if (imprint.empty()) {
        return fail("time-stamp token: empty imprint");
    }
    Ossl<TS_VERIFY_CTX> ctx(TS_VERIFY_CTX_new());
    if (!ctx) {
        return fail(OsslError::fromQueue("TS_VERIFY_CTX_new").what());
    }
    TS_VERIFY_CTX_set_flags(ctx.get(), TS_VFY_SIGNATURE | TS_VFY_VERSION | TS_VFY_IMPRINT | TS_VFY_SIGNER);
    auto* buf = static_cast<unsigned char*>(OPENSSL_malloc(imprint.size()));
    if (buf == nullptr) {
        return fail("out of memory");
    }
    std::memcpy(buf, imprint.data(), imprint.size());
    TS_VERIFY_CTX_set0_imprint(ctx.get(), buf, static_cast<long>(imprint.size()));
    // Цепочка TSA — на момент метки (сертификат службы мог истечь позже).
    TS_VERIFY_CTX_set0_store(
        ctx.get(), verificationStore(store, p7.get(), toTimeT(TS_TST_INFO_get_time(tst.get()))).release());
    if (TS_RESP_verify_token(ctx.get(), p7.get()) != 1) {
        return fail(OsslError::fromQueue("TS_RESP_verify_token").what());
    }
    if (info != nullptr) {
        *info = {};
        fillFromTstInfo(tst.get(), *info);
        fillSigner(token, *info);
        info->trusted = store != nullptr;
    }
    return true;
}

TsaClient::TsaClient(net::HttpFetcher& fetcher, TsaConfig config)
    : fetcher_(fetcher), config_(std::move(config)) {}

TsaClient::~TsaClient() = default;

void TsaClient::setTrustStore(X509_STORE* store) {
    if (store != nullptr) {
        X509_STORE_up_ref(store);
    }
    store_.reset(store);
}

Bytes TsaClient::timestamp(std::span<const std::uint8_t> imprint) {
    const TsaRequest request = makeTsaRequest(imprint, config_.digest, config_.policy, true);
    net::Request http;
    http.method = "POST";
    http.url = config_.url;
    http.contentType = "application/timestamp-query";
    http.body = request.der;
    http.headers.emplace_back("Accept", "application/timestamp-reply");
    if (!config_.user.empty() || !config_.password.empty()) {
        http.headers.emplace_back("Authorization", "Basic " + base64(config_.user + ":" + config_.password));
    }
    http.timeoutMs = config_.timeoutMs;
    http.sameHostRedirectsOnly = true;
    http.maxResponseBytes = kMaxReply;
    http.purpose = net::Purpose::Signatures;

    const net::Response response = fetcher_.fetch(http);
    const std::string where = " (" + config_.url + ")";
    switch (response.status) {
        case net::Status::Ok:
            break;
        case net::Status::Disabled:
            throw TsaError(TsaStatus::NetworkDisabled, "network access is disabled" + where);
        case net::Status::Timeout:
            throw TsaError(TsaStatus::Timeout, response.error + where);
        case net::Status::HttpError:
            throw TsaError(TsaStatus::HttpError, "HTTP " + std::to_string(response.httpCode) + where,
                           response.httpCode);
        default:
            throw TsaError(TsaStatus::NetworkError,
                           std::string(net::statusName(response.status)) + ": " + response.error + where);
    }
    if (!response.ok()) {
        throw TsaError(TsaStatus::HttpError, "HTTP " + std::to_string(response.httpCode) + where,
                       response.httpCode);
    }
    // RFC 3161 §3.4: application/timestamp-reply; «timestamp-response» — опечатка в прозе RFC, встречается.
    if (response.contentType != "application/timestamp-reply" &&
        response.contentType != "application/timestamp-response") {
        throw TsaError(TsaStatus::BadResponse,
                       "unexpected content type '" + response.contentType + "'" + where);
    }
    TimestampInfo info;
    Bytes token = checkTsaResponse(response.body, request, store_.get(), &info);
    last_ = std::move(info);
    spdlog::info("tsa: {} granted: {}, serial {}, policy {}, {} bytes{}", config_.url, last_.genTime,
                 last_.serial, last_.policy, token.size(),
                 last_.trusted ? "" : " (TSA not checked against trust anchors)");
    return token;
}

}  // namespace ot::sign
