// Хранилище сертификатов Windows для подписи (эталон 3.8 «Диалог подписания» — хранилище Windows, «CNG-путь»;
// план 4.3, п. 1; sign-08 — сертификат из хранилища Windows; sign-07 — вместо PKCS#11: смарт-карты и токены с
// мини-драйвером видны в «Личное» через тот же KSP, эталон 1.6). Ключ из хранилища не экспортируется: здесь —
// только сертификаты, перечисление и отпечатки; подпись ключом — cng_signer.h.
//
// CertStore — RAII над HCERTSTORE: «Личное» текущего пользователя (MY, только чтение; запись — только по
// явной команде человека), хранилище в памяти (тесты, отфильтрованный список для диалога выбора), PFX-импорт
// (PFXImportCertStore: ключ только в памяти процесса — PKCS12_NO_PERSIST_KEY, тесты без касания хранилищ
// человека; или в KSP пользователя — по команде «Импортировать в хранилище Windows»). CertContext — RAII над
// PCCERT_CONTEXT. Фильтр по умолчанию — «есть закрытый ключ и подпись документов» (KU digitalSignature или
// nonRepudiation, EKU — как ChainPurpose::DocumentSigning). Заголовок без <windows.h>: дескрипторы — void*.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/x509_info.h"

namespace ot::sign {
struct SigningKey;
}

namespace ot::sign::cng {

class StoreError : public std::runtime_error {
public:
    StoreError(const std::string& message, unsigned long code = 0)
        : std::runtime_error(message), code_(code) {}
    unsigned long code() const noexcept { return code_; }  // GetLastError

private:
    unsigned long code_;
};

// Сертификат хранилища (PCCERT_CONTEXT): копия — CertDuplicateCertificateContext.
class CertContext {
public:
    CertContext() = default;
    // Берёт владение контекстом (PCCERT_CONTEXT).
    explicit CertContext(const void* context) noexcept : ctx_(context) {}
    CertContext(const CertContext& other);
    CertContext& operator=(const CertContext& other);
    CertContext(CertContext&& other) noexcept;
    CertContext& operator=(CertContext&& other) noexcept;
    ~CertContext();

    const void* get() const noexcept { return ctx_; }
    explicit operator bool() const noexcept { return ctx_ != nullptr; }

    Bytes der() const;
    Ossl<X509> x509() const;
    std::string thumbprint() const;  // SHA-1 HEX заглавными, как certificateThumbprint
    // Закрытый ключ связан с сертификатом (свойства KEY_PROV_INFO, KEY_CONTEXT, NCRYPT_KEY_HANDLE) — без
    // открытия ключа (смарт-карта не спрашивает PIN).
    bool hasPrivateKey() const;
    std::string keyProvider() const;  // KSP/CSP из KEY_PROV_INFO (UTF-8); ключ только в памяти — пусто

private:
    const void* ctx_ = nullptr;
};

struct CertFilter {
    bool requirePrivateKey = true;
    bool requireSigningUsage = true;  // allowedForDocumentSigning (chain.h)
    bool excludeExpired = false;      // истёкшие остаются в списке с expired (Acrobat показывает их серыми)
};

struct CertEntry {
    CertContext context;
    std::string thumbprint;  // SHA-1 HEX
    CertSummary summary;     // имя, субъект, издатель, сроки, DER
    bool hasPrivateKey = false;
    bool signingUsage = false;  // разрешена подпись документов
    bool expired = false;       // notAfter в прошлом или notBefore в будущем
    std::string keyProvider;    // «Microsoft Smart Card Key Storage Provider» у карт и токенов
    bool smartCard = false;     // ключ на карте или токене (KSP/CSP смарт-карт)
};

// Где ключ PFX-импорта.
enum class KeyStorage {
    Memory,  // PKCS12_NO_PERSIST_KEY — только в памяти процесса (тесты, разовая подпись)
    User,    // KSP пользователя (CRYPT_USER_KEYSET) — по явной команде человека
};

class CertStore {
public:
    CertStore() = default;
    CertStore(CertStore&& other) noexcept;
    CertStore& operator=(CertStore&& other) noexcept;
    CertStore(const CertStore&) = delete;
    CertStore& operator=(const CertStore&) = delete;
    ~CertStore();

    // «Личное» (MY) текущего пользователя; writable = false — только чтение. Ошибка — StoreError.
    static CertStore openPersonal(bool writable = false);
    // Пустое хранилище в памяти (CERT_STORE_PROV_MEMORY).
    static CertStore openMemory();
    // PKCS#12 → хранилище в памяти со всеми сертификатами файла; ключ — в KSP (PKCS12_ALWAYS_CNG_KSP) по
    // storage. Неверный пароль, испорченный файл — StoreError.
    static CertStore importPfx(std::span<const std::uint8_t> pfx, std::string_view passwordUtf8,
                               KeyStorage storage);

    void* handle() const noexcept { return store_; }
    explicit operator bool() const noexcept { return store_ != nullptr; }

    // Все сертификаты (без фильтра) или по фильтру, в порядке хранилища.
    std::vector<CertEntry> list() const;
    std::vector<CertEntry> list(const CertFilter& filter) const;
    // По отпечатку SHA-1 (регистр, пробелы, двоеточия и невидимые знаки при копировании из certmgr не
    // мешают); нет — пустой контекст.
    CertContext find(std::string_view thumbprint) const;
    // Копия сертификата со свойствами (ключ KSP — через KEY_PROV_INFO; ключ только в памяти копия теряет) —
    // CertAddCertificateContextToStore, замена той же.
    CertContext add(const CertContext& cert);
    // Сертификат из DER без ключа.
    CertContext addDer(std::span<const std::uint8_t> der);
    // Ссылка на сертификат другого хранилища (свойства и ключ — его); для списков в памяти.
    CertContext addLink(const CertContext& cert);

private:
    explicit CertStore(void* store) noexcept : store_(store) {}
    void* store_ = nullptr;
};

// Описание сертификата хранилища (поля CertEntry); now — секунды Unix (0 — сейчас).
CertEntry describe(const CertContext& cert, std::int64_t now = 0);
bool matches(const CertEntry& entry, const CertFilter& filter);
// Хранилище в памяти со ссылками на подходящие сертификаты source — для CryptUIDlgSelectCertificateFromStore.
CertStore filtered(const CertStore& source, const CertFilter& filter);
// Отпечаток к виду «40 HEX заглавными»; не 40 шестнадцатеричных цифр — пусто.
std::string normalizeThumbprint(std::string_view thumbprint);

// Импорт удостоверения (ключ и сертификат с цепочкой) по команде человека: PKCS#12 с одноразовым паролем →
// PFXImportCertStore по storage → сертификат с ключом в target (MY — writable openPersonal; ключ в памяти —
// ссылкой, копия хранилища теряет дескриптор). Итог — запись target; ошибка — StoreError.
CertEntry importIdentity(const SigningKey& key, CertStore& target, KeyStorage storage);

}  // namespace ot::sign::cng
