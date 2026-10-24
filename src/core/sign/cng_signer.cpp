#include "core/sign/cng_signer.h"

#include <windows.h>
#include <ncrypt.h>
#include <wincrypt.h>

#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/evp.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <system_error>
#include <utility>
#include <vector>

#include "core/sign/cms_external.h"

#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "ncrypt.lib")

namespace ot::sign {

namespace {

// Коды отказов CNG и смарт-карт, которые различает интерфейс.
constexpr long kNoKeyProperty = static_cast<long>(0x8009200B);   // CRYPT_E_NO_KEY_PROPERTY
constexpr long kBadKeyset = static_cast<long>(0x80090016);       // NTE_BAD_KEYSET
constexpr long kNoKey = static_cast<long>(0x8009000D);           // NTE_NO_KEY
constexpr long kSilentContext = static_cast<long>(0x80090022);   // NTE_SILENT_CONTEXT
constexpr long kUserCancelled = static_cast<long>(0x80090036);   // NTE_USER_CANCELLED
constexpr long kScardCancelled = static_cast<long>(0x8010006E);  // SCARD_W_CANCELLED_BY_USER
constexpr long kCancelled = static_cast<long>(0x800704C7);       // HRESULT_FROM_WIN32(ERROR_CANCELLED)

std::string codeText(long code) { return std::system_category().message(static_cast<int>(code)); }

long asHresult(DWORD error) {
    // GetLastError у Crypt* — то HRESULT, то код Win32.
    return (error & 0x80000000u) != 0 ? static_cast<long>(error)
                                      : static_cast<long>(HRESULT_FROM_WIN32(error));
}

// Отказ CNG → CngError с понятным статусом.
[[noreturn]] void throwCng(const char* what, long code, CngError::Status fallback) {
    CngError::Status status = fallback;
    if (code == kSilentContext) {
        status = CngError::UiRequired;
    } else if (code == kUserCancelled || code == kScardCancelled || code == kCancelled) {
        status = CngError::Cancelled;
    } else if (code == kNoKeyProperty || code == kBadKeyset || code == kNoKey) {
        status = CngError::NoPrivateKey;
    }
    throw CngError(status, std::string(what) + ": " + codeText(code), code);
}

LPCWSTR cngHashName(DigestAlgorithm digest) {
    switch (digest) {
        case DigestAlgorithm::Sha384:
            return BCRYPT_SHA384_ALGORITHM;
        case DigestAlgorithm::Sha512:
            return BCRYPT_SHA512_ALGORITHM;
        case DigestAlgorithm::Sha256:
        default:
            return BCRYPT_SHA256_ALGORITHM;
    }
}

CngKeyKind keyKindOf(const cng::CertContext& cert) {
    const Ossl<X509> x = cert.x509();
    const EVP_PKEY* pub = X509_get0_pubkey(x.get());
    if (pub != nullptr && EVP_PKEY_is_a(pub, "RSA")) {
        return CngKeyKind::Rsa;
    }
    if (pub != nullptr && EVP_PKEY_is_a(pub, "EC")) {
        return CngKeyKind::Ec;
    }
    throw CngError(CngError::UnsupportedKey, "certificate key is neither RSA nor ECDSA");
}

}  // namespace

// ---------------------------------------------------------------- CngKey

CngKey CngKey::acquire(const cng::CertContext& cert, const CngOptions& options) {
    if (!cert) {
        throw CngError(CngError::NotFound, "no certificate");
    }
    CngKey key;
    key.kind_ = keyKindOf(cert);
    key.silent_ = options.silent;
    key.cert_ = cert;

    // CACHE: ключ, который живёт только в памяти (PFX без сохранения), доступен лишь через свойство
    // KEY_CONTEXT контекста — дескриптор кэшируется в нём и освобождается вместе с контекстом (CngKey держит
    // копию). WINDOW_HANDLE и SILENT вместе нельзя (Microsoft Learn): окно — только когда окна разрешены.
    DWORD flags =
        CRYPT_ACQUIRE_CACHE_FLAG | CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG | CRYPT_ACQUIRE_COMPARE_KEY_FLAG;
    HWND window = reinterpret_cast<HWND>(options.window);
    void* parameters = nullptr;
    if (options.silent) {
        flags |= CRYPT_ACQUIRE_SILENT_FLAG;
    } else if (window != nullptr) {
        flags |= CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG;
        parameters = &window;
    }
    HCRYPTPROV_OR_NCRYPT_KEY_HANDLE handle = 0;
    DWORD spec = 0;
    BOOL mustFree = FALSE;
    if (CryptAcquireCertificatePrivateKey(static_cast<PCCERT_CONTEXT>(cert.get()), flags, parameters, &handle,
                                          &spec, &mustFree) == FALSE) {
        throwCng("CryptAcquireCertificatePrivateKey", asHresult(GetLastError()), CngError::NoPrivateKey);
    }
    if (spec == CERT_NCRYPT_KEY_SPEC) {
        key.key_ = static_cast<std::uintptr_t>(handle);
        key.owned_ = mustFree != FALSE;
        return key;
    }
    // Ключ старого CSP (AT_SIGNATURE / AT_KEYEXCHANGE): сначала перевести в CNG; не переводится (ключ без
    // контейнера, CSP карты без KSP) — RSA подписывается через CryptoAPI (CryptSignHash, только PKCS#1).
    NCRYPT_PROV_HANDLE provider = 0;
    NCRYPT_KEY_HANDLE translated = 0;
    const SECURITY_STATUS st = NCryptTranslateHandle(&provider, &translated, handle, 0, spec, 0);
    if (st == ERROR_SUCCESS) {
        if (mustFree != FALSE) {
            CryptReleaseContext(handle, 0);
        }
        key.key_ = static_cast<std::uintptr_t>(translated);
        key.provider_ = static_cast<std::uintptr_t>(provider);
        key.owned_ = true;
        if (window != nullptr && !options.silent) {
            // Владелец окна PIN для переведённого ключа.
            NCryptSetProperty(translated, NCRYPT_WINDOW_HANDLE_PROPERTY, reinterpret_cast<PBYTE>(&window),
                              sizeof window, 0);
        }
        return key;
    }
    if (key.kind_ != CngKeyKind::Rsa) {
        if (mustFree != FALSE) {
            CryptReleaseContext(handle, 0);
        }
        throw CngError(CngError::LegacyProvider, "NCryptTranslateHandle: " + codeText(static_cast<long>(st)),
                       static_cast<long>(st));
    }
    spdlog::info("cng: legacy CSP key, signing through CryptoAPI");
    key.capi_ = static_cast<std::uintptr_t>(handle);
    key.capiSpec_ = spec;
    key.owned_ = mustFree != FALSE;
    return key;
}

CngKey::CngKey(CngKey&& other) noexcept
    : key_(std::exchange(other.key_, 0)),
      provider_(std::exchange(other.provider_, 0)),
      capi_(std::exchange(other.capi_, 0)),
      capiSpec_(other.capiSpec_),
      owned_(std::exchange(other.owned_, false)),
      kind_(other.kind_),
      silent_(other.silent_),
      cert_(std::move(other.cert_)) {}

CngKey& CngKey::operator=(CngKey&& other) noexcept {
    if (this != &other) {
        reset();
        key_ = std::exchange(other.key_, 0);
        provider_ = std::exchange(other.provider_, 0);
        capi_ = std::exchange(other.capi_, 0);
        capiSpec_ = other.capiSpec_;
        owned_ = std::exchange(other.owned_, false);
        kind_ = other.kind_;
        silent_ = other.silent_;
        cert_ = std::move(other.cert_);
    }
    return *this;
}

CngKey::~CngKey() { reset(); }

void CngKey::reset() noexcept {
    if (key_ != 0 && owned_) {
        NCryptFreeObject(static_cast<NCRYPT_HANDLE>(key_));
    }
    if (provider_ != 0) {
        NCryptFreeObject(static_cast<NCRYPT_HANDLE>(provider_));
    }
    if (capi_ != 0 && owned_) {
        CryptReleaseContext(static_cast<HCRYPTPROV>(capi_), 0);
    }
    key_ = 0;
    provider_ = 0;
    capi_ = 0;
    owned_ = false;
    cert_ = cng::CertContext();
}

Bytes CngKey::signHash(std::span<const std::uint8_t> hash, DigestAlgorithm digest, CngPadding padding) const {
    if (hash.size() != digestSize(digest)) {
        throw std::invalid_argument("CngKey::signHash: hash length " + std::to_string(hash.size()) +
                                    ", expected " + std::to_string(digestSize(digest)));
    }
    if (capi_ != 0) {
        return capiSignHash(hash, digest, padding);
    }
    if (key_ == 0) {
        throw CngError(CngError::NoPrivateKey, "no key handle");
    }
    BCRYPT_PKCS1_PADDING_INFO pkcs1{cngHashName(digest)};
    BCRYPT_PSS_PADDING_INFO pss{cngHashName(digest), static_cast<ULONG>(hash.size())};
    void* info = nullptr;
    DWORD flags = silent_ ? NCRYPT_SILENT_FLAG : 0;
    if (kind_ == CngKeyKind::Rsa) {
        if (padding == CngPadding::Pss) {
            info = &pss;
            flags |= NCRYPT_PAD_PSS_FLAG;
        } else {
            info = &pkcs1;
            flags |= NCRYPT_PAD_PKCS1_FLAG;
        }
    }
    const auto key = static_cast<NCRYPT_KEY_HANDLE>(key_);
    auto* data = const_cast<PBYTE>(hash.data());
    const auto dataLen = static_cast<DWORD>(hash.size());
    DWORD size = 0;
    SECURITY_STATUS st = NCryptSignHash(key, info, data, dataLen, nullptr, 0, &size, flags);
    if (st != ERROR_SUCCESS) {
        throwCng("NCryptSignHash", static_cast<long>(st), CngError::SignFailed);
    }
    Bytes signature(size);
    st = NCryptSignHash(key, info, data, dataLen, signature.data(), size, &size, flags);
    if (st != ERROR_SUCCESS) {
        throwCng("NCryptSignHash", static_cast<long>(st), CngError::SignFailed);
    }
    signature.resize(size);
    return kind_ == CngKeyKind::Ec ? ecdsaP1363ToDer(signature) : signature;
}

std::uintptr_t CngKey::reopenWithAes() const {
    DWORD size = 0;
    const auto ctx = static_cast<PCCERT_CONTEXT>(cert_.get());
    if (ctx == nullptr ||
        CertGetCertificateContextProperty(ctx, CERT_KEY_PROV_INFO_PROP_ID, nullptr, &size) == FALSE) {
        return 0;
    }
    std::vector<BYTE> buffer(size);
    if (CertGetCertificateContextProperty(ctx, CERT_KEY_PROV_INFO_PROP_ID, buffer.data(), &size) == FALSE) {
        return 0;
    }
    const auto* info = reinterpret_cast<const CRYPT_KEY_PROV_INFO*>(buffer.data());
    if (info->dwProvType != PROV_RSA_FULL || info->pwszContainerName == nullptr) {
        return 0;
    }
    HCRYPTPROV aes = 0;
    const DWORD flags = (info->dwFlags & CRYPT_MACHINE_KEYSET) | (silent_ ? CRYPT_SILENT : 0);
    if (CryptAcquireContextW(&aes, info->pwszContainerName, nullptr, PROV_RSA_AES, flags) == FALSE) {
        return 0;
    }
    return static_cast<std::uintptr_t>(aes);
}

Bytes CngKey::capiSignHash(std::span<const std::uint8_t> hash, DigestAlgorithm digest,
                           CngPadding padding) const {
    if (padding == CngPadding::Pss) {
        throw CngError(CngError::LegacyProvider, "RSA-PSS needs a CNG key");
    }
    ALG_ID alg = CALG_SHA_256;
    if (digest == DigestAlgorithm::Sha384) {
        alg = CALG_SHA_384;
    } else if (digest == DigestAlgorithm::Sha512) {
        alg = CALG_SHA_512;
    }
    HCRYPTPROV prov = static_cast<HCRYPTPROV>(capi_);
    HCRYPTPROV reopened = 0;
    HCRYPTHASH h = 0;
    if (CryptCreateHash(prov, alg, 0, 0, &h) == FALSE) {
        // «Enhanced Cryptographic Provider» (PROV_RSA_FULL) не знает SHA-2: тот же контейнер — через
        // «Enhanced RSA and AES» (PROV_RSA_AES). Ключ без контейнера (только в памяти) — честный отказ.
        const DWORD first = GetLastError();
        reopened = static_cast<HCRYPTPROV>(reopenWithAes());
        if (reopened == 0 || CryptCreateHash(reopened, alg, 0, 0, &h) == FALSE) {
            if (reopened != 0) {
                CryptReleaseContext(reopened, 0);
            }
            throwCng("CryptCreateHash", asHresult(first), CngError::LegacyProvider);
        }
        prov = reopened;
    }
    const auto release = [&] {
        if (reopened != 0) {
            CryptReleaseContext(reopened, 0);
        }
    };
    const auto destroy = [&] {
        CryptDestroyHash(h);
        release();
    };
    if (CryptSetHashParam(h, HP_HASHVAL, const_cast<BYTE*>(hash.data()), 0) == FALSE) {
        const DWORD e = GetLastError();
        destroy();
        throwCng("CryptSetHashParam", asHresult(e), CngError::SignFailed);
    }
    DWORD size = 0;
    if (CryptSignHashW(h, static_cast<DWORD>(capiSpec_), nullptr, 0, nullptr, &size) == FALSE) {
        const DWORD e = GetLastError();
        destroy();
        throwCng("CryptSignHash", asHresult(e), CngError::SignFailed);
    }
    Bytes signature(size);
    if (CryptSignHashW(h, static_cast<DWORD>(capiSpec_), nullptr, 0, signature.data(), &size) == FALSE) {
        const DWORD e = GetLastError();
        destroy();
        throwCng("CryptSignHash", asHresult(e), CngError::SignFailed);
    }
    destroy();
    signature.resize(size);
    // CryptoAPI отдаёт подпись младшим байтом вперёд.
    std::reverse(signature.begin(), signature.end());
    return signature;
}

Bytes ecdsaP1363ToDer(std::span<const std::uint8_t> rs) {
    if (rs.empty() || rs.size() % 2 != 0) {
        throw std::invalid_argument("ECDSA P1363 signature must have an even, non-zero length");
    }
    const std::size_t half = rs.size() / 2;
    Ossl<BIGNUM> r(ossl::checkPtr(BN_bin2bn(rs.data(), static_cast<int>(half), nullptr), "BN_bin2bn"));
    Ossl<BIGNUM> s(ossl::checkPtr(BN_bin2bn(rs.data() + half, static_cast<int>(half), nullptr), "BN_bin2bn"));
    ECDSA_SIG* sig = ossl::checkPtr(ECDSA_SIG_new(), "ECDSA_SIG_new");
    if (ECDSA_SIG_set0(sig, r.get(), s.get()) != 1) {
        ECDSA_SIG_free(sig);
        throw OsslError::fromQueue("ECDSA_SIG_set0");
    }
    (void)r.release();  // владеет sig
    (void)s.release();
    unsigned char* der = nullptr;
    const int len = i2d_ECDSA_SIG(sig, &der);
    ECDSA_SIG_free(sig);
    if (len <= 0 || der == nullptr) {
        throw OsslError::fromQueue("i2d_ECDSA_SIG");
    }
    Bytes out(der, der + len);
    OPENSSL_free(der);
    return out;
}

// ---------------------------------------------------------------- удостоверение и цепочка

Ossl<STACK_OF(X509)> certificateChain(const cng::CertContext& cert) {
    Ossl<STACK_OF(X509)> out(ossl::checkPtr(sk_X509_new_null(), "sk_X509_new_null"));
    if (!cert) {
        return out;
    }
    const auto ctx = static_cast<PCCERT_CONTEXT>(cert.get());
    CERT_CHAIN_PARA para{};
    para.cbSize = sizeof para;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    // Только кэш: подпись не ходит в сеть за промежуточными (AIA — дело LTV, 4.5).
    const DWORD flags = CERT_CHAIN_CACHE_ONLY_URL_RETRIEVAL | CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE;
    if (CertGetCertificateChain(nullptr, ctx, nullptr, ctx->hCertStore, &para, flags, nullptr, &chain) ==
            FALSE ||
        chain == nullptr) {
        spdlog::warn("cng: certificate chain is not built ({})", GetLastError());
        return out;
    }
    if (chain->cChain > 0) {
        const PCERT_SIMPLE_CHAIN simple = chain->rgpChain[0];
        for (DWORD i = 1; i < simple->cElement; ++i) {
            const PCCERT_CONTEXT c = simple->rgpElement[i]->pCertContext;
            const unsigned char* p = c->pbCertEncoded;
            X509* x = d2i_X509(nullptr, &p, static_cast<long>(c->cbCertEncoded));
            if (x != nullptr && sk_X509_push(out.get(), x) <= 0) {
                X509_free(x);
            }
        }
    }
    CertFreeCertificateChain(chain);
    return out;
}

CngIdentity CngIdentity::open(const cng::CertStore& store, std::string_view thumbprint,
                              const CngOptions& options) {
    CngIdentity id;
    id.context = store.find(thumbprint);
    if (!id.context) {
        throw CngError(CngError::NotFound, "no certificate with thumbprint " + std::string(thumbprint));
    }
    if (!id.context.hasPrivateKey()) {
        throw CngError(CngError::NoPrivateKey, "certificate has no private key");
    }
    id.cert = id.context.x509();
    id.chain = certificateChain(id.context);
    id.key = std::make_shared<CngKey>(CngKey::acquire(id.context, options));
    return id;
}

Bytes cngCmsFromDigest(const CngIdentity& identity, std::span<const std::uint8_t> digest,
                       DigestAlgorithm algorithm) {
    if (!identity.key || !identity.cert) {
        throw CngError(CngError::NoPrivateKey, "Windows identity is not open");
    }
    const std::shared_ptr<CngKey> key = identity.key;
    ExternalCmsBuilder builder(
        identity.cert.get(), identity.chain.get(),
        [key, algorithm](std::span<const std::uint8_t> hash) {
            return key->signHash(hash, algorithm, CngPadding::Pkcs1);
        },
        algorithm);
    return builder.buildFromDigest(digest);
}

}  // namespace ot::sign
