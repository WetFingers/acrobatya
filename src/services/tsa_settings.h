// Серверы времени в настройках (план 4.2, п. 2; эталон 3.8 «Доверие» — «Серверы времени»; вопрос 12.2):
// список core/sign/tsa_servers.h — JSON в signatures/tsaServers (пусто или испорчено — DigiCert, Sectigo,
// GlobalSign), «служба недоступна» — signatures/timestampFallback; TsaClient к службе по умолчанию поверх
// httpFetcher() процесса (выключатель trust/network действует). Интерфейс «Серверы времени» — 4.6.
#pragma once

#include <memory>

#include "core/sign/tsa_servers.h"

namespace ot {

class Settings;

sign::TsaServers loadTsaServers(const Settings& settings);
void saveTsaServers(Settings& settings, const sign::TsaServers& servers);
sign::TimestampFallback timestampFallback(const Settings& settings);

// Клиент службы server (или службы по умолчанию) через httpFetcher(); список пуст — nullptr.
std::shared_ptr<sign::TsaClient> makeTsaClient(const sign::TsaServer& server);
std::shared_ptr<sign::TsaClient> defaultTsaClient(const Settings& settings);

}  // namespace ot
