#include "core/sign/cng_store.h"

#include <windows.h>
#include <wincrypt.h>

#include <openssl/rand.h>
#include <spdlog/spdlog.h>

#include <cctype>
#include <cstdio>
#include <ctime>
#include <system_error>

#include "core/sign/chain.h"
#include "core/sign/identity.h"
#include "core/sign/pkcs12.h"
#include "core/util/paths.h"

#pragma comment(lib, "crypt32.lib")

namespace ot::sign::cng {

namespace {

PCCERT_CONTEXT ctxOf(const void* p) { return static_cast<PCCERT_CONTEXT>(p); }

std::string lastErrorText(const char* what, DWORD code) {
    return std::string(what) + ": " + std::system_category().message(static_cast<int>(code));
}

[[noreturn]] void throwLast(const char* what) {
    const DWORD code = GetLastError();
    throw StoreError(lastErrorText(what, code), code);
}

bool hasProperty(PCCERT_CONTEXT ctx, DWORD id) {
    DWORD size = 0;
    return CertGetCertificateContextProperty(ctx, id, nullptr, &size) != FALSE;
}

// «Microsoft Smart Card Key Storage Provider», «Microsoft Base Smart Card Crypto Provider» и KSP токенов
// с мини-драйвером (в имени — «Smart Card»).
bool smartCardProvider(std::string_view name) {
    return name.find("Smart Card") != std::string_view::npos ||
           name.find("SmartCard") != std::string_view::npos;
}

std::int64_t isoToUnix(const std::string& iso) {
    // «2026-01-01T00:00:00Z»
    std::tm tm{};
    if (iso.size() < 19 || std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
                                       &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6) {
        return 0;
    }
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    return static_cast<std::int64_t>(_mkgmtime(&tm));
}

}  // namespace

// ---------------------------------------------------------------- CertContext

CertContext::CertContext(const CertContext& other)
    : ctx_(other.ctx_ != nullptr ? CertDuplicateCertificateContext(ctxOf(other.ctx_)) : nullptr) {}

CertContext& CertContext::operator=(const CertContext& other) {
    if (this != &other) {
        CertContext copy(other);
        std::swap(ctx_, copy.ctx_);
    }
    return *this;
}

CertContext::CertContext(CertContext&& other) noexcept : ctx_(std::exchange(other.ctx_, nullptr)) {}

CertContext& CertContext::operator=(CertContext&& other) noexcept {
    if (this != &other) {
        if (ctx_ != nullptr) {
            CertFreeCertificateContext(ctxOf(ctx_));
        }
        ctx_ = std::exchange(other.ctx_, nullptr);
    }
    return *this;
}

CertContext::~CertContext() {
    if (ctx_ != nullptr) {
        CertFreeCertificateContext(ctxOf(ctx_));
    }
}

Bytes CertContext::der() const {
    if (ctx_ == nullptr) {
        return {};
    }
    const PCCERT_CONTEXT c = ctxOf(ctx_);
    return Bytes(c->pbCertEncoded, c->pbCertEncoded + c->cbCertEncoded);
}

Ossl<X509> CertContext::x509() const {
    if (ctx_ == nullptr) {
        return {};
    }
    const PCCERT_CONTEXT c = ctxOf(ctx_);
    const unsigned char* p = c->pbCertEncoded;
    return Ossl<X509>(ossl::checkPtr(d2i_X509(nullptr, &p, static_cast<long>(c->cbCertEncoded)), "d2i_X509"));
}

std::string CertContext::thumbprint() const {
    if (ctx_ == nullptr) {
        return {};
    }
    BYTE hash[20];
    DWORD len = sizeof hash;
    if (CertGetCertificateContextProperty(ctxOf(ctx_), CERT_SHA1_HASH_PROP_ID, hash, &len) == FALSE) {
        return {};
    }
    return ossl::toHex(std::span<const std::uint8_t>(hash, len));
}

bool CertContext::hasPrivateKey() const {
    if (ctx_ == nullptr) {
        return false;
    }
    const PCCERT_CONTEXT c = ctxOf(ctx_);
    return hasProperty(c, CERT_KEY_PROV_INFO_PROP_ID) || hasProperty(c, CERT_KEY_CONTEXT_PROP_ID) ||
           hasProperty(c, CERT_NCRYPT_KEY_HANDLE_PROP_ID) ||
           hasProperty(c, CERT_HCRYPTPROV_OR_NCRYPT_KEY_HANDLE_PROP_ID);
}

std::string CertContext::keyProvider() const {
    if (ctx_ == nullptr) {
        return {};
    }
    DWORD size = 0;
    if (CertGetCertificateContextProperty(ctxOf(ctx_), CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size) == FALSE ||
        size < sizeof(CRYPT_KEY_PROV_INFO)) {
        return {};
    }
    std::vector<std::uint8_t> buffer(size);
    if (CertGetCertificateContextProperty(ctxOf(ctx_), CERT_KEY_PROV_INFO_PROP_ID, buffer.data(), &size) ==
        FALSE) {
        return {};
    }
    const auto* info = reinterpret_cast<const CRYPT_KEY_PROV_INFO*>(buffer.data());
    return info->pwszProvName != nullptr ? paths::narrow(info->pwszProvName) : std::string();
}

// ---------------------------------------------------------------- CertStore

CertStore::CertStore(CertStore&& other) noexcept : store_(std::exchange(other.store_, nullptr)) {}

CertStore& CertStore::operator=(CertStore&& other) noexcept {
    if (this != &other) {
        if (store_ != nullptr) {
            CertCloseStore(static_cast<HCERTSTORE>(store_), 0);
        }
        store_ = std::exchange(other.store_, nullptr);
    }
    return *this;
}

CertStore::~CertStore() {
    if (store_ != nullptr) {
        CertCloseStore(static_cast<HCERTSTORE>(store_), 0);
    }
}

CertStore CertStore::openPersonal(bool writable) {
    const DWORD flags = CERT_SYSTEM_STORE_CURRENT_USER | CERT_STORE_OPEN_EXISTING_FLAG |
                        (writable ? 0 : CERT_STORE_READONLY_FLAG);
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, flags, L"MY");
    if (store == nullptr) {
        throwLast("CertOpenStore(MY)");
    }
    return CertStore(store);
}

CertStore CertStore::openMemory() {
    HCERTSTORE store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    if (store == nullptr) {
        throwLast("CertOpenStore(memory)");
    }
    return CertStore(store);
}

CertStore CertStore::importPfx(std::span<const std::uint8_t> pfx, std::string_view passwordUtf8,
                               KeyStorage storage) {
    CRYPT_DATA_BLOB blob{static_cast<DWORD>(pfx.size()), const_cast<BYTE*>(pfx.data())};
    const std::wstring password = paths::widen(passwordUtf8);
    const DWORD flags = CRYPT_USER_KEYSET | PKCS12_ALWAYS_CNG_KSP |
                        (storage == KeyStorage::Memory ? PKCS12_NO_PERSIST_KEY : 0);
    HCERTSTORE store = PFXImportCertStore(&blob, password.c_str(), flags);
    if (store == nullptr) {
        throwLast("PFXImportCertStore");
    }
    return CertStore(store);
}

std::vector<CertEntry> CertStore::list() const {
    CertFilter all;
    all.requirePrivateKey = false;
    all.requireSigningUsage = false;
    return list(all);
}

std::vector<CertEntry> CertStore::list(const CertFilter& filter) const {
    std::vector<CertEntry> out;
    if (store_ == nullptr) {
        return out;
    }
    const auto store = static_cast<HCERTSTORE>(store_);
    for (PCCERT_CONTEXT ctx = CertEnumCertificatesInStore(store, nullptr); ctx != nullptr;
         ctx = CertEnumCertificatesInStore(store, ctx)) {
        try {
            CertEntry e = describe(CertContext(CertDuplicateCertificateContext(ctx)));
            if (matches(e, filter)) {
                out.push_back(std::move(e));
            }
        } catch (const std::exception& ex) {
            // Сертификат, который OpenSSL не читает, в список не попадает.
            spdlog::warn("cng: skipping certificate: {}", ex.what());
        }
    }
    return out;
}

CertContext CertStore::find(std::string_view thumbprint) const {
    const std::string hex = normalizeThumbprint(thumbprint);
    if (store_ == nullptr || hex.empty()) {
        return {};
    }
    BYTE hash[20];
    for (std::size_t i = 0; i < 20; ++i) {
        hash[i] = static_cast<BYTE>(std::stoi(hex.substr(i * 2, 2), nullptr, 16));
    }
    CRYPT_HASH_BLOB blob{sizeof hash, hash};
    return CertContext(CertFindCertificateInStore(static_cast<HCERTSTORE>(store_), X509_ASN_ENCODING, 0,
                                                  CERT_FIND_SHA1_HASH, &blob, nullptr));
}

CertContext CertStore::add(const CertContext& cert) {
    PCCERT_CONTEXT added = nullptr;
    if (CertAddCertificateContextToStore(static_cast<HCERTSTORE>(store_), ctxOf(cert.get()),
                                         CERT_STORE_ADD_REPLACE_EXISTING, &added) == FALSE) {
        throwLast("CertAddCertificateContextToStore");
    }
    return CertContext(added);
}

CertContext CertStore::addDer(std::span<const std::uint8_t> der) {
    PCCERT_CONTEXT added = nullptr;
    if (CertAddEncodedCertificateToStore(static_cast<HCERTSTORE>(store_), X509_ASN_ENCODING, der.data(),
                                         static_cast<DWORD>(der.size()), CERT_STORE_ADD_REPLACE_EXISTING,
                                         &added) == FALSE) {
        throwLast("CertAddEncodedCertificateToStore");
    }
    return CertContext(added);
}

CertContext CertStore::addLink(const CertContext& cert) {
    PCCERT_CONTEXT added = nullptr;
    if (CertAddCertificateLinkToStore(static_cast<HCERTSTORE>(store_), ctxOf(cert.get()),
                                      CERT_STORE_ADD_REPLACE_EXISTING, &added) == FALSE) {
        throwLast("CertAddCertificateLinkToStore");
    }
    return CertContext(added);
}

// ---------------------------------------------------------------- описание и фильтр

CertEntry describe(const CertContext& cert, std::int64_t now) {
    CertEntry e;
    e.context = cert;
    e.thumbprint = cert.thumbprint();
    const Ossl<X509> x = cert.x509();
    e.summary = summarize(x.get());
    e.hasPrivateKey = cert.hasPrivateKey();
    e.signingUsage = allowedForDocumentSigning(x.get());
    const std::int64_t t = now != 0 ? now : static_cast<std::int64_t>(std::time(nullptr));
    const std::int64_t from = isoToUnix(e.summary.notBefore);
    const std::int64_t to = isoToUnix(e.summary.notAfter);
    e.expired = (to != 0 && t > to) || (from != 0 && t < from);
    e.keyProvider = cert.keyProvider();
    e.smartCard = smartCardProvider(e.keyProvider);
    return e;
}

bool matches(const CertEntry& entry, const CertFilter& filter) {
    return (!filter.requirePrivateKey || entry.hasPrivateKey) &&
           (!filter.requireSigningUsage || entry.signingUsage) && (!filter.excludeExpired || !entry.expired);
}

CertStore filtered(const CertStore& source, const CertFilter& filter) {
    CertStore out = CertStore::openMemory();
    for (const CertEntry& e : source.list(filter)) {
        (void)out.addLink(e.context);
    }
    return out;
}

std::string normalizeThumbprint(std::string_view thumbprint) {
    std::string hex;
    for (const char ch : thumbprint) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isxdigit(c) != 0 && c < 0x80) {
            hex.push_back(static_cast<char>(std::toupper(c)));
        }
    }
    return hex.size() == 40 ? hex : std::string();
}

CertEntry importIdentity(const SigningKey& key, CertStore& target, KeyStorage storage) {
    // Одноразовый пароль контейнера между OpenSSL и PFXImportCertStore.
    unsigned char raw[16];
    if (RAND_bytes(raw, sizeof raw) != 1) {
        throw OsslError::fromQueue("RAND_bytes");
    }
    const std::string password = ossl::toHex(std::span<const std::uint8_t>(raw, sizeof raw));
    const CertStore imported = CertStore::importPfx(exportPkcs12(key, password), password, storage);
    const CertContext withKey = imported.find(certificateThumbprint(key.cert.get()));
    if (!withKey || !withKey.hasPrivateKey()) {
        throw StoreError("imported PKCS#12 has no certificate with a private key");
    }
    // Ключ в памяти привязан к контексту импорта (дескриптор процесса — копия хранилища его теряет): в target
    // — ссылка; сохранённый в KSP ключ — свойством KEY_PROV_INFO копии.
    return describe(storage == KeyStorage::Memory ? target.addLink(withKey) : target.add(withKey));
}

}  // namespace ot::sign::cng
