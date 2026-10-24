#include "core/sign/ltv.h"

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdio>
#include <map>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/chain.h"
#include "core/sign/cms.h"
#include "core/sign/docmdp.h"
#include "core/sign/revocation.h"
#include "core/sign/sign_field.h"
#include "core/sign/tsa.h"
#include "core/sign/x509_info.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace fs = std::filesystem;

namespace {

bool hasDer(const std::vector<Bytes>& set, const Bytes& der) {
    return std::find(set.begin(), set.end(), der) != set.end();
}

bool addUnique(std::vector<Bytes>& set, const Bytes& der) {
    if (der.empty() || hasDer(set, der)) {
        return false;
    }
    set.push_back(der);
    return true;
}

// Длина DER по заголовку первого элемента (дополнение нулями /Contents отрезается); не DER — весь буфер.
std::size_t derLength(const Bytes& b) {
    if (b.size() < 2) {
        return b.size();
    }
    std::size_t len = b[1];
    std::size_t header = 2;
    if ((len & 0x80) != 0) {
        const std::size_t n = len & 0x7F;
        if (n == 0 || n > 4 || b.size() < 2 + n) {
            return b.size();
        }
        len = 0;
        for (std::size_t i = 0; i < n; ++i) {
            len = (len << 8) | b[2 + i];
        }
        header += n;
    }
    return std::min(b.size(), header + len);
}

std::string sha1Hex(const Bytes& data) {
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1(data.data(), data.size(), md);
    return ossl::toHex(std::span<const std::uint8_t>(md, SHA_DIGEST_LENGTH));
}

Bytes certDer(X509* x) { return ossl::toDer(x, i2d_X509, "i2d_X509"); }

bool selfSigned(X509* x) { return X509_self_signed(x, 0) == 1; }

// «D:YYYYMMDDHHmmSS+HH'mm'» → секунды UTC; не дата — nullopt.
std::optional<std::time_t> pdfDate(const std::string& d) {
    const std::string s = d.rfind("D:", 0) == 0 ? d.substr(2) : d;
    int f[6] = {0, 1, 1, 0, 0, 0};
    const int widths[6] = {4, 2, 2, 2, 2, 2};
    std::size_t at = 0;
    for (int i = 0; i < 6 && at + static_cast<std::size_t>(widths[i]) <= s.size(); ++i) {
        int v = 0;
        bool digits = true;
        for (int k = 0; k < widths[i]; ++k) {
            const char c = s[at + static_cast<std::size_t>(k)];
            digits = digits && c >= '0' && c <= '9';
            v = v * 10 + (c - '0');
        }
        if (!digits) {
            break;
        }
        f[i] = v;
        at += static_cast<std::size_t>(widths[i]);
    }
    if (at < 4) {
        return std::nullopt;
    }
    std::tm tm{};
    tm.tm_year = f[0] - 1900;
    tm.tm_mon = f[1] - 1;
    tm.tm_mday = f[2];
    tm.tm_hour = f[3];
    tm.tm_min = f[4];
    tm.tm_sec = f[5];
    std::time_t t = _mkgmtime(&tm);
    if (t == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    if (at == 14 && s.size() >= 17 && (s[14] == '+' || s[14] == '-')) {
        const int hh = (s[15] - '0') * 10 + (s[16] - '0');
        int mm = 0;
        const std::size_t m = s.size() >= 20 && s[17] == '\'' ? 18 : 17;
        if (s.size() >= m + 2) {
            mm = (s[m] - '0') * 10 + (s[m + 1] - '0');
        }
        const int offset = (hh * 60 + mm) * 60;
        t += s[14] == '+' ? -offset : offset;
    }
    return t;
}

// «2026-10-24T09:27:43Z» → секунды UTC.
std::optional<std::time_t> isoTime(const std::string& iso) {
    std::tm tm{};
    if (std::sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
                    &tm.tm_min, &tm.tm_sec) != 6) {
        return std::nullopt;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    const std::time_t t = _mkgmtime(&tm);
    return t == static_cast<std::time_t>(-1) ? std::nullopt : std::optional<std::time_t>(t);
}

// Потоки массива /DSS (DER), без повторов.
std::vector<Bytes> dssArray(fz_context* ctx, pdf_obj* arr) {
    std::vector<Bytes> out;
    int n = 0;
    mu::call(ctx, [&] { n = pdf_array_len(ctx, arr); });
    for (int i = 0; i < n; ++i) {
        mu::BufferHandle buf(ctx);
        try {
            mu::call(ctx, [&] {
                pdf_obj* s = pdf_array_get(ctx, arr, i);
                if (pdf_is_stream(ctx, s)) {
                    buf.reset(pdf_load_stream(ctx, s));
                }
            });
        } catch (const std::exception& e) {
            spdlog::debug("ltv: /DSS stream {}: {}", i, e.what());
            continue;
        }
        if (!buf) {
            continue;
        }
        unsigned char* data = nullptr;
        const std::size_t len = fz_buffer_storage(ctx, buf, &data);
        addUnique(out, Bytes(data, data + len));
    }
    return out;
}

LtvMaterial readDss(fz_context* ctx, pdf_document* pdf) {
    LtvMaterial m;
    pdf_obj* certs = nullptr;
    pdf_obj* ocsps = nullptr;
    pdf_obj* crls = nullptr;
    mu::call(ctx, [&] {
        pdf_obj* dss = pdf_dict_gets(ctx, pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root)), "DSS");
        certs = pdf_dict_gets(ctx, dss, "Certs");
        ocsps = pdf_dict_gets(ctx, dss, "OCSPs");
        crls = pdf_dict_gets(ctx, dss, "CRLs");
    });
    m.certs = dssArray(ctx, certs);
    m.ocsps = dssArray(ctx, ocsps);
    m.crls = dssArray(ctx, crls);
    return m;
}

// Подписанные поля /AcroForm (/FT /Sig с /V-словарём), в глубину по /Kids.
void walkFields(fz_context* ctx, pdf_obj* field, std::vector<pdf_obj*>& out, int depth) {
    if (depth > 32) {
        return;
    }
    int sig = 0;
    int kids = 0;
    pdf_obj* arr = nullptr;
    mu::call(ctx, [&] {
        sig = pdf_name_eq(ctx, pdf_dict_get_inheritable(ctx, field, PDF_NAME(FT)), PDF_NAME(Sig)) &&
                      pdf_is_dict(ctx, pdf_dict_get(ctx, field, PDF_NAME(V)))
                  ? 1
                  : 0;
        arr = pdf_dict_get(ctx, field, PDF_NAME(Kids));
        kids = pdf_array_len(ctx, arr);
    });
    if (sig != 0) {
        out.push_back(field);
    }
    for (int i = 0; i < kids; ++i) {
        pdf_obj* kid = nullptr;
        mu::call(ctx, [&] { kid = pdf_array_get(ctx, arr, i); });
        walkFields(ctx, kid, out, depth + 1);
    }
}

struct FieldData {
    std::string name;
    std::string subFilter;
    std::string m;
    int vnum = 0;
    Bytes der;
};

std::vector<FieldData> signedFieldData(fz_context* ctx, pdf_document* pdf) {
    pdf_obj* fields = nullptr;
    int n = 0;
    mu::call(ctx, [&] {
        fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
        n = pdf_array_len(ctx, fields);
    });
    std::vector<pdf_obj*> found;
    for (int i = 0; i < n; ++i) {
        pdf_obj* f = nullptr;
        mu::call(ctx, [&] { f = pdf_array_get(ctx, fields, i); });
        walkFields(ctx, f, found, 0);
    }
    std::vector<FieldData> out;
    for (pdf_obj* f : found) {
        FieldData d;
        char* name = nullptr;
        const char* sub = nullptr;
        const char* m = nullptr;
        const char* buf = nullptr;
        std::size_t len = 0;
        mu::call(ctx, [&] {
            name = pdf_load_field_name(ctx, f);
            pdf_obj* v = pdf_dict_get(ctx, f, PDF_NAME(V));
            d.vnum = pdf_to_num(ctx, v);
            sub = pdf_to_name(ctx, pdf_dict_get(ctx, v, PDF_NAME(SubFilter)));
            m = pdf_to_text_string(ctx, pdf_dict_get(ctx, v, PDF_NAME(M)));
            pdf_obj* c = pdf_dict_get(ctx, v, PDF_NAME(Contents));
            buf = pdf_to_str_buf(ctx, c);
            len = pdf_to_str_len(ctx, c);
        });
        d.name = name != nullptr ? name : "";
        fz_free(ctx, name);
        d.subFilter = sub != nullptr ? sub : "";
        d.m = m != nullptr ? m : "";
        if (buf != nullptr && len > 0) {
            d.der.assign(reinterpret_cast<const std::uint8_t*>(buf),
                         reinterpret_cast<const std::uint8_t*>(buf) + len);
            d.der.resize(derLength(d.der));
        }
        out.push_back(std::move(d));
    }
    return out;
}

// Сборщик: общий пул сертификатов документа, якоря, собранное.
class Collector {
public:
    Collector(const LtvOptions& options, net::HttpFetcher* fetcher) : options_(options), fetcher_(fetcher) {
        pool_.reset(ossl::checkPtr(sk_X509_new_null(), "sk_X509_new_null"));
        if (options_.trust != nullptr) {
            anchors_ = options_.trust->x509Store(kTrustSignatures | kTrustCertification);
        }
    }

    void addToPool(X509* x) {
        const Bytes der = certDer(x);
        if (addUnique(poolDer_, der)) {
            X509_up_ref(x);
            sk_X509_push(pool_.get(), x);
        }
    }
    void addToPool(const std::vector<CertSummary>& certs) {
        for (const CertSummary& c : certs) {
            try {
                Ossl<X509> x = loadCertificate(c.der);
                addToPool(x.get());
            } catch (const std::exception&) {
                ERR_clear_error();
            }
        }
    }
    void addKnown(const RevocationData& data) {
        for (const Bytes& b : data.ocspResponses) {
            addUnique(known_.ocspResponses, b);
        }
        for (const Bytes& b : data.crls) {
            addUnique(known_.crls, b);
        }
    }

    // Сертификат по issuer/serial из пула (подписант не в CMS — /DSS).
    X509* findInPool(const std::string& issuer, const std::string& serial) const {
        for (int i = 0; i < sk_X509_num(pool_.get()); ++i) {
            X509* x = sk_X509_value(pool_.get(), i);
            const CertSummary s = summarize(x);
            if (s.issuer == issuer && s.serial == serial) {
                return x;
            }
        }
        return nullptr;
    }
    X509* issuerOf(X509* cert) const {
        for (int i = 0; i < sk_X509_num(pool_.get()); ++i) {
            X509* x = sk_X509_value(pool_.get(), i);
            if (x != cert && X509_check_issued(x, cert) == X509_V_OK) {
                return x;
            }
        }
        return nullptr;
    }

    // Цепочка leaf до якоря или корня и отзыв её сертификатов — в sig.
    void chain(X509* leaf, std::time_t at, LtvSignature& sig) {
        ChainBuilder builder(anchors_.get(), at, true);
        for (int i = 0; i < sk_X509_num(pool_.get()); ++i) {
            builder.addUntrusted(sk_X509_value(pool_.get(), i));
        }
        builder.setFetcher(fetcher_);
        const ChainResult r = builder.build(leaf);
        for (const Bytes& der : r.fetched) {
            try {
                Ossl<X509> x = loadCertificate(der);
                addToPool(x.get());
            } catch (const std::exception&) {
                ERR_clear_error();
            }
        }
        const int n = sk_X509_num(r.certs.get());
        for (int i = 0; i < n; ++i) {
            addUnique(sig.material.certs, certDer(sk_X509_value(r.certs.get(), i)));
        }
        X509* last = n > 0 ? sk_X509_value(r.certs.get(), n - 1) : leaf;
        const CertSummary lastInfo = summarize(last);
        const bool anchored =
            options_.trust != nullptr && options_.trust->find(ossl::toHex(lastInfo.sha256)) != nullptr;
        const bool rooted = selfSigned(last) || anchored;
        if (!rooted) {
            sig.chainComplete = false;
            sig.missing.push_back("issuer of \"" + lastInfo.subject + "\" not found");
        }
        // Отзыв: все, кроме корня (якоря); у неполной цепочки — пока есть издатель.
        RevocationChecker checker(known_, fetcher_);
        checker.setTime(at);
        checker.setRetroactive(true);
        for (int i = 0; i + 1 < n; ++i) {
            X509* cert = sk_X509_value(r.certs.get(), i);
            X509* issuer = sk_X509_value(r.certs.get(), i + 1);
            revocation(checker, cert, issuer, sig);
        }
    }

    // Отзыв респондеров OCSP — после цепочек (их CRL уже собраны).
    void responders(std::time_t at, LtvSignature& sig) {
        std::vector<Ossl<X509>> todo = std::move(responders_);
        responders_.clear();
        for (Ossl<X509>& r : todo) {
            X509* issuer = issuerOf(r.get());
            if (issuer == nullptr) {
                if (!selfSigned(r.get())) {
                    sig.revocationComplete = false;
                    sig.missing.push_back("issuer of OCSP responder \"" + summarize(r.get()).subject +
                                          "\" not found");
                }
                continue;
            }
            addUnique(sig.material.certs, certDer(issuer));
            RevocationChecker checker(known_, fetcher_);
            checker.setTime(at);
            checker.setRetroactive(true);
            revocation(checker, r.get(), issuer, sig, false);
        }
    }

    const RevocationData& known() const { return known_; }

private:
    void revocation(RevocationChecker& checker, X509* cert, X509* issuer, LtvSignature& sig,
                    bool followResponder = true) {
        const RevocationResult res = checker.check(cert, issuer);
        if (res.status == RevocationStatus::Unknown) {
            sig.revocationComplete = false;
            sig.missing.push_back("revocation of \"" + summarize(cert).subject + "\": " + res.detail);
            return;
        }
        if (!res.ocspResponse.empty()) {
            const Bytes resp = ocspResponseDer(res.ocspResponse);
            if (!resp.empty()) {
                addUnique(sig.material.ocsps, resp);
                addUnique(known_.ocspResponses, resp);
                if (followResponder) {
                    responderCerts(resp, issuer, sig);
                }
            }
        }
        if (!res.crl.empty()) {
            addUnique(sig.material.crls, res.crl);
            addUnique(known_.crls, res.crl);
        }
    }

    // Сертификаты из ответа OCSP: в /Certs; делегированный респондер — его отзыв (если нет ocsp-nocheck).
    void responderCerts(const Bytes& resp, X509* issuer, LtvSignature& sig) {
        const unsigned char* p = resp.data();
        Ossl<OCSP_RESPONSE> r(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(resp.size())));
        if (!r) {
            ERR_clear_error();
            return;
        }
        Ossl<OCSP_BASICRESP> bs(OCSP_response_get1_basic(r.get()));
        if (!bs) {
            ERR_clear_error();
            return;
        }
        const STACK_OF(X509)* certs = OCSP_resp_get0_certs(bs.get());
        for (int i = 0; certs != nullptr && i < sk_X509_num(certs); ++i) {
            X509* x = sk_X509_value(certs, i);
            addToPool(x);
            addUnique(sig.material.certs, certDer(x));
            const bool nocheck = X509_get_ext_by_NID(x, NID_id_pkix_OCSP_noCheck, -1) >= 0;
            if (X509_cmp(x, issuer) != 0 && !nocheck && !selfSigned(x)) {
                X509_up_ref(x);
                responders_.emplace_back(x);
            }
        }
    }

    const LtvOptions& options_;
    net::HttpFetcher* fetcher_;
    Ossl<X509_STORE> anchors_;
    Ossl<STACK_OF(X509)> pool_;
    std::vector<Bytes> poolDer_;
    RevocationData known_;
    std::vector<Ossl<X509>> responders_;
};

LtvCollection collect(Document& doc, const LtvOptions& options, net::HttpFetcher* fetcher) {
    LtvCollection out;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return out;
    }
    fz_context* ctx = doc.context().get();
    out.existing = readDss(ctx, pdf);
    const std::vector<FieldData> fields = signedFieldData(ctx, pdf);
    const DocMdp mdp = readDocMdp(ctx, pdf);
    const std::time_t now = options.now.value_or(std::time(nullptr));

    Collector c(options, fetcher);
    RevocationData existing{out.existing.ocsps, out.existing.crls};
    c.addKnown(existing);
    for (const Bytes& der : out.existing.certs) {
        try {
            Ossl<X509> x = loadCertificate(der);
            c.addToPool(x.get());
        } catch (const std::exception&) {
            ERR_clear_error();
        }
    }
    // Разбор всех подписей — пул сертификатов документа до цепочек.
    struct Parsed {
        CmsInfo cms;
        bool ok = false;
        CmsInfo token;  // метка подписи
        bool hasToken = false;
        std::optional<std::time_t> tokenTime;
    };
    std::vector<Parsed> parsed(fields.size());
    for (std::size_t i = 0; i < fields.size(); ++i) {
        try {
            parsed[i].cms = parseCms(fields[i].der);
            parsed[i].ok = true;
            c.addToPool(parsed[i].cms.certificates);
            if (const CmsAttribute* a = parsed[i].cms.signedAttribute(oid::kAdobeRevocation)) {
                c.addKnown(archivalRevocation(a->value));
            }
            const Bytes& token =
                fields[i].subFilter == "ETSI.RFC3161" ? fields[i].der : parsed[i].cms.timestampToken;
            if (!token.empty()) {
                parsed[i].token = fields[i].subFilter == "ETSI.RFC3161" ? parsed[i].cms : parseCms(token);
                parsed[i].hasToken = true;
                c.addToPool(parsed[i].token.certificates);
                parsed[i].tokenTime = isoTime(parseTimestampToken(token).genTime);
            }
        } catch (const std::exception& e) {
            ERR_clear_error();
            spdlog::debug("ltv: {}: {}", fields[i].name, e.what());
        }
    }

    for (std::size_t i = 0; i < fields.size(); ++i) {
        const FieldData& f = fields[i];
        LtvSignature sig;
        sig.field = f.name;
        sig.kind = f.subFilter == "ETSI.RFC3161"                    ? SignatureKind::DocTimestamp
                   : (mdp.signatureObject == f.vnum && f.vnum != 0) ? SignatureKind::Certification
                                                                    : SignatureKind::Approval;
        sig.vriKey = f.der.empty() ? std::string() : sha1Hex(f.der);
        sig.chainComplete = true;
        sig.revocationComplete = true;
        const Parsed& p = parsed[i];
        if (!p.ok) {
            sig.chainComplete = false;
            sig.missing.emplace_back("signature contents are not a CMS");
            out.signatures.push_back(std::move(sig));
            continue;
        }
        const std::time_t claimed = pdfDate(f.m).value_or(now);
        const auto signer = [&](const CmsInfo& cms) -> Ossl<X509> {
            if (cms.signerCertIndex >= 0) {
                return loadCertificate(cms.certificates[static_cast<std::size_t>(cms.signerCertIndex)].der);
            }
            if (X509* x = c.findInPool(cms.signerIssuer, cms.signerSerial)) {
                X509_up_ref(x);
                return Ossl<X509>(x);
            }
            return Ossl<X509>();
        };
        try {
            if (sig.kind == SignatureKind::DocTimestamp) {
                Ossl<X509> tsa = signer(p.cms);
                if (tsa) {
                    c.chain(tsa.get(), p.tokenTime.value_or(now), sig);
                } else {
                    sig.chainComplete = false;
                    sig.missing.emplace_back("time-stamping certificate is not in the document");
                }
            } else {
                Ossl<X509> leaf = signer(p.cms);
                const std::time_t at = p.tokenTime.value_or(claimed);
                if (leaf) {
                    c.chain(leaf.get(), at, sig);
                } else {
                    sig.chainComplete = false;
                    sig.missing.emplace_back("signer certificate is not in the document");
                }
                if (p.hasToken) {
                    Ossl<X509> tsa = signer(p.token);
                    if (tsa) {
                        c.chain(tsa.get(), at, sig);
                    } else {
                        sig.chainComplete = false;
                        sig.missing.emplace_back(
                            "time-stamping certificate is not in the signature time-stamp");
                    }
                }
            }
            c.responders(p.tokenTime.value_or(claimed), sig);
        } catch (const std::exception& e) {
            ERR_clear_error();
            sig.chainComplete = false;
            sig.missing.push_back(e.what());
        }
        out.material.merge(sig.material);
        out.signatures.push_back(std::move(sig));
    }
    return out;
}

// Ссылка на поток с DER: существующий элемент массива или новый поток (в конец массива).
class DssArray {
public:
    DssArray(fz_context* ctx, pdf_document* pdf, pdf_obj* dss, const char* key) : ctx_(ctx), pdf_(pdf) {
        mu::call(ctx, [&] {
            arr_ = pdf_dict_gets(ctx, dss, key);
            if (!pdf_is_array(ctx, arr_)) {
                pdf_dict_puts_drop(ctx, dss, key, pdf_new_array(ctx, pdf, 4));
                arr_ = pdf_dict_gets(ctx, dss, key);
            }
        });
        int n = 0;
        mu::call(ctx, [&] { n = pdf_array_len(ctx, arr_); });
        for (int i = 0; i < n; ++i) {
            mu::BufferHandle buf(ctx);
            pdf_obj* item = nullptr;
            try {
                mu::call(ctx, [&] {
                    item = pdf_array_get(ctx, arr_, i);
                    if (pdf_is_stream(ctx, item)) {
                        buf.reset(pdf_load_stream(ctx, item));
                    }
                });
            } catch (const std::exception&) {
                continue;
            }
            if (!buf) {
                continue;
            }
            unsigned char* data = nullptr;
            const std::size_t len = fz_buffer_storage(ctx, buf, &data);
            Bytes der(data, data + len);
            if (refs_.find(der) == refs_.end()) {
                refs_.emplace(std::move(der), pdf_to_num(ctx, item));
            }
        }
    }

    // Номер объекта потока; added — новый.
    int ref(const Bytes& der, bool& added) {
        added = false;
        if (const auto it = refs_.find(der); it != refs_.end()) {
            return it->second;
        }
        mu::BufferHandle buf(ctx_);
        mu::call(ctx_, [&] { buf.reset(fz_new_buffer_from_copied_data(ctx_, der.data(), der.size())); });
        int num = 0;
        mu::call(ctx_, [&] {
            pdf_obj* s = pdf_add_stream(ctx_, pdf_, buf, nullptr, 0);
            num = pdf_to_num(ctx_, s);
            pdf_array_push_drop(ctx_, arr_, s);
        });
        refs_.emplace(der, num);
        added = true;
        return num;
    }

private:
    fz_context* ctx_;
    pdf_document* pdf_;
    pdf_obj* arr_ = nullptr;
    std::map<Bytes, int> refs_;
};

}  // namespace

std::size_t LtvMaterial::bytes() const noexcept {
    std::size_t n = 0;
    for (const auto* set : {&certs, &ocsps, &crls}) {
        for (const Bytes& b : *set) {
            n += b.size();
        }
    }
    return n;
}

int LtvMaterial::merge(const LtvMaterial& other) {
    int added = 0;
    for (const Bytes& b : other.certs) {
        added += addUnique(certs, b) ? 1 : 0;
    }
    for (const Bytes& b : other.ocsps) {
        added += addUnique(ocsps, b) ? 1 : 0;
    }
    for (const Bytes& b : other.crls) {
        added += addUnique(crls, b) ? 1 : 0;
    }
    return added;
}

LtvMaterial LtvMaterial::minus(const LtvMaterial& other) const {
    LtvMaterial out;
    for (const Bytes& b : certs) {
        if (!hasDer(other.certs, b)) {
            out.certs.push_back(b);
        }
    }
    for (const Bytes& b : ocsps) {
        if (!hasDer(other.ocsps, b)) {
            out.ocsps.push_back(b);
        }
    }
    for (const Bytes& b : crls) {
        if (!hasDer(other.crls, b)) {
            out.crls.push_back(b);
        }
    }
    return out;
}

LtvCollection collectLtv(Document& doc, const LtvOptions& options) {
    return collect(doc, options, options.fetcher);
}

DssWriteResult writeDss(Document& doc, const LtvCollection& collected, bool vri) {
    DssWriteResult result;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return result;
    }
    fz_context* ctx = doc.context().get();
    const LtvMaterial add = collected.added();
    // /VRI: записи подписей, которых ещё нет (или без материала).
    std::vector<const LtvSignature*> vriTodo;
    if (vri) {
        for (const LtvSignature& s : collected.signatures) {
            if (s.vriKey.empty() || s.material.empty()) {
                continue;
            }
            int present = 0;
            const char* key = s.vriKey.c_str();
            mu::call(ctx, [&] {
                pdf_obj* v = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/DSS/VRI");
                present = pdf_is_dict(ctx, pdf_dict_gets(ctx, v, key)) ? 1 : 0;
            });
            if (present == 0 || !add.empty()) {
                vriTodo.push_back(&s);
            }
        }
    }
    if (add.empty() && vriTodo.empty()) {
        return result;
    }

    Journal::Operation op(doc, "Add validation info");
    pdf_obj* dss = nullptr;
    mu::call(ctx, [&] {
        pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        dss = pdf_dict_gets(ctx, root, "DSS");
        if (!pdf_is_dict(ctx, dss)) {
            pdf_dict_puts_drop(ctx, root, "DSS", pdf_add_new_dict(ctx, pdf, 4));
            dss = pdf_dict_gets(ctx, root, "DSS");
            pdf_dict_put_drop(ctx, dss, PDF_NAME(Type), pdf_new_name(ctx, "DSS"));
        }
    });
    DssArray certs(ctx, pdf, dss, "Certs");
    DssArray ocsps(ctx, pdf, dss, "OCSPs");
    DssArray crls(ctx, pdf, dss, "CRLs");
    const auto put = [&](DssArray& arr, const std::vector<Bytes>& set, int& counter) {
        for (const Bytes& der : set) {
            bool added = false;
            (void)arr.ref(der, added);
            counter += added ? 1 : 0;
        }
    };
    put(certs, collected.material.certs, result.certs);
    put(ocsps, collected.material.ocsps, result.ocsps);
    put(crls, collected.material.crls, result.crls);

    for (const LtvSignature* s : vriTodo) {
        std::vector<int> c;
        std::vector<int> o;
        std::vector<int> l;
        bool added = false;
        for (const Bytes& der : s->material.certs) {
            c.push_back(certs.ref(der, added));
        }
        for (const Bytes& der : s->material.ocsps) {
            o.push_back(ocsps.ref(der, added));
        }
        for (const Bytes& der : s->material.crls) {
            l.push_back(crls.ref(der, added));
        }
        const char* key = s->vriKey.c_str();
        pdf_obj* entry = nullptr;
        mu::call(ctx, [&] {
            pdf_obj* v = pdf_dict_gets(ctx, dss, "VRI");
            if (!pdf_is_dict(ctx, v)) {
                pdf_dict_puts_drop(ctx, dss, "VRI", pdf_new_dict(ctx, pdf, 2));
                v = pdf_dict_gets(ctx, dss, "VRI");
            }
            pdf_dict_puts_drop(ctx, v, key, pdf_new_dict(ctx, pdf, 3));
            entry = pdf_dict_gets(ctx, v, key);
        });
        const std::pair<const char*, const std::vector<int>*> lists[] = {
            {"Cert", &c}, {"OCSP", &o}, {"CRL", &l}};
        for (const auto& [name, nums] : lists) {
            if (nums->empty()) {
                continue;
            }
            const int* data = nums->data();
            const int count = static_cast<int>(nums->size());
            mu::call(ctx, [&] {
                pdf_obj* arr = pdf_new_array(ctx, pdf, count);
                pdf_dict_puts_drop(ctx, entry, name, arr);
                for (int i = 0; i < count; ++i) {
                    pdf_array_push_drop(ctx, arr, pdf_new_indirect(ctx, pdf, data[i], 0));
                }
            });
        }
        ++result.vri;
    }
    op.commit();
    result.written = true;
    return result;
}

LtvResult addValidationInfo(Document& doc, const LtvOptions& options, const fs::path& target) {
    LtvResult result;
    if (doc.pdf() == nullptr) {
        result.save.error = SaveError::NotPdf;
        result.save.message = "only PDF documents carry validation info";
        return result;
    }
    const fs::path out = target.empty() ? doc.path() : target;
    if (out.empty()) {
        result.save.error = SaveError::NoPath;
        result.save.message = "the document has no file";
        return result;
    }
    if (detail::needsPreSave(doc)) {
        result.save = Saver::save(doc, out);
        if (!result.save.ok()) {
            return result;
        }
    }
    result.collected = collectLtv(doc, options);
    try {
        result.dss = writeDss(doc, result.collected, options.vri);
    } catch (...) {
        Saver::revert(doc);
        throw;
    }
    if (!result.dss.written) {
        if (out != doc.path()) {
            result.save = Saver::save(doc, out);  // «Сохранить как» без нового материала — просто запись
        }
        return result;
    }
    SaveOptions save;
    save.mode = SaveMode::Incremental;
    result.save = Saver::save(doc, out, save);
    if (!result.save.ok() && result.save.error != SaveError::ReplaceFailed &&
        result.save.error != SaveError::ReopenFailed) {
        try {
            Saver::revert(doc);
        } catch (const std::exception& e) {
            spdlog::error("ltv: revert failed: {}", e.what());
        }
    }
    spdlog::info("ltv: {} validation info: +{} certificates, +{} OCSP, +{} CRL, {} VRI ({})",
                 paths::toUtf8(out), result.dss.certs, result.dss.ocsps, result.dss.crls, result.dss.vri,
                 result.save.ok() ? "saved" : result.save.message);
    return result;
}

ArchiveResult extendArchiveTimestamp(Document& doc, std::shared_ptr<TsaClient> tsa, const LtvOptions& options,
                                     const fs::path& target) {
    if (!tsa) {
        throw SignError(SignStatus::TimestampFailed, "no timestamp service", SaveError::None,
                        TsaStatus::NetworkError);
    }
    ArchiveResult result;
    result.ltv = addValidationInfo(doc, options, target);
    if (!result.ltv.save.ok()) {
        throw SignError(SignStatus::SaveFailed, "validation info: " + result.ltv.save.message,
                        result.ltv.save.error);
    }
    // Записано (в т. ч. в новый путь) — документ уже на цели.
    result.timestamp =
        addDocumentTimestamp(doc, std::move(tsa), result.ltv.save.bytes > 0 ? fs::path() : target);
    return result;
}

std::vector<LtvSignature> ltvStatus(Document& doc, const LtvOptions& options) {
    return collect(doc, options, nullptr).signatures;
}

bool ltvEnabled(Document& doc, const SignatureInfo& sig, const LtvOptions& options) {
    for (const LtvSignature& s : ltvStatus(doc, options)) {
        if (s.field == sig.field) {
            return s.complete();
        }
    }
    return false;
}

Bytes ocspResponseDer(std::span<const std::uint8_t> der) {
    if (der.empty()) {
        return {};
    }
    const unsigned char* p = der.data();
    Ossl<OCSP_RESPONSE> resp(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(der.size())));
    if (resp) {
        return Bytes(der.begin(), der.end());
    }
    ERR_clear_error();
    p = der.data();
    Ossl<OCSP_BASICRESP> basic(d2i_OCSP_BASICRESP(nullptr, &p, static_cast<long>(der.size())));
    if (!basic) {
        ERR_clear_error();
        return {};
    }
    Ossl<OCSP_RESPONSE> wrapped(OCSP_response_create(OCSP_RESPONSE_STATUS_SUCCESSFUL, basic.get()));
    if (!wrapped) {
        ERR_clear_error();
        return {};
    }
    return ossl::toDer(wrapped.get(), i2d_OCSP_RESPONSE, "i2d_OCSP_RESPONSE");
}

LtvAutoAdd ltvAutoAddFromName(std::string_view name) {
    if (name == "never") {
        return LtvAutoAdd::Never;
    }
    if (name == "always") {
        return LtvAutoAdd::Always;
    }
    return LtvAutoAdd::Ask;
}

const char* ltvAutoAddName(LtvAutoAdd mode) {
    switch (mode) {
        case LtvAutoAdd::Never:
            return "never";
        case LtvAutoAdd::Always:
            return "always";
        case LtvAutoAdd::Ask:
            break;
    }
    return "ask";
}

LtvDecision decideAutoLtv(LtvAutoAdd mode, std::uintmax_t fileSize, std::size_t materialBytes) {
    if (mode == LtvAutoAdd::Never || materialBytes == 0) {
        return LtvDecision::Skip;
    }
    if (mode == LtvAutoAdd::Always) {
        return LtvDecision::Add;
    }
    const std::uintmax_t limit = fileSize / 10 + 10240;
    return materialBytes > limit ? LtvDecision::Ask : LtvDecision::Add;
}

}  // namespace ot::sign
