#include "services/tsa_settings.h"

#include "services/http_fetcher.h"
#include "services/settings.h"

namespace ot {

sign::TsaServers loadTsaServers(const Settings& settings) {
    const QString json = settings.get(settings::kTsaServers);
    return json.isEmpty() ? sign::TsaServers::defaults() : sign::TsaServers::fromJson(json.toStdString());
}

void saveTsaServers(Settings& settings, const sign::TsaServers& servers) {
    settings.set(settings::kTsaServers, QString::fromStdString(servers.toJson()));
}

sign::TimestampFallback timestampFallback(const Settings& settings) {
    return sign::parseFallback(settings.get(settings::kTimestampFallback).toStdString());
}

std::shared_ptr<sign::TsaClient> makeTsaClient(const sign::TsaServer& server) {
    return std::make_shared<sign::TsaClient>(httpFetcher(), sign::TsaServers::config(server));
}

std::shared_ptr<sign::TsaClient> defaultTsaClient(const Settings& settings) {
    const sign::TsaServers servers = loadTsaServers(settings);
    const sign::TsaServer* server = servers.defaultServer();
    return server != nullptr ? makeTsaClient(*server) : nullptr;
}

}  // namespace ot
