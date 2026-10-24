#include "core/sign/verifier.h"

#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/objects.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <stdexcept>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/cms.h"
#include "core/sign/der.h"
#include "core/sign/tsa.h"

namespace ot::sign {

namespace {

constexpr std::int64_t kMaxHole = 4 << 20;  // «дыра» /ByteRange больше — не /Contents подписи
constexpr std::size_t kChunk = 1 << 16;

// --- чтение файла документа
// -------------------------------------------------------------------------------------

// Байты [offset, offset + length) потока документа.
Bytes readBytes(fz_context* ctx, pdf_document* pdf, std::int64_t offset, std::int64_t length) {
    Bytes out(static_cast<std::size_t>(length));
    std::size_t got = 0;
    mu::call(ctx, [&] {
        fz_seek(ctx, pdf->file, offset, SEEK_SET);
        while (got < out.size()) {
            const std::size_t n = fz_read(ctx, pdf->file, out.data() + got, out.size() - got);
            if (n == 0) {
                break;
            }
            got += n;
        }
    });
    out.resize(got);
    return out;
}

// Отрезки /ByteRange по кускам в sink (вызывается вне mu::call).
template <typename Sink>
void streamRanges(fz_context* ctx, pdf_document* pdf, const std::vector<ByteRange>& ranges, Sink&& sink) {
    Bytes buf(kChunk);
    for (const ByteRange& r : ranges) {
        std::int64_t pos = r.offset;
        std::int64_t left = r.length;
        while (left > 0) {
            const auto want = static_cast<std::size_t>(std::min<std::int64_t>(left, kChunk));
            std::size_t got = 0;
            mu::call(ctx, [&] {
                fz_seek(ctx, pdf->file, pos, SEEK_SET);
                got = fz_read(ctx, pdf->file, buf.data(), want);
            });
            if (got == 0) {
                throw std::runtime_error("signed range is truncated");
            }
            sink(buf.data(), got);
            pos += static_cast<std::int64_t>(got);
            left -= static_cast<std::int64_t>(got);
        }
    }
}

Bytes digestOfRanges(fz_context* ctx, pdf_document* pdf, const std::vector<ByteRange>& ranges,
                     const EVP_MD* md) {
    Ossl<EVP_MD_CTX> mctx(ossl::checkPtr(EVP_MD_CTX_new(), "EVP_MD_CTX_new"));
    ossl::check(EVP_DigestInit_ex(mctx.get(), md, nullptr), "EVP_DigestInit_ex");
    streamRanges(ctx, pdf, ranges,
                 [&](const std::uint8_t* p, std::size_t n) { EVP_DigestUpdate(mctx.get(), p, n); });
    Bytes out(EVP_MAX_MD_SIZE);
    unsigned int len = 0;
    ossl::check(EVP_DigestFinal_ex(mctx.get(), out.data(), &len), "EVP_DigestFinal_ex");
    out.resize(len);
    return out;
}

Bytes digestOf(std::span<const std::uint8_t> data, const EVP_MD* md) {
    Bytes out(EVP_MAX_MD_SIZE);
    unsigned int len = 0;
    ossl::check(EVP_Digest(data.data(), data.size(), out.data(), &len, md, nullptr), "EVP_Digest");
    out.resize(len);
    return out;
}

const EVP_MD* mdByOid(const std::string& dotted) {
    Ossl<ASN1_OBJECT> obj(OBJ_txt2obj(dotted.c_str(), 1));
    const EVP_MD* md = obj ? EVP_get_digestbyobj(obj.get()) : nullptr;
    ERR_clear_error();
    return md;
}

// --- объекты PDF
// ------------------------------------------------------------------------------------------------

std::string nameText(fz_context* ctx, pdf_obj* o) {
    const char* s = nullptr;
    mu::call(ctx, [&] { s = pdf_is_name(ctx, o) ? pdf_to_name(ctx, o) : nullptr; });
    return s != nullptr ? s : "";
}

std::string textString(fz_context* ctx, pdf_obj* o) {
    const char* s = nullptr;
    mu::call(ctx, [&] { s = pdf_is_string(ctx, o) ? pdf_to_text_string(ctx, o) : nullptr; });
    return s != nullptr ? s : "";
}

// Дата PDF «D:20260101120000Z» / «D:20180104115357+01'00'» → ISO UTC.
std::string pdfDateToIso(const std::string& d) {
    std::string s = d.rfind("D:", 0) == 0 ? d.substr(2) : d;
    if (s.size() < 4) {
        return {};
    }
    auto num = [&](std::size_t at, std::size_t len, int def) {
        if (s.size() < at + len) {
            return def;
        }
        for (std::size_t i = at; i < at + len; ++i) {
            if (s[i] < '0' || s[i] > '9') {
                return def;
            }
        }
        return std::stoi(s.substr(at, len));
    };
    std::tm tm{};
    tm.tm_year = num(0, 4, 1970) - 1900;
    tm.tm_mon = num(4, 2, 1) - 1;
    tm.tm_mday = num(6, 2, 1);
    tm.tm_hour = num(8, 2, 0);
    tm.tm_min = num(10, 2, 0);
    tm.tm_sec = num(12, 2, 0);
    std::time_t t = _mkgmtime(&tm);
    if (s.size() > 14 && (s[14] == '+' || s[14] == '-')) {
        const int hh = num(15, 2, 0);
        const int mm = s.size() >= 20 && s[17] == '\'' ? num(18, 2, 0) : num(17, 2, 0);
        const int offset = (hh * 60 + mm) * 60;
        t += s[14] == '+' ? -offset : offset;
    }
    if (t == static_cast<std::time_t>(-1)) {
        return {};
    }
    std::tm utc{};
    gmtime_s(&utc, &t);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buf;
}

struct FieldScan {
    std::vector<pdf_obj*> fields;
};

void collectSignedField(fz_context* ctx, pdf_obj* field, void* arg, pdf_obj** inherited) {
    pdf_obj* ft = inherited[0];
    if (ft == nullptr || !pdf_name_eq(ctx, ft, PDF_NAME(Sig)) ||
        !pdf_is_dict(ctx, pdf_dict_get(ctx, field, PDF_NAME(V)))) {
        return;
    }
    static_cast<FieldScan*>(arg)->fields.push_back(field);
}

std::vector<pdf_obj*> signedFields(fz_context* ctx, pdf_document* pdf) {
    FieldScan scan;
    pdf_obj* names[2] = {PDF_NAME(FT), nullptr};
    pdf_obj* values[2] = {nullptr, nullptr};
    mu::call(ctx, [&] {
        pdf_obj* fields = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/AcroForm/Fields");
        if (pdf_is_array(ctx, fields)) {
            pdf_walk_tree(ctx, fields, PDF_NAME(Kids), collectSignedField, nullptr, &scan, names, values);
        }
    });
    return scan.fields;
}

// Страница, рамка и видимость виджета поля (сам виджет или первый потомок).
void widgetPlacement(fz_context* ctx, pdf_document* pdf, pdf_obj* field, SignatureInfo& out) {
    mu::call(ctx, [&] {
        pdf_obj* widget = field;
        if (!pdf_name_eq(ctx, pdf_dict_get(ctx, field, PDF_NAME(Subtype)), PDF_NAME(Widget))) {
            pdf_obj* kids = pdf_dict_get(ctx, field, PDF_NAME(Kids));
            widget = pdf_array_len(ctx, kids) > 0 ? pdf_array_get(ctx, kids, 0) : nullptr;
        }
        if (widget == nullptr) {
            return;
        }
        pdf_obj* pageObj = pdf_dict_get(ctx, widget, PDF_NAME(P));
        int page = pageObj != nullptr ? pdf_lookup_page_number(ctx, pdf, pageObj) : -1;
        if (page < 0) {
            const int n = pdf_count_pages(ctx, pdf);
            for (int i = 0; i < n && page < 0; ++i) {
                pdf_obj* p = pdf_lookup_page_obj(ctx, pdf, i);
                if (pdf_array_find(ctx, pdf_dict_get(ctx, p, PDF_NAME(Annots)), widget) >= 0) {
                    page = i;
                    pageObj = p;
                }
            }
        }
        fz_rect rect = pdf_dict_get_rect(ctx, widget, PDF_NAME(Rect));
        if (page >= 0) {
            fz_rect box;
            fz_matrix ctm;
            pdf_page_obj_transform(ctx, pdf_lookup_page_obj(ctx, pdf, page), &box, &ctm);
            rect = fz_transform_rect(rect, ctm);
        }
        const int flags = pdf_dict_get_int(ctx, widget, PDF_NAME(F));
        out.page = page;
        out.rect = {rect.x0, rect.y0, rect.x1, rect.y1};
        out.visible = !fz_is_empty_rect(rect) && rect.x1 - rect.x0 > 0.5f && rect.y1 - rect.y0 > 0.5f &&
                      (flags & (PDF_ANNOT_IS_HIDDEN | PDF_ANNOT_IS_NO_VIEW)) == 0;
    });
}

// Сертификаты /DSS /Certs.
std::vector<Ossl<X509>> dssCertificates(fz_context* ctx, pdf_document* pdf) {
    std::vector<Ossl<X509>> out;
    pdf_obj* certs = nullptr;
    int n = 0;
    mu::call(ctx, [&] {
        certs = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/DSS/Certs");
        n = pdf_array_len(ctx, certs);
    });
    for (int i = 0; i < n; ++i) {
        mu::BufferHandle buf(ctx);
        try {
            mu::call(ctx, [&] {
                pdf_obj* s = pdf_array_get(ctx, certs, i);
                if (pdf_is_stream(ctx, s)) {
                    buf.reset(pdf_load_stream(ctx, s));
                }
            });
        } catch (const std::exception& e) {
            spdlog::debug("verifier: DSS certificate {}: {}", i, e.what());
            continue;
        }
        if (!buf) {
            continue;
        }
        unsigned char* data = nullptr;
        const std::size_t len = fz_buffer_storage(ctx, buf, &data);
        const unsigned char* p = data;
        Ossl<X509> x(d2i_X509(nullptr, &p, static_cast<long>(len)));
        ERR_clear_error();
        if (x) {
            out.push_back(std::move(x));
        }
    }
    return out;
}

bool isHex(std::uint8_t c) { return std::isxdigit(c) != 0; }
bool isSpace(std::uint8_t c) {
    return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == 0;
}

// Длина DER/BER первого элемента (0 — не разобрать).
std::size_t encodedLength(const Bytes& der) {
    try {
        der::Reader r(der);
        return r.next().encoded.size();
    } catch (const der::DerError&) {
        return 0;
    }
}

// --- проверка одной подписи
// -------------------------------------------------------------------------------------

class SignatureCheck {
public:
    SignatureCheck(fz_context* ctx, pdf_document* pdf, SignatureInfo& sig, STACK_OF(X509) * extraCerts)
        : ctx_(ctx), pdf_(pdf), sig_(sig), extra_(extraCerts) {}

    // (1) структура: /ByteRange и /Contents в «дыре». false — дальше не проверять.
    bool structure(pdf_obj* v);
    // (2) целостность по /SubFilter.
    void integrity();

private:
    void add(Issue issue, std::string detail) { sig_.findings.push_back({issue, std::move(detail)}); }
    void checkCms(bool sha1Content);
    void checkDocTimestamp();
    bool checkAlgorithms(const std::string& digestOid, const std::string& signatureOid);

    fz_context* ctx_;
    pdf_document* pdf_;
    SignatureInfo& sig_;
    STACK_OF(X509) * extra_;
    Bytes contents_;  // DER без дополнения
};

bool SignatureCheck::structure(pdf_obj* v) {
    pdf_obj* br = nullptr;
    pdf_obj* contents = nullptr;
    int n = 0;
    bool allNumbers = true;
    std::vector<std::int64_t> nums;
    mu::call(ctx_, [&] {
        br = pdf_dict_get(ctx_, v, PDF_NAME(ByteRange));
        contents = pdf_dict_get(ctx_, v, PDF_NAME(Contents));
        n = pdf_is_array(ctx_, br) ? pdf_array_len(ctx_, br) : -1;
        for (int i = 0; i < n; ++i) {
            pdf_obj* x = pdf_array_get(ctx_, br, i);
            allNumbers = allNumbers && pdf_is_int(ctx_, x);
            nums.push_back(pdf_to_int64(ctx_, x));
        }
    });
    const char* cbuf = nullptr;
    std::size_t clen = 0;
    mu::call(ctx_, [&] {
        if (pdf_is_string(ctx_, contents)) {
            cbuf = pdf_to_str_buf(ctx_, contents);
            clen = pdf_to_str_len(ctx_, contents);
        }
    });
    if (n < 0) {
        add(Issue::NoByteRange, "no /ByteRange");
        return false;
    }
    for (int i = 0; i + 1 < n; i += 2) {
        sig_.byteRange.push_back({nums[static_cast<std::size_t>(i)], nums[static_cast<std::size_t>(i) + 1]});
    }
    for (const ByteRange& r : sig_.byteRange) {
        sig_.rangeEnd = std::max(sig_.rangeEnd, r.offset + r.length);
    }
    sig_.coversWholeFile = sig_.rangeEnd == sig_.fileSize;
    if (cbuf == nullptr || clen == 0) {
        add(Issue::NoContents, "no /Contents");
        return false;
    }
    contents_.assign(reinterpret_cast<const std::uint8_t*>(cbuf),
                     reinterpret_cast<const std::uint8_t*>(cbuf) + clen);
    if (n != 4 || !allNumbers) {
        add(Issue::BadByteRange, "/ByteRange must hold 4 integers, has " + std::to_string(n));
        return false;
    }
    const ByteRange& a = sig_.byteRange[0];
    const ByteRange& b = sig_.byteRange[1];
    if (a.offset != 0 || a.length <= 0 || b.length < 0 || b.offset <= a.length ||
        b.offset + b.length > sig_.fileSize) {
        add(Issue::BadByteRange, "/ByteRange [" + std::to_string(a.offset) + " " + std::to_string(a.length) +
                                     " " + std::to_string(b.offset) + " " + std::to_string(b.length) +
                                     "] does not fit the file of " + std::to_string(sig_.fileSize) +
                                     " bytes");
        return false;
    }
    const std::int64_t holeSize = b.offset - a.length;
    if (holeSize < 2 || holeSize > kMaxHole) {
        add(Issue::BadByteRange, "gap of " + std::to_string(holeSize) + " bytes is not a signature value");
        return false;
    }
    // «Дыра» — ровно hex-строка /Contents.
    const Bytes hole = readBytes(ctx_, pdf_, a.length, holeSize);
    bool hex = hole.size() == static_cast<std::size_t>(holeSize) && hole.front() == '<' && hole.back() == '>';
    Bytes decoded;
    int half = -1;
    for (std::size_t i = 1; hex && i + 1 < hole.size(); ++i) {
        const std::uint8_t c = hole[i];
        if (isSpace(c) && c != 0) {
            continue;
        }
        if (!isHex(c)) {
            hex = false;
            break;
        }
        const int d = std::isdigit(c) ? c - '0' : (std::tolower(c) - 'a' + 10);
        if (half < 0) {
            half = d;
        } else {
            decoded.push_back(static_cast<std::uint8_t>(half * 16 + d));
            half = -1;
        }
    }
    if (half >= 0) {
        decoded.push_back(static_cast<std::uint8_t>(half * 16));
    }
    if (!hex) {
        add(Issue::BadByteRange, "the gap between the signed ranges is not a hexadecimal string");
        return false;
    }
    if (decoded != contents_) {
        add(Issue::BadByteRange, "the gap between the signed ranges is not the /Contents of this signature");
        return false;
    }
    // DER без дополнения нулями.
    const std::size_t used = encodedLength(contents_);
    if (used > 0 && used <= contents_.size()) {
        contents_.resize(used);
    }
    sig_.contentsSize = contents_.size();
    if (!sig_.coversWholeFile) {
        add(Issue::ModifiedAfterSigning, "the file has " + std::to_string(sig_.fileSize - sig_.rangeEnd) +
                                             " bytes after the signed revision");
    }
    sig_.structureOk = true;
    return true;
}

bool SignatureCheck::checkAlgorithms(const std::string& digestOid, const std::string& signatureOid) {
    bool ok = true;
    for (const std::string* o : {&digestOid, &signatureOid}) {
        switch (algorithmStrength(*o)) {
            case AlgorithmStrength::Forbidden:
                add(Issue::ForbiddenAlgorithm, algorithmName(*o) + " is not accepted");
                ok = false;
                break;
            case AlgorithmStrength::Weak:
                if (!sig_.has(Issue::WeakAlgorithm)) {
                    add(Issue::WeakAlgorithm, algorithmName(*o) + " is obsolete");
                }
                break;
            case AlgorithmStrength::Ok:
                break;
        }
    }
    return ok;
}

void SignatureCheck::integrity() {
    const std::string& sf = sig_.subFilter;
    if (sf == "ETSI.RFC3161") {
        checkDocTimestamp();
    } else if (sf == "ETSI.CAdES.detached" || sf == "adbe.pkcs7.detached") {
        checkCms(false);
    } else if (sf == "adbe.pkcs7.sha1") {
        checkCms(true);
    } else {
        add(Issue::UnsupportedSubFilter, "/SubFilter /" + sf + " is not supported");
    }
}

void SignatureCheck::checkDocTimestamp() {
    TimestampInfo info;
    try {
        info = parseTimestampToken(contents_);
        const CmsInfo cms = parseCms(contents_);
        sig_.digestAlgorithm = cms.digestAlgorithm;
        sig_.signatureAlgorithm = cms.signatureAlgorithm;
        sig_.certificates = cms.certificates;
        sig_.signedAttributes = !cms.signedAttributes.empty();
        sig_.ess = cms.signingCertificateV2.has_value() || cms.hasSigningCertificateV1;
        if (cms.signerCertIndex >= 0) {
            sig_.signer = cms.certificates[static_cast<std::size_t>(cms.signerCertIndex)];
        }
    } catch (const std::exception& e) {
        add(Issue::CmsDamaged, std::string("time-stamp token: ") + e.what());
        return;
    }
    if (!checkAlgorithms(info.hashAlgorithm, sig_.signatureAlgorithm)) {
        return;
    }
    const EVP_MD* md = mdByOid(info.hashAlgorithm);
    if (md == nullptr) {
        add(Issue::UnsupportedAlgorithm,
            "time-stamp imprint algorithm " + info.hashAlgorithm + " is unknown");
        return;
    }
    const Bytes imprint = digestOfRanges(ctx_, pdf_, sig_.byteRange, md);
    if (imprint != info.imprint) {
        add(Issue::DigestMismatch, "time-stamp imprint does not match the signed ranges");
        return;
    }
    std::string error;
    if (!verifyTimestampToken(contents_, imprint, nullptr, &error)) {
        add(Issue::SignatureMismatch, "time-stamp token: " + error);
        return;
    }
    sig_.intact = true;
}

void SignatureCheck::checkCms(bool sha1Content) {
    CmsInfo info;
    try {
        info = parseCms(contents_);
    } catch (const std::exception& e) {
        add(Issue::CmsDamaged, e.what());
        return;
    }
    sig_.digestAlgorithm = info.digestAlgorithm;
    sig_.signatureAlgorithm = info.signatureAlgorithm;
    sig_.certificates = info.certificates;
    sig_.signedAttributes = !info.signedAttributes.empty() || !info.signedAttributesDer.empty();
    sig_.ess = info.signingCertificateV2.has_value() || info.hasSigningCertificateV1;
    if (info.signerCount > 1) {
        spdlog::info("verifier: {}: {} SignerInfos, the first one is checked", sig_.field, info.signerCount);
    }
    if (!checkAlgorithms(info.digestAlgorithm, info.signatureAlgorithm)) {
        return;
    }

    ERR_clear_error();
    const unsigned char* p = contents_.data();
    Ossl<CMS_ContentInfo> cms(d2i_CMS_ContentInfo(nullptr, &p, static_cast<long>(contents_.size())));
    if (!cms || OBJ_obj2nid(CMS_get0_type(cms.get())) != NID_pkcs7_signed) {
        add(Issue::CmsDamaged,
            cms ? "not CMS SignedData" : OsslError::fromQueue("d2i_CMS_ContentInfo").what());
        return;
    }
    STACK_OF(CMS_SignerInfo)* signers = CMS_get0_SignerInfos(cms.get());
    if (signers == nullptr || sk_CMS_SignerInfo_num(signers) < 1) {
        add(Issue::CmsDamaged, "CMS without SignerInfo");
        return;
    }
    CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signers, 0);

    // Алгоритмы известны OpenSSL.
    X509_ALGOR* pdig = nullptr;
    X509_ALGOR* psig = nullptr;
    CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &pdig, &psig);
    const ASN1_OBJECT* digObj = nullptr;
    const ASN1_OBJECT* sigObj = nullptr;
    X509_ALGOR_get0(&digObj, nullptr, nullptr, pdig);
    X509_ALGOR_get0(&sigObj, nullptr, nullptr, psig);
    const EVP_MD* md = digObj != nullptr ? EVP_get_digestbyobj(digObj) : nullptr;
    const int sigNid = sigObj != nullptr ? OBJ_obj2nid(sigObj) : NID_undef;
    int mdNid = NID_undef;
    int pkNid = NID_undef;
    const bool sigKnown = sigNid != NID_undef &&
                          (OBJ_find_sigid_algs(sigNid, &mdNid, &pkNid) == 1 || sigNid == NID_rsaEncryption ||
                           sigNid == NID_rsassaPss || sigNid == NID_X9_62_id_ecPublicKey ||
                           sigNid == NID_dsa || sigNid == NID_ED25519 || sigNid == NID_ED448);
    ERR_clear_error();
    if (md == nullptr || !sigKnown) {
        add(Issue::UnsupportedAlgorithm,
            "algorithm " + algorithmName(md == nullptr ? info.digestAlgorithm : info.signatureAlgorithm) +
                " is not supported");
        return;
    }

    // Сертификат подписанта: CMS, затем /DSS.
    CMS_set1_signers_certs(cms.get(), extra_, 0);
    ERR_clear_error();
    X509* signer = nullptr;
    CMS_SignerInfo_get0_algs(si, nullptr, &signer, nullptr, nullptr);
    if (signer == nullptr) {
        add(Issue::NoSignerCertificate, "the signer certificate is neither in the CMS nor in /DSS");
        return;
    }
    sig_.signer = summarize(signer);

    bool ok = true;
    // ESS signing-certificate(-v2) — о сертификате подписанта.
    const Bytes certDer = ossl::toDer(signer, i2d_X509, "i2d_X509");
    const auto checkEss = [&](const std::optional<EssCertId>& ess, const char* what) {
        if (!ess) {
            return;
        }
        const EVP_MD* essMd = mdByOid(ess->hashAlgorithm);
        if (essMd == nullptr) {
            add(Issue::UnsupportedAlgorithm,
                std::string(what) + ": hash " + ess->hashAlgorithm + " is unknown");
            ok = false;
            return;
        }
        if (digestOf(certDer, essMd) != ess->certHash) {
            add(Issue::EssMismatch, std::string(what) + " does not reference the signer certificate");
            ok = false;
        }
    };
    checkEss(info.signingCertificateV2, "ESS signing-certificate-v2");
    checkEss(info.signingCertificateV1, "ESS signing-certificate");

    // Подписанные данные: eContent (adbe.pkcs7.sha1 — SHA-1 диапазонов; «обёрнутая» CMS — сами диапазоны)
    // или /ByteRange потоком.
    ASN1_OCTET_STRING** econtent = CMS_get0_content(cms.get());
    const bool hasEcontent = econtent != nullptr && *econtent != nullptr;
    Bytes content;
    if (hasEcontent) {
        const unsigned char* d = ASN1_STRING_get0_data(*econtent);
        content.assign(d, d + ASN1_STRING_length(*econtent));
        if (sha1Content) {
            if (content != digestOfRanges(ctx_, pdf_, sig_.byteRange, EVP_sha1())) {
                add(Issue::DigestMismatch, "SHA-1 in the signed content does not match the signed ranges");
                ok = false;
            }
        } else if (static_cast<std::int64_t>(content.size()) != ossl::rangeLength(sig_.byteRange) ||
                   digestOf(content, EVP_sha256()) !=
                       digestOfRanges(ctx_, pdf_, sig_.byteRange, EVP_sha256())) {
            add(Issue::DigestMismatch, "the encapsulated content is not the signed ranges");
            ok = false;
        }
    } else if (sha1Content) {
        add(Issue::CmsDamaged, "adbe.pkcs7.sha1 without encapsulated content");
        return;
    }

    if (sig_.signedAttributes && info.messageDigest.empty()) {
        add(Issue::NoMessageDigest, "signed attributes without message-digest");
        ok = false;
    } else {
        // Хэш данных против message-digest (без атрибутов — подпись над хэшем данных).
        Ossl<BIO> mdBio(ossl::checkPtr(BIO_new(BIO_f_md()), "BIO_new"));
        ossl::check(static_cast<int>(BIO_set_md(mdBio.get(), md)), "BIO_set_md");
        BIO* sink = ossl::checkPtr(BIO_new(BIO_s_null()), "BIO_new");
        BIO_push(mdBio.get(), sink);
        if (hasEcontent) {
            BIO_write(mdBio.get(), content.data(), static_cast<int>(content.size()));
        } else {
            streamRanges(ctx_, pdf_, sig_.byteRange, [&](const std::uint8_t* d, std::size_t n) {
                BIO_write(mdBio.get(), d, static_cast<int>(n));
            });
        }
        const int rc = CMS_SignerInfo_verify_content(si, mdBio.get());
        if (rc != 1) {
            const std::string why = OsslError::fromQueue("CMS_SignerInfo_verify_content").what();
            if (sig_.signedAttributes || hasEcontent) {
                add(Issue::DigestMismatch, "message digest does not match the signed data: " + why);
            } else {
                add(Issue::DigestMismatch, "signature does not match the signed data: " + why);
            }
            ok = false;
        }
    }
    // Подпись над signed-атрибутами. OpenSSL проверяет её над перекодировкой атрибутов в полученном порядке;
    // подписаны же байты из файла (RFC 5652, 5.4) — атрибут закодирован не по DER (длины и т. п.) — подпись
    // не сходится, как у pyHanko и DSS (dss-818 «HASH_FAILURE»).
    if (sig_.signedAttributes) {
        if (CMS_SignerInfo_verify(si) != 1) {
            add(Issue::SignatureMismatch, OsslError::fromQueue("CMS_SignerInfo_verify").what());
            ok = false;
        } else {
            Bytes attrs;
            for (int k = 0; k < CMS_signed_get_attr_count(si); ++k) {
                const Bytes a =
                    ossl::toDer(CMS_signed_get_attr(si, k), i2d_X509_ATTRIBUTE, "i2d_X509_ATTRIBUTE");
                attrs.insert(attrs.end(), a.begin(), a.end());
            }
            if (der::encode(der::kSet, attrs) != info.signedAttributesDer) {
                add(Issue::SignatureMismatch,
                    "signed attributes are not DER-encoded: the signed bytes differ");
                ok = false;
            }
        }
    }
    ERR_clear_error();
    sig_.intact = ok;
}

}  // namespace

const char* signatureKindName(SignatureKind kind) {
    switch (kind) {
        case SignatureKind::Approval:
            return "Approval";
        case SignatureKind::Certification:
            return "Certification";
        case SignatureKind::DocTimestamp:
            return "DocTimestamp";
    }
    return "?";
}

const char* verdictName(Verdict verdict) {
    switch (verdict) {
        case Verdict::Valid:
            return "Valid";
        case Verdict::ValidWithChanges:
            return "ValidWithChanges";
        case Verdict::ValidUntrusted:
            return "ValidUntrusted";
        case Verdict::Unknown:
            return "Unknown";
        case Verdict::Invalid:
            return "Invalid";
    }
    return "?";
}

const char* issueName(Issue issue) {
    switch (issue) {
        case Issue::NoByteRange:
            return "NoByteRange";
        case Issue::BadByteRange:
            return "BadByteRange";
        case Issue::NoContents:
            return "NoContents";
        case Issue::CmsDamaged:
            return "CmsDamaged";
        case Issue::UnsupportedSubFilter:
            return "UnsupportedSubFilter";
        case Issue::UnsupportedAlgorithm:
            return "UnsupportedAlgorithm";
        case Issue::NoSignerCertificate:
            return "NoSignerCertificate";
        case Issue::NoMessageDigest:
            return "NoMessageDigest";
        case Issue::DigestMismatch:
            return "DigestMismatch";
        case Issue::SignatureMismatch:
            return "SignatureMismatch";
        case Issue::EssMismatch:
            return "EssMismatch";
        case Issue::ForbiddenAlgorithm:
            return "ForbiddenAlgorithm";
        case Issue::WeakAlgorithm:
            return "WeakAlgorithm";
        case Issue::ModifiedAfterSigning:
            return "ModifiedAfterSigning";
    }
    return "?";
}

std::optional<Verdict> issueSeverity(Issue issue) {
    switch (issue) {
        case Issue::NoByteRange:
        case Issue::BadByteRange:
        case Issue::NoContents:
        case Issue::CmsDamaged:
        case Issue::NoMessageDigest:
        case Issue::DigestMismatch:
        case Issue::SignatureMismatch:
        case Issue::EssMismatch:
        case Issue::ForbiddenAlgorithm:
            return Verdict::Invalid;
        case Issue::UnsupportedSubFilter:
        case Issue::UnsupportedAlgorithm:
        case Issue::NoSignerCertificate:
        case Issue::ModifiedAfterSigning:
            return Verdict::Unknown;
        case Issue::WeakAlgorithm:
            return std::nullopt;
    }
    return Verdict::Unknown;
}

AlgorithmStrength algorithmStrength(std::string_view o) {
    static constexpr std::array<std::string_view, 6> kForbidden = {
        "1.2.840.113549.2.5",    // MD5
        "1.2.840.113549.2.2",    // MD2
        "1.2.840.113549.2.4",    // MD4
        "1.2.840.113549.1.1.4",  // md5WithRSAEncryption
        "1.2.840.113549.1.1.2",  // md2WithRSAEncryption
        "1.2.840.113549.1.1.3",  // md4WithRSAEncryption
    };
    static constexpr std::array<std::string_view, 5> kWeak = {
        "1.3.14.3.2.26",         // SHA-1
        "1.2.840.113549.1.1.5",  // sha1WithRSAEncryption
        "1.2.840.10045.4.1",     // ecdsa-with-SHA1
        "1.2.840.10040.4.3",     // dsa-with-sha1
        "1.3.14.3.2.29",         // sha1WithRSA (OIW)
    };
    if (std::find(kForbidden.begin(), kForbidden.end(), o) != kForbidden.end()) {
        return AlgorithmStrength::Forbidden;
    }
    if (std::find(kWeak.begin(), kWeak.end(), o) != kWeak.end()) {
        return AlgorithmStrength::Weak;
    }
    return AlgorithmStrength::Ok;
}

bool SignatureInfo::has(Issue issue) const {
    return std::any_of(findings.begin(), findings.end(), [&](const Finding& f) { return f.issue == issue; });
}

const char* documentStatusName(DocumentStatus status) {
    switch (status) {
        case DocumentStatus::NotSigned:
            return "NotSigned";
        case DocumentStatus::Valid:
            return "Valid";
        case DocumentStatus::Certified:
            return "Certified";
        case DocumentStatus::Problems:
            return "Problems";
        case DocumentStatus::Invalid:
            return "Invalid";
    }
    return "?";
}

Verdict verdictFromFindings(const std::vector<Finding>& findings) {
    Verdict worst = Verdict::Valid;
    for (const Finding& f : findings) {
        if (const auto s = issueSeverity(f.issue); s && static_cast<int>(*s) > static_cast<int>(worst)) {
            worst = *s;
        }
    }
    return worst;
}

DocumentStatus documentStatus(const std::vector<SignatureInfo>& signatures) {
    if (signatures.empty()) {
        return DocumentStatus::NotSigned;
    }
    bool problems = false;
    bool certified = false;
    for (const SignatureInfo& s : signatures) {
        if (s.verdict == Verdict::Invalid) {
            return DocumentStatus::Invalid;
        }
        problems = problems || s.verdict == Verdict::Unknown || s.verdict == Verdict::ValidUntrusted;
        certified = certified || s.kind == SignatureKind::Certification;
    }
    if (problems) {
        return DocumentStatus::Problems;
    }
    return certified ? DocumentStatus::Certified : DocumentStatus::Valid;
}

DocumentVerification verifyDocument(Document& doc, const VerifyOptions& /*options*/) {
    DocumentVerification out;
    pdf_document* pdf = doc.pdf();
    if (pdf == nullptr) {
        return out;
    }
    fz_context* ctx = doc.context().get();
    mu::call(ctx, [&] {
        out.versions = pdf_count_versions(ctx, pdf);
        out.fileSize = pdf->file_size;
    });
    const std::vector<pdf_obj*> fields = signedFields(ctx, pdf);
    if (fields.empty()) {
        return out;
    }
    std::vector<Ossl<X509>> dss = dssCertificates(ctx, pdf);
    Ossl<STACK_OF(X509)> extra(ossl::checkPtr(sk_X509_new_null(), "sk_X509_new_null"));
    for (const Ossl<X509>& x : dss) {
        X509_up_ref(x.get());
        sk_X509_push(extra.get(), x.get());
    }
    pdf_obj* docMdp = nullptr;
    mu::call(ctx, [&] { docMdp = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/Perms/DocMDP"); });

    for (std::size_t i = 0; i < fields.size(); ++i) {
        pdf_obj* field = fields[i];
        SignatureInfo sig;
        sig.index = static_cast<int>(i);
        sig.fileSize = out.fileSize;
        pdf_obj* v = nullptr;
        char* name = nullptr;
        int foundIn = 0;
        bool certification = false;
        mu::call(ctx, [&] {
            v = pdf_dict_get(ctx, field, PDF_NAME(V));
            name = pdf_load_field_name(ctx, field);
            foundIn = pdf_find_version_for_obj(ctx, pdf, v);
            if (docMdp != nullptr && pdf_is_indirect(ctx, v) &&
                pdf_to_num(ctx, docMdp) == pdf_to_num(ctx, v)) {
                certification = true;
            }
            pdf_obj* refs = pdf_dict_get(ctx, v, PDF_NAME(Reference));
            for (int k = 0; k < pdf_array_len(ctx, refs); ++k) {
                if (pdf_name_eq(ctx,
                                pdf_dict_get(ctx, pdf_array_get(ctx, refs, k), PDF_NAME(TransformMethod)),
                                PDF_NAME(DocMDP))) {
                    certification = true;
                }
            }
        });
        sig.field = name != nullptr ? name : "";
        fz_free(ctx, name);
        sig.revision = std::max(1, out.versions - foundIn);
        pdf_obj* type = nullptr;
        pdf_obj* filter = nullptr;
        pdf_obj* sub = nullptr;
        pdf_obj* m = nullptr;
        pdf_obj* nameObj = nullptr;
        pdf_obj* reason = nullptr;
        pdf_obj* location = nullptr;
        pdf_obj* contact = nullptr;
        mu::call(ctx, [&] {
            type = pdf_dict_get(ctx, v, PDF_NAME(Type));
            filter = pdf_dict_get(ctx, v, PDF_NAME(Filter));
            sub = pdf_dict_get(ctx, v, PDF_NAME(SubFilter));
            m = pdf_dict_get(ctx, v, PDF_NAME(M));
            nameObj = pdf_dict_get(ctx, v, PDF_NAME(Name));
            reason = pdf_dict_get(ctx, v, PDF_NAME(Reason));
            location = pdf_dict_get(ctx, v, PDF_NAME(Location));
            contact = pdf_dict_get(ctx, v, PDF_NAME(ContactInfo));
        });
        sig.type = nameText(ctx, type);
        sig.filter = nameText(ctx, filter);
        sig.subFilter = nameText(ctx, sub);
        sig.name = textString(ctx, nameObj);
        sig.reason = textString(ctx, reason);
        sig.location = textString(ctx, location);
        sig.contact = textString(ctx, contact);
        sig.claimedTime = pdfDateToIso(textString(ctx, m));
        if (sig.subFilter == "ETSI.RFC3161" || sig.type == "DocTimeStamp") {
            sig.kind = SignatureKind::DocTimestamp;
        } else if (certification) {
            sig.kind = SignatureKind::Certification;
        }
        widgetPlacement(ctx, pdf, field, sig);

        SignatureCheck check(ctx, pdf, sig, extra.get());
        try {
            if (check.structure(v)) {
                check.integrity();
            }
        } catch (const std::exception& e) {
            sig.findings.push_back({Issue::CmsDamaged, std::string("verification failed: ") + e.what()});
            sig.intact = false;
        }
        Verdict verdict = verdictFromFindings(sig.findings);
        if (!sig.intact && verdict == Verdict::Valid) {
            verdict = Verdict::Unknown;
        }
        // Доверие — п. 2: без якорей подписант не доверен.
        if (verdict == Verdict::Valid) {
            verdict = Verdict::ValidUntrusted;
        }
        sig.verdict = verdict;
        spdlog::debug("verifier: {}: {} ({} findings)", sig.field, verdictName(sig.verdict),
                      sig.findings.size());
        out.signatures.push_back(std::move(sig));
    }
    out.status = documentStatus(out.signatures);
    return out;
}

}  // namespace ot::sign
