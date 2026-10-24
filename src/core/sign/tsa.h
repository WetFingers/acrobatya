// Метки времени RFC 3161 (эталон 3.8 «Механика», 7.4; план 4.1, п. 4; sign-19 — ядро): запрос TimeStampReq
// (imprint, nonce 64 бит, certReq), POST application/timestamp-query через net::HttpFetcher (единая точка
// сети), разбор TimeStampResp и проверка токена TS_RESP_verify_response: подпись, версия, imprint, nonce,
// подписант (EKU timeStamping, ESS, имя TSA). Якоря TSA — store; без него проверяется только согласованность
// (подпись по сертификату из токена, trusted = false): у публичных TSA корни не наши, доверие решает
// валидатор (4.4). B-T: imprint — хэш значения подписи CMS (RFC 3161 прил. A); DocTimeStamp — хэш /ByteRange.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "core/sign/cms.h"
#include "core/sign/ossl.h"
#include "core/sign/x509_info.h"
#include "core/util/http_fetcher.h"

namespace ot::sign {

enum class TsaStatus {
    Ok,
    NetworkDisabled,  // выключатель «Сеть»
    NetworkError,     // нет соединения, неверный адрес, перенаправление на другой хост, слишком большой ответ
    Timeout,
    HttpError,    // сервер ответил не 2xx
    BadResponse,  // не TimeStampResp или не тот Content-Type
    Rejected,     // PKIStatus rejection/waiting/revocation…
    VerifyFailed  // подпись, imprint, nonce, подписант
};

class TsaError : public std::runtime_error {
public:
    TsaError(TsaStatus status, const std::string& message, int httpCode = 0)
        : std::runtime_error(message), status_(status), httpCode_(httpCode) {}
    TsaStatus status() const noexcept { return status_; }
    int httpCode() const noexcept { return httpCode_; }

private:
    TsaStatus status_;
    int httpCode_;
};

struct TsaRequest {
    Bytes der;  // TimeStampReq
    Bytes imprint;
    DigestAlgorithm digest = DigestAlgorithm::Sha256;
    Bytes nonce;  // big-endian, положительное
    std::string policy;
    bool certReq = true;
};

// Запрос на метку для готового хэша (длина — как у digest, иначе std::invalid_argument).
TsaRequest makeTsaRequest(std::span<const std::uint8_t> imprint,
                          DigestAlgorithm digest = DigestAlgorithm::Sha256, std::string_view policy = {},
                          bool certReq = true);

struct TimestampInfo {
    std::string genTime;        // «2026-10-24T09:27:43Z»
    std::string serial;         // HEX
    std::string policy;         // OID
    std::string hashAlgorithm;  // OID imprint
    Bytes imprint;
    Bytes nonce;
    std::string tsaName;         // поле tsa (DirName строкой), пусто — нет
    double accuracySeconds = 0;  // 0 — не указана
    CertSummary signer;          // сертификат TSA из токена (пусто — в токене нет)
    bool trusted = false;        // цепочка подписанта до store проверена
};

// Сведения о токене (ContentInfo SignedData с TSTInfo) без проверки; не токен — std::runtime_error.
TimestampInfo parseTimestampToken(std::span<const std::uint8_t> token);

// Ответ TSA на запрос: статус granted/grantedWithMods, токен, подпись, версия, imprint, nonce, подписант.
// store — якоря TSA (цепочка с purpose timestampsign на текущее время), nullptr — только согласованность.
// Ошибка — TsaError (BadResponse, Rejected, VerifyFailed). Возвращает DER токена.
Bytes checkTsaResponse(std::span<const std::uint8_t> response, const TsaRequest& request, X509_STORE* store,
                       TimestampInfo* info = nullptr);

// Готовый токен по хэшу данных (валидатор, DocTimeStamp): подпись, версия, imprint (и алгоритм, если задан),
// подписант; цепочка — на время genTime. store == nullptr — согласованность. false — причина в error.
bool verifyTimestampToken(std::span<const std::uint8_t> token, std::span<const std::uint8_t> imprint,
                          X509_STORE* store, std::string* error, TimestampInfo* info = nullptr);

struct TsaConfig {
    std::string url;   // http(s)://…
    std::string user;  // Basic-аутентификация (sign-19: «при необходимости логин/пароль»)
    std::string password;
    DigestAlgorithm digest = DigestAlgorithm::Sha256;
    std::string policy;  // reqPolicy, пусто — политика TSA по умолчанию
    int timeoutMs = 15000;
};

class TsaClient {
public:
    TsaClient(net::HttpFetcher& fetcher, TsaConfig config);
    ~TsaClient();
    TsaClient(const TsaClient&) = delete;
    TsaClient& operator=(const TsaClient&) = delete;

    // Якоря TSA (up_ref); nullptr — без проверки доверия.
    void setTrustStore(X509_STORE* store);
    // Метка для готового хэша: DER токена. Ошибка — TsaError.
    Bytes timestamp(std::span<const std::uint8_t> imprint);
    // Сведения о последней метке.
    const TimestampInfo& lastInfo() const { return last_; }
    const TsaConfig& config() const { return config_; }

private:
    net::HttpFetcher& fetcher_;
    TsaConfig config_;
    Ossl<X509_STORE> store_;
    TimestampInfo last_;
};

}  // namespace ot::sign
