// Удостоверения (эталон 3.8 «Диалог подписания» — удостоверение, «Доверие» — «Удостоверения»; 4.7 — ids\;
// вопрос 12.5; план 4.2, п. 4; sign-05 — создать самоподписанное, sign-06 — удостоверение из файла, sign-10 —
// управление: по умолчанию, тайм-аут пароля, смена пароля, удаление; sign-11 — экспорт сертификата).
//
// Самоподписанное: RSA-3072 или ECDSA P-256 (EVP_PKEY_Q_keygen) на 5 лет, X.509 v3, basicConstraints CA:false
// (критическое), KU digitalSignature + nonRepudiation (критическое), EKU emailProtection, documentSigning
// (RFC 9336), Adobe Authentic Documents, SKI. PKCS#12 — AES-256-CBC + PBKDF2 для ключа и сертификатов и MAC
// SHA-256 (как openssl pkcs12 -export -keypbe AES-256-CBC -certpbe AES-256-CBC -macalg sha256) — Windows его
// импортирует. В хранилище Windows — importToWindows (PFXImportCertStore; ключ в KSP пользователя и
// сертификат в «Личное» — только по действию человека, тесты — без сохранения ключа).
//
// IdentityStore — записи в state.sqlite (таблица identities, шаг миграции services/state_db.cpp): id —
// SHA-256 сертификата HEX, вид (.pfx в <данные>\ids\ — имя ASCII по id; сертификат хранилища Windows —
// отпечаток SHA-1, подпись им — CNG, 4.3), сертификат DER (экспорт без пароля), «по умолчанию», тайм-аут
// пароля, пароль под защитой SecretProtector (DPAPI — services/dpapi.h) по флажку «запомнить». Пароль при
// открытии: аргумент → кэш в памяти до тайм-аута (0 — спрашивать всегда, kUntilExit — до выхода) →
// запомненный → NeedPassword. Вызывающий держит замок базы (StateDb::mutex) — services/identities.h. Пароли —
// UTF-8, в журнал не пишутся.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/pkcs12.h"

struct sqlite3;

namespace ot::sign {

enum class KeyAlgorithm { Rsa3072, EcdsaP256 };

struct SelfSignedOptions {
    std::string commonName;  // UTF-8 (обязательно)
    std::string organization;
    std::string organizationalUnit;
    std::string email;
    std::string country;  // две латинские буквы, пусто — нет
    KeyAlgorithm algorithm = KeyAlgorithm::Rsa3072;
    int years = 5;
};

// Ключ и самоподписанный сертификат (friendlyName — CN). Ошибка — OsslError, пустое имя — invalid_argument.
SigningKey createSelfSigned(const SelfSignedOptions& options);
// PKCS#12 удостоверения (ключ, сертификат, цепочка; friendlyName — key.friendlyName или CN).
Bytes exportPkcs12(const SigningKey& key, std::string_view passwordUtf8);

// Отпечаток SHA-1 HEX (как «Отпечаток» в Windows) и SHA-256 HEX (id удостоверения).
std::string certificateThumbprint(X509* cert);
std::string certificateId(X509* cert);
Bytes toDerCertificate(X509* cert);

struct WindowsImport {
    bool ok = false;
    std::string thumbprint;  // сертификата с ключом
    bool hasKey = false;
    std::string error;  // английский текст для журнала
};
// .pfx в Windows (PFXImportCertStore, пароль — UTF-16): persist = false — ключ только в памяти
// (PKCS12_NO_PERSIST_KEY), хранилища пользователя не меняются; true — ключ в KSP пользователя и сертификат в
// «Личное» (MY).
WindowsImport importToWindows(std::span<const std::uint8_t> pfx, std::string_view passwordUtf8, bool persist);

enum class CertFormat {
    Cer,  // DER
    P7c,  // PKCS#7 только сертификаты (как openssl crl2pkcs7 -nocrl)
    Fdf,  // обмен сертификатами Acrobat: %FDF-1.2, /FDF << /Certs [<DER>] >>
};
Bytes exportCertificate(X509* cert, CertFormat format);
// Сертификаты из FDF (/Certs — шестнадцатеричные и литеральные строки); не FDF — пусто.
std::vector<Bytes> readFdfCertificates(std::string_view fdf);

// Защита запомненных паролей (DPAPI текущего пользователя — services/dpapi.h; тесты — заглушка).
class SecretProtector {
public:
    virtual ~SecretProtector() = default;
    virtual Bytes protect(std::span<const std::uint8_t> plain) = 0;
    virtual std::optional<Bytes> unprotect(std::span<const std::uint8_t> secret) = 0;
};

enum class IdentityKind { Pkcs12File, WindowsStore };

struct IdentityRecord {
    std::string id;  // SHA-256 сертификата HEX
    IdentityKind kind = IdentityKind::Pkcs12File;
    std::filesystem::path file;  // .pfx в ids\ (Pkcs12File)
    std::string thumbprint;      // SHA-1 HEX
    std::string name;            // CN (или субъект)
    std::string subject;
    std::string issuer;
    std::string notAfter;  // ISO
    bool isDefault = false;
    bool rememberPassword = false;
    int passwordTimeout = -1;  // секунд; 0 — спрашивать всегда, −1 — до выхода
};

enum class IdentityStatus {
    NotFound,
    Exists,        // то же удостоверение уже есть
    NeedPassword,  // пароль не запомнен и не в кэше — спросить
    BadPassword,
    Damaged,       // .pfx испорчен или без ключа
    FileMissing,   // .pfx удалён с диска
    WindowsStore,  // удостоверение хранилища Windows — подпись через CNG (4.3)
    IoError,       // база или диск
};

class IdentityError : public std::runtime_error {
public:
    IdentityError(IdentityStatus status, const std::string& message)
        : std::runtime_error(message), status_(status) {}
    IdentityStatus status() const noexcept { return status_; }

private:
    IdentityStatus status_;
};

class IdentityStore {
public:
    static constexpr int kUntilExit = -1;
    using Clock = std::chrono::steady_clock;

    // db — state.sqlite (nullptr — пусто, правки — IoError); idsDir — <данные>\ids (создаётся при записи).
    IdentityStore(sqlite3* db, std::filesystem::path idsDir, SecretProtector* protector);

    // Таблица identities (CREATE … IF NOT EXISTS) — шаг миграции state.sqlite.
    static void createSchema(sqlite3* db);

    void setClock(std::function<Clock::time_point()> now) { now_ = std::move(now); }

    std::vector<IdentityRecord> list() const;  // по времени добавления
    std::optional<IdentityRecord> find(std::string_view id) const;
    std::optional<IdentityRecord> defaultIdentity() const;
    bool setDefault(std::string_view id);

    // Удостоверение из файла: проверка пароля, копия в ids\, запись (первое — по умолчанию); remember —
    // пароль под защитой. Ошибки — IdentityError (BadPassword, Damaged, Exists, IoError).
    IdentityRecord addPkcs12(const std::filesystem::path& file, std::string_view password,
                             bool rememberPassword);
    // Новое самоподписанное в ids\.
    IdentityRecord createSelfSigned(const SelfSignedOptions& options, std::string_view password,
                                    bool rememberPassword);
    // Сертификат из хранилища Windows (подпись — CNG, 4.3): только запись.
    IdentityRecord addWindows(X509* cert);
    // Убрать запись (и .pfx в ids\, если deleteFile); по умолчанию — первое оставшееся. Нет такой — false.
    bool remove(std::string_view id, bool deleteFile);
    // Сменить пароль .pfx (перезапись файла); запомненный пароль и кэш — новые.
    void changePassword(std::string_view id, std::string_view oldPassword, std::string_view newPassword);
    // Запомнить пароль (проверяется) или забыть (nullopt).
    void setRememberedPassword(std::string_view id, std::optional<std::string> password);
    void setPasswordTimeout(std::string_view id, int seconds);
    void forgetCachedPasswords();

    // Ключ удостоверения; пароль — см. начало файла.
    SigningKey open(std::string_view id, std::optional<std::string> password = std::nullopt);
    // Сертификат удостоверения в файл.
    void exportCertificate(std::string_view id, const std::filesystem::path& target, CertFormat format) const;

private:
    struct Cached {
        std::string password;
        std::optional<Clock::time_point> expires;  // нет — до выхода
    };

    IdentityRecord insert(const SigningKey& key, IdentityKind kind, const Bytes& pfx,
                          std::string_view password, bool rememberPassword);
    SigningKey load(const IdentityRecord& rec, std::string_view password) const;
    void cache(const IdentityRecord& rec, std::string_view password);
    void storeSecret(std::string_view id, std::optional<std::string_view> password);
    std::optional<std::string> secret(std::string_view id) const;
    Bytes certificateDer(std::string_view id) const;
    sqlite3* db() const;
    Clock::time_point now() const { return now_ ? now_() : Clock::now(); }

    sqlite3* db_;
    std::filesystem::path idsDir_;
    SecretProtector* protector_;
    std::function<Clock::time_point()> now_;
    std::map<std::string, Cached, std::less<>> cache_;
};

}  // namespace ot::sign
