#include "core/sign/eutl.h"

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <stdexcept>

#include "core/sign/x509_info.h"

namespace ot::sign {

namespace {

constexpr std::array<std::string_view, 4> kTypes = {"CA/QC", "TSA/QTST", "TSA", "NationalRootCA-QC"};
constexpr std::array<std::string_view, 5> kStatuses = {"granted", "recognisedatnationallevel", "accredited",
                                                       "undersupervision", "setbynationallaw"};

bool suffixIn(std::string_view uri, std::string_view prefix, auto const& list) {
    if (uri.rfind(prefix, 0) != 0) {
        return false;
    }
    const std::string_view tail = uri.substr(prefix.size());
    return std::find(list.begin(), list.end(), tail) != list.end();
}

std::string sha256Hex(const Bytes& der) { return ossl::toHex(ossl::sha256(der)); }

bool declared(const std::vector<Bytes>& signers, const std::vector<Bytes>& allowed) {
    for (const Bytes& s : signers) {
        if (std::find(allowed.begin(), allowed.end(), s) != allowed.end()) {
            return true;
        }
    }
    return false;
}

bool expired(const std::string& nextUpdate, std::time_t now) {
    const std::time_t t = isoToTime(nextUpdate);
    return t != 0 && t < now;
}

}  // namespace

std::time_t isoToTime(std::string_view iso) {
    std::tm tm{};
    const std::string s(iso);
    if (std::sscanf(s.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour,
                    &tm.tm_min, &tm.tm_sec) != 6) {
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    const std::time_t t = _mkgmtime(&tm);
    return t == static_cast<std::time_t>(-1) ? 0 : t;
}

LotlPins LotlPins::parse(std::string_view text) {
    try {
        const nlohmann::json j = nlohmann::json::parse(text);
        LotlPins p;
        p.url = j.value("url", std::string());
        p.snapshot = j.value("snapshot", std::string());
        for (const nlohmann::json& pin : j.at("pins")) {
            p.sha256.push_back(normalizeThumbprint(pin.at("sha256").get<std::string>()));
        }
        return p;
    } catch (const nlohmann::json::exception& e) {
        throw std::runtime_error(std::string("LOTL pins: ") + e.what());
    }
}

bool LotlPins::contains(const Bytes& der) const {
    const std::string h = sha256Hex(der);
    return std::find(sha256.begin(), sha256.end(), h) != sha256.end();
}

bool usableService(const TrustService& service) {
    return suffixIn(service.type, etsi::kSvcType, kTypes) &&
           suffixIn(service.status, etsi::kSvcStatus, kStatuses);
}

const char* listIssueName(ListIssue issue) {
    switch (issue) {
        case ListIssue::None:
            return "none";
        case ListIssue::Unpinned:
            return "unpinned";
        case ListIssue::Expired:
            return "expired";
        case ListIssue::SignerNotDeclared:
            return "signerNotDeclared";
        case ListIssue::Unavailable:
            return "unavailable";
    }
    return "?";
}

bool EutlResult::trustworthy() const {
    if (!pinned || lotlExpired) {
        return false;
    }
    return std::none_of(territories.begin(), territories.end(), [](const TerritoryResult& t) {
        return t.issue == ListIssue::SignerNotDeclared || t.issue == ListIssue::Unpinned;
    });
}

EutlResult buildEutl(TrustListSource& source, const LotlPins& pins, const EutlOptions& options) {
    const std::time_t now = options.now != 0 ? options.now : std::time(nullptr);
    const Lotl lotl = source.lotl();
    EutlResult result;
    result.lotlSequence = lotl.sequence;
    result.lotlNextUpdate = lotl.nextUpdate;
    result.pinned = std::any_of(lotl.signers.begin(), lotl.signers.end(),
                                [&](const Bytes& der) { return pins.contains(der); });
    result.lotlExpired = expired(lotl.nextUpdate, now);
    if (!result.pinned) {
        spdlog::warn("eutl: LOTL {} is signed by a certificate outside the pinned list", lotl.sequence);
    }
    std::set<std::string> seen;
    for (const ListPointer& p : lotl.pointers) {
        if (p.type == etsi::kTslTypeLotl || (!p.mimeType.empty() && p.mimeType != etsi::kTslMime)) {
            continue;
        }
        if (!options.territories.empty() && std::find(options.territories.begin(), options.territories.end(),
                                                      p.territory) == options.territories.end()) {
            continue;
        }
        TerritoryResult t;
        t.territory = p.territory;
        t.location = p.location;
        TrustedList list;
        try {
            list = source.list(p);
        } catch (const std::exception& e) {
            t.issue = ListIssue::Unavailable;
            t.detail = e.what();
            spdlog::info("eutl: {} {}: {}", p.territory, p.location, e.what());
            result.territories.push_back(std::move(t));
            continue;
        }
        t.sequence = list.sequence;
        t.nextUpdate = list.nextUpdate;
        if (!declared(list.signers, p.signers)) {
            t.issue = ListIssue::SignerNotDeclared;
            t.detail = "the list signer is not declared in the LOTL pointer";
            result.territories.push_back(std::move(t));
            continue;
        }
        if (expired(list.nextUpdate, now)) {
            t.issue = ListIssue::Expired;
            t.detail = "next update was due " + list.nextUpdate;
        }
        for (const TrustService& s : list.services) {
            if (!usableService(s)) {
                continue;
            }
            for (const Bytes& der : s.certificates) {
                if (!seen.insert(sha256Hex(der)).second) {
                    continue;
                }
                ListedCertificate c;
                c.der = der;
                c.flags = kTrustSignatures | kTrustCertification;
                c.label = list.territory + ": " + s.provider + (s.name.empty() ? "" : " — " + s.name);
                result.certificates.push_back(std::move(c));
                ++t.certificates;
            }
        }
        result.territories.push_back(std::move(t));
    }
    spdlog::info("eutl: LOTL {} ({}{}), {} lists, {} anchors", lotl.sequence,
                 result.pinned ? "pinned" : "UNPINNED", result.lotlExpired ? ", expired" : "",
                 result.territories.size(), result.certificates.size());
    return result;
}

}  // namespace ot::sign
