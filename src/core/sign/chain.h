// Цепочка сертификатов (эталон 3.8 «Валидатор» — доверие; план 4.1, п. 5; sign-33 — ядро): X509_STORE с
// якорями (своё хранилище доверия, EUTL/AATL — 4.5), промежуточные из CMS и DSS, недостающие — по AIA
// caIssuers через net::HttpFetcher; момент проверки — время метки или /M (X509_VERIFY_PARAM_set_time);
// X509_V_FLAG_PARTIAL_CHAIN — доверенный промежуточный становится якорем; назначение — подпись документа или
// метка времени; CRL внутри цепочки (X509_V_FLAG_CRL_CHECK[_ALL]) — по желанию, основной отзыв —
// RevocationChecker (OCSP, затем CRL). Итог — вид по RFC 5280 6.1 (коды X509_V_ERR_* сведены к тому, что
// показывает интерфейс).
#pragma once

#include <ctime>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/x509_info.h"
#include "core/util/http_fetcher.h"

namespace ot::sign {

enum class ChainStatus {
    Valid,              // путь до доверенного якоря построен и проверен
    Untrusted,          // нет пути до якоря: издатель не найден, самоподписанный не из якорей
    Expired,            // сертификат цепочки истёк на момент проверки
    NotYetValid,        // ещё не действует на момент проверки
    Revoked,            // отозван (CRL в цепочке)
    RevocationUnknown,  // проверка CRL в цепочке: нет CRL, устарела или ещё не действует
    BadSignature,       // подпись сертификата не сходится
    InvalidPurpose,     // keyUsage / extendedKeyUsage не для этого назначения
    InvalidCa,          // издатель не ЦС, превышен pathLen, ограничения имён и политик
    Other,
};
const char* chainStatusName(ChainStatus status);

enum class ChainPurpose {
    Any,              // назначение не проверяется
    DocumentSigning,  // KU digitalSignature или nonRepudiation; EKU (если есть) — emailProtection,
                      // documentSigning (RFC 9336), Adobe Authentic Documents, Microsoft Document Signing или
                      // anyExtendedKeyUsage
    Timestamping,  // служба времени: единственный критический EKU timeStamping (RFC 3161 2.3)
};

enum class CrlCheck { None, Leaf, All };

struct ChainResult {
    ChainStatus status = ChainStatus::Other;
    int opensslError = 0;            // X509_V_ERR_* (0 — нет)
    std::string error;               // текст OpenSSL (английский, для журнала и «Подробнее»)
    int errorDepth = -1;             // на каком сертификате: 0 — конечный
    std::vector<CertSummary> chain;  // от конечного к якорю — то, что построено (и при ошибке)
    Ossl<STACK_OF(X509)> certs;      // те же сертификаты (для RevocationChecker)
    std::vector<Bytes> fetched;      // DER сертификатов, скачанных по AIA (в DSS — 4.5)

    bool trusted() const { return status == ChainStatus::Valid; }
};

class ChainBuilder {
public:
    // trustStore — якоря (сертификаты копируются в своё хранилище: объект вызывающего не меняется), nullptr —
    // якорей нет. time — момент проверки, nullopt — сейчас. partialChain — промежуточный из якорей — тоже
    // якорь.
    explicit ChainBuilder(X509_STORE* trustStore, std::optional<std::time_t> time = std::nullopt,
                          bool partialChain = false);
    ~ChainBuilder();
    ChainBuilder(const ChainBuilder&) = delete;
    ChainBuilder& operator=(const ChainBuilder&) = delete;

    void addUntrusted(X509* cert);  // промежуточные (up_ref)
    void addCrl(X509_CRL* crl);     // для CrlCheck (up_ref)
    void setCrlCheck(CrlCheck check) { crlCheck_ = check; }
    void setPurpose(ChainPurpose purpose) { purpose_ = purpose; }
    void setFetcher(net::HttpFetcher* fetcher) { fetcher_ = fetcher; }  // AIA caIssuers; nullptr — без сети

    ChainResult build(X509* leaf);

private:
    bool fetchIssuer(X509* cert, ChainResult& result);

    Ossl<X509_STORE> store_;
    Ossl<STACK_OF(X509)> untrusted_;
    Ossl<STACK_OF(X509_CRL)> crls_;
    std::optional<std::time_t> time_;
    bool partialChain_ = false;
    CrlCheck crlCheck_ = CrlCheck::None;
    ChainPurpose purpose_ = ChainPurpose::Any;
    net::HttpFetcher* fetcher_ = nullptr;
    std::set<std::string> triedUrls_;
};

// Можно ли сертификатом подписывать документы (правило ChainPurpose::DocumentSigning).
bool allowedForDocumentSigning(X509* cert);

// Адреса из расширений: OCSP и caIssuers (authorityInfoAccess), CRL (crlDistributionPoints) — только URI.
std::vector<std::string> ocspUrls(X509* cert);
std::vector<std::string> caIssuersUrls(X509* cert);
std::vector<std::string> crlUrls(X509* cert);

}  // namespace ot::sign
