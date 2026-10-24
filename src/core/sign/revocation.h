// Отзыв сертификатов (эталон 3.8 «Валидатор»: OCSP, затем CRL; данные документа (DSS, CMS) — в первую
// очередь, затем сеть, если разрешена; план 4.1, п. 5; sign-33 — ядро). OCSP: CertID по алгоритму ответа,
// подпись респондера — OCSP_basic_verify (издатель или делегированный с EKU OCSPSigning), статус
// OCSP_single_get0_status, своё окно действия на момент проверки; сетевой запрос — POST
// application/ocsp-request с nonce по AIA. CRL: имя и подпись издателя, окно действия, X509_CRL_get0_by_cert,
// причина по RFC 5280 5.3.1; по CRL DP. Отзыв после момента проверки — ещё не отозван («отозван после подписи
// — действительна»). retroactive — данные отзыва действуют сколь угодно назад (Acrobat; pyHanko
// --retroactive-revinfo). Нет данных или сеть выключена — Unknown, не Revoked.
#pragma once

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "core/sign/chain.h"
#include "core/sign/ossl.h"
#include "core/util/http_fetcher.h"

namespace ot::sign {

enum class RevocationStatus { Good, Revoked, Unknown };
enum class RevocationSource { None, Ocsp, Crl };

// CRLReason (RFC 5280 5.3.1); None — причина не указана в данных.
enum class RevocationReason {
    None = -1,
    Unspecified = 0,
    KeyCompromise = 1,
    CaCompromise = 2,
    AffiliationChanged = 3,
    Superseded = 4,
    CessationOfOperation = 5,
    CertificateHold = 6,
    RemoveFromCrl = 8,
    PrivilegeWithdrawn = 9,
    AaCompromise = 10,
};

const char* revocationStatusName(RevocationStatus status);
const char* revocationReasonName(RevocationReason reason);  // "keyCompromise"…; None — ""

struct RevocationResult {
    RevocationStatus status = RevocationStatus::Unknown;
    RevocationSource source = RevocationSource::None;
    RevocationReason reason = RevocationReason::None;
    std::string revocationTime;  // ISO; заполнено и когда отозван позже момента проверки
    std::string thisUpdate;      // использованного ответа или CRL
    std::string nextUpdate;
    std::string responder;  // CN подписанта OCSP-ответа
    std::string detail;     // почему Unknown, откуда данные (английский — журнал и «Подробнее»)
    Bytes ocspResponse;     // DER OCSPResponse, по которому решено (для DSS — 4.5)
    Bytes crl;              // DER CRL, по которой решено
};

// Худший статус набора: Revoked > Unknown > Good (пусто — Good).
RevocationStatus worstStatus(const std::vector<RevocationResult>& results);

// Данные отзыва, известные без сети: DSS документа, adbe-revocationInfoArchival в CMS, кеш.
struct RevocationData {
    std::vector<Bytes> ocspResponses;  // DER OCSPResponse (или BasicOCSPResponse)
    std::vector<Bytes> crls;           // DER или PEM
};

class RevocationChecker {
public:
    // fetcher — сеть (AIA OCSP, CRL DP); nullptr — только известные данные.
    RevocationChecker(RevocationData known, net::HttpFetcher* fetcher);

    void setOcsp(bool on) { ocsp_ = on; }
    void setCrl(bool on) { crl_ = on; }
    void setRetroactive(bool on) { retroactive_ = on; }
    void setTime(std::time_t t) { time_ = t; }  // момент проверки: время метки или /M; по умолчанию — сейчас
    void setLeewaySeconds(int seconds) { leeway_ = seconds; }  // допуск часов, 300 с

    // Статус cert, выпущенного issuer (issuer — для CertID, подписи CRL и проверки респондера).
    RevocationResult check(X509* cert, X509* issuer);
    // Все сертификаты цепочки, кроме якоря (последнего): [конечный, промежуточные…].
    std::vector<RevocationResult> checkChain(const ChainResult& chain);

private:
    struct Verdict;
    void judgeOcsp(const Bytes& der, X509* cert, X509* issuer, OCSP_REQUEST* sent, Verdict& v) const;
    void judgeCrl(const Bytes& der, X509* cert, X509* issuer, Verdict& v) const;
    std::time_t when() const;

    RevocationData known_;
    net::HttpFetcher* fetcher_;
    bool ocsp_ = true;
    bool crl_ = true;
    bool retroactive_ = false;
    std::optional<std::time_t> time_;
    int leeway_ = 300;
};

}  // namespace ot::sign
