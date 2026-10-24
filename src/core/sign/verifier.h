// Проверка подписей документа — валидатор (эталон 3.8 «Валидатор», «Граничные случаи», 4.5; план 4.4; sign-25
// — ядро, sign-27 — данные для свойств подписи). Для каждого подписанного поля /Sig (обход /AcroForm
// /Fields):
//
// (1) структура — /ByteRange из 4 чисел от 0, отрезки по возрастанию в пределах файла, «дыра» между ними —
//     ровно шестнадцатеричная строка /Contents (иначе «неверный ByteRange»); конец диапазона раньше конца
//     файла — «есть изменения после подписи» (их разбирает анализ ревизий, changes.h);
// (2) целостность — CMS SignerInfo по шагам CMS_verify без CMS_CADES (с ним флаги NO_* игнорируются):
//     сертификат подписанта (CMS, затем /DSS /Certs), хэш данных /ByteRange потоком против message-digest
//     (CMS_SignerInfo_verify_content), подпись над signed-атрибутами (CMS_SignerInfo_verify) или — без
//     атрибутов — над хэшем данных; ESS signing-certificate(-v2) сверяется с сертификатом подписанта;
//     /SubFilter: ETSI.CAdES.detached (и PBAD.PAdES ICP-Brasil — тот же detached CMS), adbe.pkcs7.detached,
//     adbe.pkcs7.sha1 (eContent — SHA-1 данных; «устаревший алгоритм»), ETSI.RFC3161 (метка документа:
//     imprint — хэш /ByteRange); adbe.x509.rsa_sha1 и прочие — «не поддерживается»; MD5/MD2 — отказ, SHA-1 —
//     предупреждение; CMS без ESS, RSA-PSS, ECDSA — читаются; «обёрнутая» CMS (eContent при detached) —
//     данные подписи обязаны совпасть с /ByteRange.
//
// (3) доверие — ChainBuilder до якорей TrustStore (подписи, у сертифицирующей — «сертифицированные
// документы»;
//     доверенный промежуточный — якорь), промежуточные — из CMS и /DSS, назначение — подпись документов;
//     время проверки — метка подписи, если она есть и проверена (п. 3), иначе /M «заявленное» (иначе —
//     сейчас); просроченный к этому моменту сертификат — не доверен;
// (4) отзыв (только у доверенной цепочки) — сначала встроенные данные (/DSS /OCSPs /CRLs,
//     adbe-revocationInfoArchival), их хватило на всю цепочку — «LTV включено»; иначе сеть по политике
//     (VerifyOptions::revocation: обязательно / по возможности / никогда); отозван до момента проверки —
//     «недействительна», после — нет; нет данных — «Неизвестно (нет данных об отзыве)»: при «обязательно» —
//     вердикт Unknown, при «по возможности» — предупреждение; retroactive — данные отзыва годятся задним
//     числом (как Acrobat; pyHanko --retroactive-revinfo).
// (5) метки времени — метка подписи (unsigned id-aa-timeStampToken: imprint — хэш значения подписи) и метка
//     документа (ETSI.RFC3161: imprint — хэш /ByteRange): токен цел, служба времени доверена на момент метки
//     (якоря «подписи») — «secure time», время проверки подписи — время метки; служба не доверена или метка
//     испорчена — время «заявленное»; сертификат службы истёк к «сейчас» — «просроченная метка», принимается
//     по VerifyOptions::acceptExpiredTimestamps; /M позже метки — предупреждение. Уровень PAdES — по
//     структуре: B; T — метка подписи или метка документа после неё; LT — T и непустой /DSS, изменённый после
//     подписи; LTA — LT и метка документа не раньше ревизии /DSS (то же правило — tools/compare_pyhanko.py).
// (6) изменения после подписи — анализ ревизий (changes.h) по DocMDP документа и FieldMDP подписи: только
// /DSS и
//     метки документа — не изменения; разрешённые — «внесены разрешённые изменения» (ValidWithChanges);
//     запрещённые и правка заблокированного поля — «недействительна»; анализ не удался — «Неизвестно».
//
// Вердикт — как у Acrobat (Verdict); причины — Finding с кодом Issue и английской подробностью (журнал,
// «Подробнее»); сводка документа — полоса сообщений (DocumentStatus). Объекты OpenSSL наружу не выходят.
// Байты подписи читаются из потока документа (файл или память). Поток — поток документа.
#pragma once

#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/sign/changes.h"
#include "core/sign/docmdp.h"
#include "core/sign/ossl.h"
#include "core/sign/revocation.h"
#include "core/sign/trust_store.h"
#include "core/sign/tsa.h"
#include "core/sign/x509_info.h"
#include "core/util/geometry.h"
#include "core/util/http_fetcher.h"

namespace ot {
class Document;
}

namespace ot::sign {

enum class SignatureKind {
    Approval,       // обычная (утверждающая) подпись
    Certification,  // сертифицирующая: /Perms /DocMDP указывает на неё (синяя лента)
    DocTimestamp,   // метка времени документа (/SubFilter /ETSI.RFC3161)
};
const char* signatureKindName(SignatureKind kind);

// Статус подписи — как в панели «Подписи» Acrobat (по убыванию тяжести снизу вверх).
enum class Verdict {
    Valid,             // зелёная галочка
    ValidWithChanges,  // зелёная галочка: после подписи внесены разрешённые изменения
    ValidUntrusted,    // жёлтый треугольник: подпись цела, подписант не доверен
    Unknown,           // «Проблемы»: отзыв неизвестен, метка не проверена, формат не поддерживается
    Invalid,           // красный крест: документ изменён или подпись повреждена
};
const char* verdictName(Verdict verdict);

// Причины. Тяжесть — issueSeverity: Invalid, Unknown, ValidUntrusted или nullopt (предупреждение).
enum class Issue {
    // структура
    NoByteRange,   // нет /ByteRange
    BadByteRange,  // не 4 числа от 0, отрезки вне файла, «дыра» — не /Contents
    NoContents,    // нет /Contents или пусто
    // целостность
    CmsDamaged,            // /Contents — не CMS SignedData (или не токен метки)
    UnsupportedSubFilter,  // adbe.x509.rsa_sha1 и неизвестные
    UnsupportedAlgorithm,  // алгоритм подписи или хэша неизвестен OpenSSL
    NoSignerCertificate,   // сертификата подписанта нет ни в CMS, ни в /DSS
    NoMessageDigest,       // signed-атрибуты без message-digest
    DigestMismatch,        // хэш /ByteRange не совпал — документ изменён
    SignatureMismatch,     // подпись не сходится с сертификатом
    EssMismatch,           // ESS signing-certificate не о сертификате подписанта
    ForbiddenAlgorithm,    // MD5, MD2 — отказ
    WeakAlgorithm,         // SHA-1 — предупреждение «устаревший алгоритм»
    // изменения после подписи
    ModifiedAfterSigning,  // после подписи дописаны секции (не разобраны)
    // доверие и отзыв
    Untrusted,            // нет пути до якоря
    CertExpired,          // сертификат цепочки истёк к моменту проверки
    CertNotYetValid,      // ещё не действовал к моменту проверки
    BadKeyUsage,          // сертификат не для подписи документов (KU/EKU)
    ChainBroken,          // подпись сертификата цепочки, ограничения ЦС
    Revoked,              // отозван до момента проверки
    RevocationUnknown,    // нет данных об отзыве при политике «обязательно»
    RevocationUnchecked,  // отзыв не проверен (нет данных при «по возможности», политика «никогда»)
    // метки времени
    TimestampInvalid,           // метка подписи испорчена (imprint, подпись токена)
    TimestampUntrusted,         // служба времени не доверена — время «заявленное»
    TimestampExpired,           // сертификат службы времени истёк к «сейчас»
    ClaimedTimeAfterTimestamp,  // /M позже времени метки
    // изменения после подписи (разобраны)
    AllowedChanges,      // разрешённые изменения (формы, подписи, комментарии по правам)
    DisallowedChanges,   // изменения вне разрешённых
    LockedFieldChanged,  // изменено поле, заблокированное подписью (FieldMDP)
};
const char* issueName(Issue issue);
std::optional<Verdict> issueSeverity(Issue issue);

// Откуда момент проверки.
enum class TimeSource {
    Now,        // ни метки, ни /M
    Claimed,    // /M — время с часов подписавшего
    Timestamp,  // метка времени подписи (служба времени)
};
const char* timeSourceName(TimeSource source);

// Уровень PAdES (ETSI EN 319 142-1) по структуре; у метки документа — None.
enum class PadesLevel { None, B, T, LT, LTA };
const char* padesLevelName(PadesLevel level);  // "", "B", "T", "LT", "LTA"

// Политика проверки отзыва (Установки → Подписи → Проверка).
enum class RevocationPolicy {
    Required,    // «обязательно»: нет данных — подпись «Неизвестно»
    BestEffort,  // «по возможности» (по умолчанию): нет данных — предупреждение
    Never,       // «никогда»: не проверять
};

struct Finding {
    Issue issue;
    std::string detail;  // английский — журнал и «Подробнее»
};

// Сила алгоритма хэша или подписи по OID.
enum class AlgorithmStrength { Ok, Weak, Forbidden };
AlgorithmStrength algorithmStrength(std::string_view oid);

struct SignatureInfo {
    int index = 0;      // по порядку обхода /AcroForm /Fields
    std::string field;  // полное имя поля (UTF-8)
    int page = -1;      // страница виджета (−1 — не найдена)
    RectF rect;         // рамка виджета в координатах страницы MuPDF
    bool visible = false;
    SignatureKind kind = SignatureKind::Approval;

    // Словарь /V.
    std::string type;  // /Type: Sig, DocTimeStamp или пусто
    std::string filter;
    std::string subFilter;
    std::string name;  // /Name
    std::string reason;
    std::string location;
    std::string contact;
    std::string claimedTime;  // /M — «заявленное» время подписи (ISO, UTC), пусто — нет
    std::vector<ByteRange> byteRange;
    std::int64_t rangeEnd = 0;  // конец подписанной ревизии
    std::int64_t fileSize = 0;
    bool coversWholeFile = false;
    int revision = 0;              // номер подписанной ревизии файла: 1 — исходная запись
    std::size_t contentsSize = 0;  // DER подписи без дополнения нулями

    // CMS (у метки документа — токен).
    std::string digestAlgorithm;     // OID
    std::string signatureAlgorithm;  // OID
    std::optional<CertSummary> signer;
    std::vector<CertSummary> certificates;  // CMS в порядке файла
    bool signedAttributes = false;
    bool ess = false;  // есть ESS signing-certificate(-v2)

    // Доверие и время.
    bool trusted = false;
    std::vector<CertSummary> chain;  // построенная цепочка: подписант → якорь (и при ошибке — что построено)
    std::string chainError;          // английский текст OpenSSL
    std::string validationTime;      // момент проверки (ISO, UTC)
    TimeSource timeSource = TimeSource::Now;

    // Отзыв.
    RevocationStatus revocation = RevocationStatus::Unknown;  // худший по цепочке
    bool revocationChecked = false;                           // проверка выполнялась
    std::vector<RevocationResult> revocationDetails;          // по сертификатам цепочки, кроме якоря
    bool ltv = false;  // «LTV включено»: цепочка доверена, отзыв всей цепочки — по данным файла

    // Метка времени: подписи (B-T) или сама метка документа.
    std::optional<TimestampInfo> timestamp;
    bool timestampIntact = false;   // токен цел, imprint сходится
    bool timestampTrusted = false;  // служба времени доверена на момент метки
    PadesLevel level = PadesLevel::None;

    // Права и изменения после подписи.
    MdpPermission docMdp = MdpPermission::None;  // DocMDP документа (у сертифицирующей — её /P)
    std::vector<FieldLock> locks;                // FieldMDP подписи
    ChangeReport changes;                        // пусто — подпись покрывает файл

    // Итог.
    bool structureOk = false;  // (1)
    bool intact = false;       // (1) и (2): данные не изменены, подпись сходится
    Verdict verdict = Verdict::Unknown;
    std::vector<Finding> findings;

    bool has(Issue issue) const;
};

// Сводка документа — полоса сообщений: синяя «Подписано, все подписи действительны» (у сертифицированного —
// «Сертифицировано»), жёлтая «Проблемы с подписью», красная «Подпись недействительна».
enum class DocumentStatus { NotSigned, Valid, Certified, Problems, Invalid };
const char* documentStatusName(DocumentStatus status);

struct DocumentVerification {
    std::vector<SignatureInfo> signatures;
    DocumentStatus status = DocumentStatus::NotSigned;
    int versions = 0;  // ревизий файла
    std::int64_t fileSize = 0;
    // Все сертификаты файла без повторов: CMS подписей и меток, /DSS /Certs («Доверять» в свойствах).
    std::vector<CertSummary> embeddedCertificates;
    MdpPermission docMdp = MdpPermission::None;  // документ сертифицирован с этими правами
};

// Сводка по вердиктам подписей: Invalid — хоть одна недействительна; Problems — хоть одна Unknown или
// ValidUntrusted; иначе Certified (есть сертифицирующая) или Valid.
DocumentStatus documentStatus(const std::vector<SignatureInfo>& signatures);
// Худший вердикт по находкам (без находок — Valid).
Verdict verdictFromFindings(const std::vector<Finding>& findings);

struct VerifyOptions {
    const TrustStore* trust = nullptr;    // якоря; nullptr — нет (все подписанты не доверены)
    net::HttpFetcher* fetcher = nullptr;  // OCSP, CRL, AIA; nullptr — без сети
    RevocationPolicy revocation = RevocationPolicy::BestEffort;
    bool retroactiveRevocation = true;  // данные отзыва годятся задним числом (Acrobat)
    bool acceptExpiredTimestamps =
        true;                        // «просроченная метка» — время всё равно метки (Acrobat по умолчанию)
    std::optional<std::time_t> now;  // «сейчас» (тесты); nullopt — часы
};

// Права сертифицированного документа (sign-30 «Certified documents»: Dynamic content, Embedded high privilege
// JavaScript): сертифицирующая подпись цела и доверена якорем с флагом «сертифицированные документы» —
// trusted; динамический контент и JavaScript — по флагам того же якоря (верх построенной цепочки).
struct CertifiedPrivileges {
    bool certified = false;  // есть сертифицирующая подпись
    bool trusted = false;    // она действительна и доверена для сертификации
    bool dynamicContent = false;
    bool javaScript = false;
    std::string anchor;  // SHA-256 якоря (HEX)
};
CertifiedPrivileges certifiedPrivileges(const DocumentVerification& verification, const TrustStore& store);

// Проверить все подписанные поля документа. Не PDF — пустой результат (NotSigned).
DocumentVerification verifyDocument(Document& doc, const VerifyOptions& options = {});

}  // namespace ot::sign
