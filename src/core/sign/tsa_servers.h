// Серверы времени (эталон 3.8 «Доверие» — «Серверы времени», «Диалог подписания» — метка времени; план 4.2,
// п. 2; вопрос 12.2): список (имя, адрес, логин), по умолчанию DigiCert, в списке Sectigo и GlobalSign;
// «по умолчанию»; «проверить» — тестовый запрос метки случайного хэша; что делать, если служба недоступна
// (флажок «без метки, если сервер недоступен — спросить») — TimestampFallback. Хранение — JSON (services:
// ключ signatures/tsaServers); пароль в JSON не пишется.
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/sign/tsa.h"
#include "core/util/http_fetcher.h"

namespace ot::sign {

struct TsaServer {
    std::string name;
    std::string url;
    std::string user;      // Basic-аутентификация, пусто — без неё
    std::string password;  // только в памяти
};

// Служба времени не ответила при подписи: спросить (подписать без метки — да/нет), подписать без метки молча,
// отменить подпись.
enum class TimestampFallback { Ask, WithoutTimestamp, Cancel };
TimestampFallback parseFallback(std::string_view text);         // чужое — Ask
const char* fallbackName(TimestampFallback fallback) noexcept;  // "ask" | "withoutTimestamp" | "cancel"

class TsaServers {
public:
    // DigiCert (по умолчанию), Sectigo, GlobalSign — без аутентификации.
    static TsaServers defaults();
    // Испорченный или пустой JSON — defaults().
    static TsaServers fromJson(std::string_view json);
    std::string toJson() const;  // без паролей

    const std::vector<TsaServer>& servers() const noexcept { return servers_; }
    const TsaServer* find(std::string_view name) const;
    // Служба по умолчанию; nullptr — список пуст.
    const TsaServer* defaultServer() const;
    const std::string& defaultName() const noexcept { return default_; }

    // Добавить (пустое имя или адрес, занятое имя — false); первая — по умолчанию.
    bool add(TsaServer server);
    // Заменить службу name (новое имя не должно быть занято другой); false — нет такой или неверно.
    bool update(std::string_view name, TsaServer server);
    // Убрать; по умолчанию — первая оставшаяся.
    bool remove(std::string_view name);
    bool setDefault(std::string_view name);

    static TsaConfig config(const TsaServer& server);

private:
    std::vector<TsaServer> servers_;
    std::string default_;
};

struct TsaCheck {
    bool ok = false;
    TsaStatus status = TsaStatus::Ok;
    std::string message;  // английский текст ошибки для журнала
    int httpCode = 0;
    TimestampInfo info;  // сведения о метке (ok)
    long long ms = 0;    // время ответа
};

// «Проверить»: метка для случайного хэша SHA-256 через fetcher (Purpose::Signatures).
TsaCheck checkTsaServer(net::HttpFetcher& fetcher, const TsaServer& server);

}  // namespace ot::sign
