// Доверенные списки ЕС — EUTL (эталон 3.8 «Доверие»; 1.4 sign-31; 7.4; вопрос 12.4 — EUTL включён; план 4.5,
// п. 4). LOTL (ETSI TS 119 612, https://ec.europa.eu/tools/lotl/eu-lotl.xml) указывает на национальные TL
// (OtherTSLPointer: страна, адрес, MIME, сертификаты подписантов TL); в TL — поставщики (TSP) и их службы с
// ServiceTypeIdentifier, текущим статусом и ServiceDigitalIdentity (X.509). Якоря программы — службы CA/QC
// (выдают квалифицированные сертификаты), TSA/QTST и TSA (метки времени), NationalRootCA-QC со статусом
// granted, recognisedatnationallevel, accredited, undersupervision, setbynationallaw; флаги — подписи и
// сертифицированные документы.
//
// Подпись LOTL (XAdES) проверяется по отпечатку: SHA-256 сертификата её подписанта (ds:X509Certificate)
// обязан быть в pinned-списке assets/trust/lotl_pins.json (сертификаты, опубликованные в OJ C и указанные в
// самом LOTL); срок — NextUpdate. Национальный TL принимается, если сертификат его подписи объявлен в
// указателе LOTL для этой страны. Не прошло — список помечен (Unpinned / Expired / SignerNotDeclared),
// применять — только по согласию человека (службы, TrustUpdater). XML разбирают службы (QXmlStreamReader,
// services/trust_lists.h) и отдают модели через TrustListSource — ядро XML не видит.
#pragma once

#include <ctime>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/trust_store.h"

namespace ot::sign {

namespace etsi {
inline constexpr std::string_view kSvcType = "http://uri.etsi.org/TrstSvc/Svctype/";
inline constexpr std::string_view kSvcStatus = "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/";
inline constexpr std::string_view kTslTypeLotl =
    "http://uri.etsi.org/TrstSvc/TrustedList/TSLType/EUlistofthelists";
inline constexpr std::string_view kTslMime = "application/vnd.etsi.tsl+xml";
}  // namespace etsi

struct TrustService {
    std::string provider;  // TSPName (английское или первое)
    std::string name;      // ServiceName
    std::string type;      // ServiceTypeIdentifier (URI)
    std::string status;    // ServiceStatus (URI)
    std::string statusStarting;
    std::vector<Bytes> certificates;  // ServiceDigitalIdentity / X509Certificate
};

struct TrustedList {
    std::string territory;  // SchemeTerritory
    std::string type;       // TSLType
    int sequence = 0;       // TSLSequenceNumber
    std::string issued;     // ListIssueDateTime (ISO)
    std::string nextUpdate;
    std::vector<TrustService> services;
    std::vector<Bytes> signers;  // ds:Signature / ds:X509Certificate
};

struct ListPointer {
    std::string territory;
    std::string type;  // TSLType
    std::string location;
    std::string mimeType;
    std::vector<Bytes> signers;  // сертификаты подписантов указанного списка
};

struct Lotl {
    int sequence = 0;
    std::string issued;
    std::string nextUpdate;
    std::vector<ListPointer> pointers;
    std::vector<Bytes> signers;  // сертификат подписи самого LOTL
};

// Pinned-сертификаты подписантов LOTL (assets/trust/lotl_pins.json).
struct LotlPins {
    std::string url;
    std::string snapshot;
    std::vector<std::string> sha256;  // HEX, верхний регистр

    // Разбор JSON; испорчен — std::runtime_error.
    static LotlPins parse(std::string_view json);
    bool contains(const Bytes& der) const;
};

// Источник списков: службы — сеть и XML; тесты — фикстуры. Ошибка загрузки или разбора — исключение.
class TrustListSource {
public:
    virtual ~TrustListSource() = default;
    virtual Lotl lotl() = 0;
    virtual TrustedList list(const ListPointer& pointer) = 0;
};

// Служба годится в якоря (тип и текущий статус).
bool usableService(const TrustService& service);

enum class ListIssue {
    None,
    Unpinned,           // подписант LOTL не из pinned-списка
    Expired,            // NextUpdate прошёл
    SignerNotDeclared,  // подписант TL не объявлен в указателе LOTL
    Unavailable,        // не загрузился или не разобрался
};
const char* listIssueName(ListIssue issue);

struct TerritoryResult {
    std::string territory;
    std::string location;
    int sequence = 0;
    std::string nextUpdate;
    int certificates = 0;
    ListIssue issue = ListIssue::None;
    std::string detail;  // английский — журнал
};

struct EutlResult {
    int lotlSequence = 0;
    std::string lotlNextUpdate;
    bool pinned = false;       // подписант LOTL из pinned-списка
    bool lotlExpired = false;  // NextUpdate LOTL прошёл
    std::vector<TerritoryResult> territories;
    std::vector<ListedCertificate> certificates;  // без повторов; флаги — подписи и сертификация

    // Можно применять без отдельного согласия: LOTL pinned и свежий, нет отклонённых списков.
    bool trustworthy() const;
};

struct EutlOptions {
    std::vector<std::string> territories;  // пусто — все страны
    std::time_t now = 0;                   // 0 — часы
};

// LOTL → национальные TL → якоря. Ошибка загрузки LOTL — исключение; национального TL —
// ListIssue::Unavailable.
EutlResult buildEutl(TrustListSource& source, const LotlPins& pins, const EutlOptions& options = {});

// Время ISO 8601 («2027-04-07T11:06:46Z») → секунды UTC; не время — 0.
std::time_t isoToTime(std::string_view iso);

}  // namespace ot::sign
