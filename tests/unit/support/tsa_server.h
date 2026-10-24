// Локальная служба времени RFC 3161 для тестов (план 4.1): tools/pki/tsa_serve.py как QProcess через
// $OTTISK_PYTHON -X utf8 -I (путь — из окружения ctest, cmake/tests.cmake), порт — первая строка stdout.
// Сервер живёт, пока жив объект: деструктор закрывает stdin (скрипт выходит сам) и при нужде убивает процесс.
// Пути сервера: /tsr — ответ openssl ts -reply; /reject, /garbage, /wrongtype, /http500, /slow?ms=N,
// /redirect?to=… — сбои для клиента (см. tsa_serve.py).
#pragma once

#include <memory>
#include <string>
#include <string_view>

class QProcess;

namespace ot::test {

class TsaServer {
public:
    // Запустить и дождаться порта. nullptr и причина в reason — нет Python, скрипта, openssl.exe или фикстур
    // PKI (тест делает GTEST_SKIP() << reason) либо сервер не ответил за timeoutMs.
    static std::unique_ptr<TsaServer> start(std::string* reason, int timeoutMs = 20000);
    ~TsaServer();
    TsaServer(const TsaServer&) = delete;
    TsaServer& operator=(const TsaServer&) = delete;

    int port() const { return port_; }
    // http://127.0.0.1:<порт><path>
    std::string url(std::string_view path = "/tsr") const;

private:
    TsaServer(std::unique_ptr<QProcess> process, int port);

    std::unique_ptr<QProcess> process_;
    int port_ = 0;
};

}  // namespace ot::test
