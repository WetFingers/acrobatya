// Сеть программы (эталон 7.4; security-36; план 4.1, п. 4): QtHttpFetcher — net::HttpFetcher на
// QNetworkAccessManager (TLS — Schannel), синхронно в потоке вызова (свой QNAM и цикл событий на запрос;
// зовут из рабочего потока), таймаут, перенаправления только на тот же хост и схему не слабее (TSA/OCSP),
// предел размера, выключатель «Сеть: разрешена / только подписи / выключена» (trust/network), журнал — метод,
// адрес без userinfo, код, размер и время, без тела. httpFetcher() — экземпляр процесса; тесты подменяют его
// ScopedHttpFetcher (FakeHttpFetcher из core/util/http_fetcher.h).
#pragma once

#include <optional>

#include "core/util/http_fetcher.h"

namespace ot {

class Settings;

enum class NetworkAccess { Allowed, SignaturesOnly, Off };

// Значение trust/network.
NetworkAccess networkAccess(const Settings& settings);

class QtHttpFetcher : public net::HttpFetcher {
public:
    QtHttpFetcher() = default;  // выключатель — из настроек при каждом запросе
    explicit QtHttpFetcher(NetworkAccess fixed) : fixed_(fixed) {}  // постоянный (тесты, проверки)

    net::Response fetch(const net::Request& request) override;

private:
    std::optional<NetworkAccess> fixed_;
};

// Сеть процесса: по умолчанию QtHttpFetcher по настройкам; setHttpFetcher(nullptr) — снова он. Не владеет.
net::HttpFetcher& httpFetcher();
void setHttpFetcher(net::HttpFetcher* fetcher);

// Подмена на время области видимости (тесты): ставит fetcher и возвращает прежний.
class ScopedHttpFetcher {
public:
    explicit ScopedHttpFetcher(net::HttpFetcher& fetcher);
    ~ScopedHttpFetcher();
    ScopedHttpFetcher(const ScopedHttpFetcher&) = delete;
    ScopedHttpFetcher& operator=(const ScopedHttpFetcher&) = delete;

private:
    net::HttpFetcher* previous_;
};

}  // namespace ot
