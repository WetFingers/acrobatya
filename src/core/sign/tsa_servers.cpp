#include "core/sign/tsa_servers.h"

#include <openssl/rand.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>

namespace ot::sign {

TimestampFallback parseFallback(std::string_view text) {
    if (text == "withoutTimestamp") {
        return TimestampFallback::WithoutTimestamp;
    }
    if (text == "cancel") {
        return TimestampFallback::Cancel;
    }
    return TimestampFallback::Ask;
}

const char* fallbackName(TimestampFallback fallback) noexcept {
    switch (fallback) {
        case TimestampFallback::Ask:
            return "ask";
        case TimestampFallback::WithoutTimestamp:
            return "withoutTimestamp";
        case TimestampFallback::Cancel:
            return "cancel";
    }
    return "ask";
}

TsaServers TsaServers::defaults() {
    TsaServers s;
    s.add({"DigiCert", "http://timestamp.digicert.com", {}, {}});
    s.add({"Sectigo", "http://timestamp.sectigo.com", {}, {}});
    s.add({"GlobalSign", "http://timestamp.globalsign.com/tsa/r6advanced1", {}, {}});
    return s;
}

TsaServers TsaServers::fromJson(std::string_view json) {
    const nlohmann::json j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("servers") || !j["servers"].is_array()) {
        return defaults();
    }
    TsaServers s;
    for (const auto& item : j["servers"]) {
        if (!item.is_object()) {
            continue;
        }
        TsaServer server;
        server.name = item.value("name", std::string());
        server.url = item.value("url", std::string());
        server.user = item.value("user", std::string());
        (void)s.add(std::move(server));
    }
    if (s.servers_.empty()) {
        return defaults();
    }
    if (j.contains("default") && j["default"].is_string()) {
        (void)s.setDefault(j["default"].get<std::string>());
    }
    return s;
}

std::string TsaServers::toJson() const {
    nlohmann::json list = nlohmann::json::array();
    for (const TsaServer& server : servers_) {
        nlohmann::json item = {{"name", server.name}, {"url", server.url}};
        if (!server.user.empty()) {
            item["user"] = server.user;
        }
        list.push_back(std::move(item));
    }
    const nlohmann::json j = {{"default", default_}, {"servers", std::move(list)}};
    return j.dump();
}

const TsaServer* TsaServers::find(std::string_view name) const {
    const auto it =
        std::find_if(servers_.begin(), servers_.end(), [&](const TsaServer& s) { return s.name == name; });
    return it != servers_.end() ? &*it : nullptr;
}

const TsaServer* TsaServers::defaultServer() const {
    const TsaServer* s = find(default_);
    return s != nullptr ? s : (servers_.empty() ? nullptr : &servers_.front());
}

bool TsaServers::add(TsaServer server) {
    if (server.name.empty() || server.url.empty() || find(server.name) != nullptr) {
        return false;
    }
    if (servers_.empty()) {
        default_ = server.name;
    }
    servers_.push_back(std::move(server));
    return true;
}

bool TsaServers::update(std::string_view name, TsaServer server) {
    const auto it =
        std::find_if(servers_.begin(), servers_.end(), [&](const TsaServer& s) { return s.name == name; });
    if (it == servers_.end() || server.name.empty() || server.url.empty()) {
        return false;
    }
    if (server.name != name && find(server.name) != nullptr) {
        return false;
    }
    if (default_ == name) {
        default_ = server.name;
    }
    *it = std::move(server);
    return true;
}

bool TsaServers::remove(std::string_view name) {
    const auto it =
        std::find_if(servers_.begin(), servers_.end(), [&](const TsaServer& s) { return s.name == name; });
    if (it == servers_.end()) {
        return false;
    }
    servers_.erase(it);
    if (default_ == name) {
        default_ = servers_.empty() ? std::string() : servers_.front().name;
    }
    return true;
}

bool TsaServers::setDefault(std::string_view name) {
    if (find(name) == nullptr) {
        return false;
    }
    default_ = std::string(name);
    return true;
}

TsaConfig TsaServers::config(const TsaServer& server) {
    TsaConfig c;
    c.url = server.url;
    c.user = server.user;
    c.password = server.password;
    return c;
}

TsaCheck checkTsaServer(net::HttpFetcher& fetcher, const TsaServer& server) {
    TsaCheck check;
    Bytes imprint(32);
    if (RAND_bytes(imprint.data(), static_cast<int>(imprint.size())) != 1) {
        check.status = TsaStatus::VerifyFailed;
        check.message = "RAND_bytes failed";
        return check;
    }
    const auto started = std::chrono::steady_clock::now();
    try {
        TsaClient client(fetcher, TsaServers::config(server));
        (void)client.timestamp(imprint);
        check.ok = true;
        check.info = client.lastInfo();
    } catch (const TsaError& e) {
        check.status = e.status();
        check.message = e.what();
        check.httpCode = e.httpCode();
    }
    check.ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started)
            .count();
    return check;
}

}  // namespace ot::sign
