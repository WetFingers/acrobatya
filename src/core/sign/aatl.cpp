#include "core/sign/aatl.h"

#include <openssl/err.h>

#include <spdlog/spdlog.h>

#include <set>
#include <string_view>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/verifier.h"
#include "core/sign/x509_info.h"
#include "core/util/paths.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace ot::sign {

namespace {

// Текст между <tag> и </tag> внутри [from, to); пусто — нет.
std::string_view element(std::string_view text, std::string_view tag) {
    const std::string open = "<" + std::string(tag) + ">";
    const std::string close = "</" + std::string(tag) + ">";
    const std::size_t a = text.find(open);
    if (a == std::string_view::npos) {
        return {};
    }
    const std::size_t b = text.find(close, a + open.size());
    if (b == std::string_view::npos) {
        return {};
    }
    return text.substr(a + open.size(), b - a - open.size());
}

bool flag(std::string_view trust, std::string_view tag) {
    const std::string_view v = element(trust, tag);
    return v.find('1') != std::string_view::npos;
}

// Флаги доверия из <Trust>; нет — подписи.
unsigned trustFlags(std::string_view identity) {
    const std::string_view trust = element(identity, "Trust");
    if (trust.empty()) {
        return kTrustSignatures;
    }
    unsigned flags = 0;
    if (flag(trust, "Root")) {
        flags |= kTrustSignatures;
    }
    if (flag(trust, "CertifiedDocuments")) {
        flags |= kTrustCertification | kTrustSignatures;
    }
    if (flag(trust, "DynamicContent")) {
        flags |= kTrustDynamicContent;
    }
    if (flag(trust, "JavaScript")) {
        flags |= kTrustJavaScript;
    }
    return flags != 0 ? flags : kTrustSignatures;
}

// Потоки PDF, где есть <Certificate>.
std::vector<std::string> xmlStreams(Document& doc) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int n = 0;
    mu::call(ctx, [&] { n = pdf_xref_len(ctx, pdf); });
    std::vector<std::string> out;
    for (int i = 1; i < n; ++i) {
        mu::BufferHandle buf(ctx);
        mu::PdfObjHandle ref(ctx);
        try {
            mu::call(ctx, [&] { ref.reset(pdf_new_indirect(ctx, pdf, i, 0)); });
            mu::call(ctx, [&] {
                if (pdf_is_stream(ctx, ref)) {
                    buf.reset(pdf_load_stream(ctx, ref));
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
        const std::string_view text(reinterpret_cast<const char*>(data), len);
        if (text.find("<Certificate>") != std::string_view::npos) {
            out.emplace_back(text);
        }
    }
    return out;
}

}  // namespace

const char* aatlStatusName(AatlStatus status) {
    switch (status) {
        case AatlStatus::Ok:
            return "ok";
        case AatlStatus::NotPdf:
            return "notPdf";
        case AatlStatus::NotATrustList:
            return "notATrustList";
        case AatlStatus::DamagedSignature:
            return "damagedSignature";
    }
    return "?";
}

AatlResult parseAatl(std::string bytes) {
    AatlResult result;
    std::unique_ptr<Document> doc;
    try {
        doc = Document::openOrThrowMemory(std::move(bytes));
    } catch (const std::exception& e) {
        result.status = AatlStatus::NotPdf;
        result.detail = e.what();
        return result;
    }
    if (doc->pdf() == nullptr) {
        result.status = AatlStatus::NotPdf;
        result.detail = "not a PDF";
        return result;
    }
    // Подпись списка — целостность.
    try {
        const DocumentVerification v = verifyDocument(*doc);
        for (const SignatureInfo& s : v.signatures) {
            result.signedList = true;
            result.signatureIntact = s.intact;
            result.signer = s.signer ? s.signer->commonName : std::string();
            if (!s.intact) {
                break;
            }
        }
    } catch (const std::exception& e) {
        spdlog::info("aatl: signature check failed: {}", e.what());
    }
    if (result.signedList && !result.signatureIntact) {
        result.status = AatlStatus::DamagedSignature;
        result.detail = "the trust list signature is broken";
        return result;
    }

    std::set<std::string> seen;
    const auto take = [&](std::string_view b64, unsigned flags) {
        try {
            const Bytes der = ossl::base64Decode(b64);
            Ossl<X509> x = loadCertificate(der);
            const CertSummary s = summarize(x.get());
            if (!seen.insert(ossl::toHex(s.sha256)).second) {
                return;
            }
            ListedCertificate c;
            c.der = s.der;
            c.flags = flags;
            c.label = s.commonName.empty() ? s.subject : s.commonName;
            result.certificates.push_back(std::move(c));
        } catch (const std::exception&) {
            ERR_clear_error();
            ++result.skipped;
        }
    };
    for (const std::string& xml : xmlStreams(*doc)) {
        const std::string_view text(xml);
        std::size_t at = 0;
        bool identities = false;
        while ((at = text.find("<Identity>", at)) != std::string_view::npos) {
            const std::size_t end = text.find("</Identity>", at);
            if (end == std::string_view::npos) {
                break;
            }
            const std::string_view identity = text.substr(at, end - at);
            identities = true;
            ++result.identities;
            const std::string_view cert = element(identity, "Certificate");
            if (!cert.empty()) {
                take(cert, trustFlags(identity));
            }
            at = end;
        }
        if (!identities) {
            // Нет <Identity> — все <Certificate> подряд.
            at = 0;
            while ((at = text.find("<Certificate>", at)) != std::string_view::npos) {
                const std::size_t end = text.find("</Certificate>", at);
                if (end == std::string_view::npos) {
                    break;
                }
                ++result.identities;
                take(text.substr(at + 13, end - at - 13), kTrustSignatures);
                at = end;
            }
        }
    }
    if (static_cast<int>(result.certificates.size()) < kAatlMinCertificates) {
        result.status = AatlStatus::NotATrustList;
        result.detail = "only " + std::to_string(result.certificates.size()) +
                        " certificates found: the file does not look like the Adobe trust list";
        spdlog::info("aatl: {}", result.detail);
        return result;
    }
    result.status = AatlStatus::Ok;
    spdlog::info("aatl: {} certificates ({} identities, {} skipped), signed by {}",
                 result.certificates.size(), result.identities, result.skipped,
                 result.signer.empty() ? "nobody" : result.signer);
    return result;
}

AatlResult parseAatlFile(const std::filesystem::path& file) { return parseAatl(paths::readFile(file)); }

}  // namespace ot::sign
