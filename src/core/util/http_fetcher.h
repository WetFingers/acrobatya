// Единая точка сети программы (эталон 7.4; security-36; план 4.1, п. 4): метки времени (TSA), OCSP и CRL,
// сертификаты по AIA, списки доверия EUTL/AATL, tessdata, веб-страница → PDF, SubmitForm — только через
// HttpFetcher. Здесь — интерфейс без Qt (им пользуются core/sign: TsaClient, RevocationChecker) и
// FakeHttpFetcher для тестов (ответы по адресу или обработчик, запись запросов). Настоящий —
// services/http_fetcher.h (QNetworkAccessManager над Schannel, выключатель «Сеть: разрешена / только подписи
// / выключена», журнал URL без тела). Вызов синхронный: зовут из рабочего потока, не из потока интерфейса.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace ot::net {

// Для выключателя «Сеть»: подписи (TSA, OCSP, CRL, AIA, списки доверия) или всё остальное.
enum class Purpose { Signatures, General };

struct Request {
    std::string method = "GET";  // "GET" | "POST"
    std::string url;             // http(s)://…
    std::string contentType;     // тело POST: "application/timestamp-query", "application/ocsp-request"…
    std::vector<std::uint8_t> body;
    std::vector<std::pair<std::string, std::string>> headers;  // Accept, Authorization…
    int timeoutMs = 15000;                                     // эталон 7.4: 15 с
    bool sameHostRedirectsOnly = true;                         // TSA/OCSP: на другой хост не идти
    std::size_t maxResponseBytes = 8u << 20;
    Purpose purpose = Purpose::Signatures;
};

enum class Status {
    Ok,               // ответ получен (код HTTP — в httpCode, 2xx — ok())
    Disabled,         // сеть выключена настройкой — запроса не было
    BadUrl,           // не http(s) или испорченный адрес
    Timeout,          // нет ответа за timeoutMs
    NetworkError,     // DNS, соединение, TLS
    HttpError,        // сервер ответил не 2xx (тело — в body)
    RedirectRefused,  // перенаправление на другой хост (sameHostRedirectsOnly)
    TooLarge,         // ответ больше maxResponseBytes
};

// Имя статуса для журнала и сообщений об ошибке: "ok", "disabled", "timeout"…
const char* statusName(Status status);

struct Response {
    Status status = Status::NetworkError;
    int httpCode = 0;
    std::string contentType;  // без параметров, в нижнем регистре: "application/timestamp-reply"
    std::vector<std::uint8_t> body;
    std::string error;     // английский текст для журнала
    std::string finalUrl;  // после перенаправлений (или куда отказались идти)

    bool ok() const { return status == Status::Ok && httpCode >= 200 && httpCode < 300; }
};

class HttpFetcher {
public:
    virtual ~HttpFetcher() = default;
    // Выполнить запрос и дождаться ответа (или таймаута). Исключений не бросает.
    virtual Response fetch(const Request& request) = 0;
};

// Фейк для тестов: ответ по точному адресу (add) или обработчик (setHandler); нет ни того ни другого —
// NetworkError «no fake response». Запросы записываются (requests() — копия; потокобезопасно).
class FakeHttpFetcher : public HttpFetcher {
public:
    using Handler = std::function<Response(const Request&)>;

    void add(const std::string& url, Response response);
    // 200 с телом и типом.
    void addBody(const std::string& url, std::vector<std::uint8_t> body, std::string contentType);
    void setHandler(Handler handler);

    Response fetch(const Request& request) override;

    std::vector<Request> requests() const;
    std::size_t requestCount() const;
    void clearRequests();

private:
    mutable std::mutex mutex_;
    std::map<std::string, Response> responses_;
    Handler handler_;
    std::vector<Request> requests_;
};

// Контент-тип без параметров и в нижнем регистре ("Application/OCSP-Response; x=y" →
// "application/ocsp-response").
std::string mediaType(const std::string& contentType);

}  // namespace ot::net
