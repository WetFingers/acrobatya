// Сведения для долгосрочной проверки — LTV (эталон 3.8 «LTV и B-LTA», «Диалог подписания»; вопрос 12.3; план
// 4.5, п. 1; sign-21). «Добавить сведения для проверки»: для каждой подписи и метки времени документа —
// цепочка подписанта до якоря хранилища доверия или самоподписанного корня (якорь включается), цепочка службы
// времени метки подписи, сертификаты респондеров OCSP; отзыв каждого сертификата цепочек, кроме корня, —
// через RevocationChecker: сначала данные файла (/DSS, adbe-revocationInfoArchival), затем сеть (fetcher;
// nullptr — без сети), задним числом (retroactive), на момент метки или /M. Респондер OCSP без
// id-pkix-ocsp-nocheck — его отзыв тоже.
//
// Запись — словарь /DSS каталога (ETSI EN 319 142-1, 5.4.2): /Certs — DER сертификатов, /OCSPs — DER
// OCSPResponse (BasicOCSPResponse оборачивается в successful), /CRLs — DER CRL, потоки без повторов по DER
// (прежние элементы /DSS остаются); /VRI по хэшу подписи — выкл. по умолчанию, опция для совместимости (ключ
// — SHA-1 HEX DER подписи без дополнения нулями, как pyHanko; /Cert, /OCSP, /CRL — ссылки на те же потоки).
// Секция — отдельная инкрементальная (одна операция журнала: /DSS в каталоге, поля не трогаются →
// Saver::Incremental); DocMDP не нарушается (/DSS — разрешено и при P=1). Нового нет — запись не делается.
//
// «LTV включено» — весь материал для цепочек подписанта и службы времени есть в документе (CMS, /DSS): та же
// сборка без сети и все цепочки и отзыв найдены. Поток — поток документа.
#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/doc/saver.h"
#include "core/sign/ossl.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/http_fetcher.h"

namespace ot {
class Document;
}

namespace ot::sign {

// Материал проверки — DER без повторов.
struct LtvMaterial {
    std::vector<Bytes> certs;
    std::vector<Bytes> ocsps;  // OCSPResponse
    std::vector<Bytes> crls;

    bool empty() const noexcept { return certs.empty() && ocsps.empty() && crls.empty(); }
    std::size_t bytes() const noexcept;
    // Добавить без повторов; число новых.
    int merge(const LtvMaterial& other);
    // То, чего нет в other.
    LtvMaterial minus(const LtvMaterial& other) const;
};

struct LtvOptions {
    const TrustStore* trust =
        nullptr;  // якоря цепочек (подписи и сертификация); nullptr — до самоподписанного
    net::HttpFetcher* fetcher = nullptr;  // OCSP, CRL, AIA caIssuers; nullptr — только данные файла
    bool vri = false;                     // /VRI по хэшу подписи (совместимость)
    std::optional<std::time_t> now;       // «сейчас» (тесты); nullopt — часы
};

// Материал одной подписи (или метки времени документа).
struct LtvSignature {
    std::string field;
    SignatureKind kind = SignatureKind::Approval;
    std::string vriKey;    // SHA-1 HEX DER подписи
    LtvMaterial material;  // цепочки, отзыв, сертификаты респондеров
    bool chainComplete =
        false;  // цепочки подписанта и службы времени дошли до якоря или самоподписанного корня
    bool revocationComplete = false;   // отзыв каждого сертификата цепочек (кроме корня) найден
    std::vector<std::string> missing;  // чего нет (английский — журнал и «Подробнее»)

    bool complete() const noexcept { return chainComplete && revocationComplete; }
};

struct LtvCollection {
    std::vector<LtvSignature> signatures;
    LtvMaterial material;  // всё собранное без повторов
    LtvMaterial existing;  // уже в /DSS
    LtvMaterial added() const { return material.minus(existing); }
};

// Собрать материал для всех подписей документа (сеть — options.fetcher).
LtvCollection collectLtv(Document& doc, const LtvOptions& options);

struct DssWriteResult {
    bool written = false;  // /DSS изменён
    int certs = 0;         // новых потоков
    int ocsps = 0;
    int crls = 0;
    int vri = 0;  // записей /VRI
};

// Записать /DSS одной операцией журнала («Add validation info»), без записи файла.
DssWriteResult writeDss(Document& doc, const LtvCollection& collected, bool vri);

struct LtvResult {
    LtvCollection collected;
    DssWriteResult dss;
    SaveReport save;  // запись (dss.written = false — не было)
};

// «Добавить сведения для проверки»: несохранённое — сначала обычная запись; собрать, записать /DSS отдельной
// инкрементальной секцией в target (пусто — файл документа). Не PDF, нет подписей — пустой результат; ошибка
// записи — в save.
LtvResult addValidationInfo(Document& doc, const LtvOptions& options,
                            const std::filesystem::path& target = {});

// Состояние «LTV включено» всех подписей — сборка без сети (fetcher не используется).
std::vector<LtvSignature> ltvStatus(Document& doc, const LtvOptions& options = {});
// «LTV включено» для подписи проверки (по имени поля).
bool ltvEnabled(Document& doc, const SignatureInfo& sig, const LtvOptions& options = {});

// DER OCSPResponse: OCSPResponse — как есть, BasicOCSPResponse — обёрнут в successful; не OCSP — пусто.
Bytes ocspResponseDer(std::span<const std::uint8_t> der);

// «Автоматически добавлять сведения LTV при сохранении» (Установки → Подписи; вопрос 12.3).
enum class LtvAutoAdd { Never, Ask, Always };
LtvAutoAdd ltvAutoAddFromName(std::string_view name);  // "never" | "ask" | "always"; прочее — Ask
const char* ltvAutoAddName(LtvAutoAdd mode);
enum class LtvDecision { Skip, Ask, Add };
// Ask: спросить, если материал больше 10 % размера файла + 10 КБ, иначе добавить молча; материала нет —
// Skip.
LtvDecision decideAutoLtv(LtvAutoAdd mode, std::uintmax_t fileSize, std::size_t materialBytes);

}  // namespace ot::sign
