#include "core/sign/trust_store.h"

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/pkcs7.h>

#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>

#include "core/sign/identity.h"
#include "core/util/paths.h"

// Windows SDK — после OpenSSL: wincrypt.h определяет макросы X509_NAME, OCSP_* (ниже они не нужны).
#include <windows.h>
#include <wincrypt.h>

#pragma comment(lib, "crypt32.lib")

namespace ot::sign {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

constexpr const char* kFile = "anchors.json";

std::string hexDigest(const Bytes& der, const EVP_MD* md) {
    Bytes out(EVP_MAX_MD_SIZE);
    unsigned int len = 0;
    ossl::check(EVP_Digest(der.data(), der.size(), out.data(), &len, md, nullptr), "EVP_Digest");
    out.resize(len);
    return ossl::toHex(out);
}

std::string base64Encode(const Bytes& data) {
    std::string out(4 * ((data.size() + 2) / 3) + 1, '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()), data.data(),
                                  static_cast<int>(data.size()));
    out.resize(static_cast<std::size_t>(std::max(n, 0)));
    return out;
}

Bytes base64Decode(const std::string& text) {
    Bytes out(3 * (text.size() / 4) + 3);
    const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(text.data()),
                                  static_cast<int>(text.size()));
    if (n < 0) {
        throw std::runtime_error("bad base64");
    }
    std::size_t len = static_cast<std::size_t>(n);
    // EVP_DecodeBlock не учитывает дополнение «=».
    for (auto it = text.rbegin(); it != text.rend() && *it == '='; ++it) {
        --len;
    }
    out.resize(len);
    return out;
}

const char* flagNames[] = {"signatures", "certification", "dynamicContent", "javaScript"};

json flagsToJson(unsigned flags) {
    json a = json::array();
    for (unsigned i = 0; i < 4; ++i) {
        if ((flags & (1u << i)) != 0) {
            a.push_back(flagNames[i]);
        }
    }
    return a;
}

unsigned flagsFromJson(const json& a) {
    unsigned flags = 0;
    for (const json& v : a) {
        for (unsigned i = 0; i < 4; ++i) {
            if (v.is_string() && v.get<std::string>() == flagNames[i]) {
                flags |= 1u << i;
            }
        }
    }
    return flags;
}

TrustSource sourceFromName(const std::string& s) {
    if (s == "eutl") {
        return TrustSource::Eutl;
    }
    if (s == "aatl") {
        return TrustSource::Aatl;
    }
    if (s == "windows") {
        return TrustSource::Windows;
    }
    return TrustSource::User;
}

// Сертификаты PKCS#7 (DER или PEM) — .p7b/.p7c.
std::vector<Bytes> pkcs7Certificates(std::span<const std::uint8_t> bytes) {
    std::vector<Bytes> out;
    Ossl<PKCS7> p7;
    {
        const unsigned char* p = bytes.data();
        p7.reset(d2i_PKCS7(nullptr, &p, static_cast<long>(bytes.size())));
    }
    if (!p7) {
        Ossl<BIO> bio = ossl::memBio(bytes);
        p7.reset(PEM_read_bio_PKCS7(bio.get(), nullptr, nullptr, nullptr));
    }
    ERR_clear_error();
    if (!p7 || !PKCS7_type_is_signed(p7.get()) || p7->d.sign == nullptr) {
        return out;
    }
    STACK_OF(X509)* certs = p7->d.sign->cert;
    for (int i = 0; certs != nullptr && i < sk_X509_num(certs); ++i) {
        out.push_back(ossl::toDer(sk_X509_value(certs, i), i2d_X509, "i2d_X509"));
    }
    return out;
}

}  // namespace

const char* trustSourceName(TrustSource source) {
    switch (source) {
        case TrustSource::User:
            return "user";
        case TrustSource::Eutl:
            return "eutl";
        case TrustSource::Aatl:
            return "aatl";
        case TrustSource::Windows:
            return "windows";
    }
    return "user";
}

std::string normalizeThumbprint(std::string_view text) {
    std::string out;
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (std::isxdigit(u) != 0) {
            out.push_back(static_cast<char>(std::toupper(u)));
        }
    }
    return out;
}

TrustStore TrustStore::load(const fs::path& dir) {
    TrustStore store;
    const fs::path file = dir / kFile;
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return store;
    }
    try {
        const json j = json::parse(paths::readFile(file));
        store.windowsRoots_ = j.value("windowsRoots", false);
        for (const json& a : j.at("anchors")) {
            store.add(base64Decode(a.at("der").get<std::string>()),
                      flagsFromJson(a.value("flags", json::array())),
                      sourceFromName(a.value("source", std::string("user"))));
        }
    } catch (const std::exception& e) {
        spdlog::warn("trust: {} is damaged ({}), starting with an empty trust store", paths::toUtf8(file),
                     e.what());
        fs::rename(file, dir / (std::string(kFile) + ".bad"), ec);
        return TrustStore{};
    }
    return store;
}

void TrustStore::save(const fs::path& dir) const {
    json anchors = json::array();
    for (const TrustAnchor& a : anchors_) {
        anchors.push_back({{"sha256", a.sha256},
                           {"subject", a.cert.subject},
                           {"flags", flagsToJson(a.flags)},
                           {"source", trustSourceName(a.source)},
                           {"der", base64Encode(a.cert.der)}});
    }
    const json j = {{"version", 1}, {"windowsRoots", windowsRoots_}, {"anchors", anchors}};
    std::error_code ec;
    fs::create_directories(dir, ec);
    paths::writeFile(dir / kFile, j.dump(1));
}

bool TrustStore::add(std::span<const std::uint8_t> cert, unsigned flags, TrustSource source) {
    Ossl<X509> x = loadCertificate(cert);
    TrustAnchor a;
    a.cert = summarize(x.get());
    a.sha1 = hexDigest(a.cert.der, EVP_sha1());
    a.sha256 = ossl::toHex(a.cert.sha256);
    a.flags = flags;
    a.source = source;
    if (find(a.sha256) != nullptr) {
        return false;
    }
    anchors_.push_back(std::move(a));
    return true;
}

int TrustStore::import(std::span<const std::uint8_t> bytes, unsigned flags) {
    std::vector<Bytes> certs;
    const std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    if (text.rfind("%FDF-", 0) == 0) {
        certs = readFdfCertificates(text);
    } else {
        certs = pkcs7Certificates(bytes);
        if (certs.empty()) {
            try {
                for (const Ossl<X509>& x : loadCertificates(bytes)) {
                    certs.push_back(ossl::toDer(x.get(), i2d_X509, "i2d_X509"));
                }
            } catch (const std::exception&) {
                ERR_clear_error();
            }
        }
    }
    int added = 0;
    for (const Bytes& der : certs) {
        try {
            added += add(der, flags) ? 1 : 0;
        } catch (const std::exception& e) {
            spdlog::info("trust: skipped a certificate: {}", e.what());
        }
    }
    return added;
}

int TrustStore::importFile(const fs::path& file, unsigned flags) {
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        throw std::runtime_error("no such file: " + paths::toUtf8(file));
    }
    const std::string bytes = paths::readFile(file);
    return import(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()), flags);
}

const TrustAnchor* TrustStore::find(std::string_view thumbprint) const {
    const std::string t = normalizeThumbprint(thumbprint);
    if (t.size() != 40 && t.size() != 64) {
        return nullptr;
    }
    for (const TrustAnchor& a : anchors_) {
        if (a.sha1 == t || a.sha256 == t) {
            return &a;
        }
    }
    return nullptr;
}

bool TrustStore::setFlags(std::string_view thumbprint, unsigned flags) {
    auto* a = const_cast<TrustAnchor*>(find(thumbprint));
    if (a == nullptr) {
        return false;
    }
    a->flags = flags;
    return true;
}

bool TrustStore::remove(std::string_view thumbprint) {
    const TrustAnchor* a = find(thumbprint);
    if (a == nullptr) {
        return false;
    }
    anchors_.erase(anchors_.begin() + (a - anchors_.data()));
    return true;
}

int TrustStore::count(unsigned flags) const {
    return static_cast<int>(std::count_if(anchors_.begin(), anchors_.end(),
                                          [&](const TrustAnchor& a) { return (a.flags & flags) != 0; }));
}

Ossl<X509_STORE> TrustStore::x509Store(unsigned flags) const {
    Ossl<X509_STORE> store(ossl::checkPtr(X509_STORE_new(), "X509_STORE_new"));
    const auto addDer = [&](const Bytes& der) {
        const unsigned char* p = der.data();
        Ossl<X509> x(d2i_X509(nullptr, &p, static_cast<long>(der.size())));
        if (x) {
            X509_STORE_add_cert(store.get(), x.get());  // дубликат — не ошибка
        }
    };
    for (const TrustAnchor& a : anchors_) {
        if ((a.flags & flags) != 0) {
            addDer(a.cert.der);
        }
    }
    if (windowsRoots_ && (flags & kTrustSignatures) != 0) {
        for (const Bytes& der : windowsRootCertificates()) {
            addDer(der);
        }
    }
    ERR_clear_error();
    return store;
}

std::vector<Bytes> windowsRootCertificates() {
    std::vector<Bytes> out;
    for (const DWORD location : {CERT_SYSTEM_STORE_CURRENT_USER, CERT_SYSTEM_STORE_LOCAL_MACHINE}) {
        HCERTSTORE store =
            CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                          location | CERT_STORE_OPEN_EXISTING_FLAG | CERT_STORE_READONLY_FLAG, L"ROOT");
        if (store == nullptr) {
            continue;
        }
        PCCERT_CONTEXT ctx = nullptr;
        while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
            Bytes der(ctx->pbCertEncoded, ctx->pbCertEncoded + ctx->cbCertEncoded);
            if (std::find(out.begin(), out.end(), der) == out.end()) {
                out.push_back(std::move(der));
            }
        }
        CertCloseStore(store, 0);
    }
    return out;
}

}  // namespace ot::sign
