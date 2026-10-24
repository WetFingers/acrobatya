// Хранилище доверия проверки подписей (эталон 3.8 «Доверие (sign-30–34)», «Валидатор»; 3.14; вопрос 12.4;
// план 4.4, п. 2; sign-30 — ядро, sign-32): якоря — свои (импорт .cer DER/PEM, .p7b/.p7c, .fdf Acrobat), кеш
// EUTL и AATL (сессия 4.5 — тем же add с источником), корни Windows — опция, по умолчанию выключена (как
// iMSStoreTrusted=0 у Acrobat). Флаги доверия — как в диалоге Acrobat «Доверенные сертификаты»: подписи,
// сертифицированные документы, динамический контент, JavaScript. Отпечатки — SHA-1 (как в certmgr) и SHA-256,
// поиск в любом написании. Хранение — каталог данных (%LOCALAPPDATA%\AcroBatya\trust\anchors.json: DER в
// base64, флаги, источник; корни Windows не копируются — читаются при сборке X509_STORE). Не потокобезопасен:
// владелец — службы (один экземпляр, копия на проверку).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/x509_info.h"

namespace ot::sign {

// Для чего доверять якорю (биты).
enum TrustFlags : unsigned {
    kTrustSignatures = 1u << 0,     // подписи (утверждающие и метки времени)
    kTrustCertification = 1u << 1,  // сертифицированные документы (DocMDP)
    kTrustDynamicContent = 1u << 2,
    kTrustJavaScript = 1u << 3,
};

enum class TrustSource { User, Eutl, Aatl, Windows };
const char* trustSourceName(TrustSource source);  // "user", "eutl", "aatl", "windows"

struct TrustAnchor {
    CertSummary cert;  // DER — cert.der
    std::string sha1;  // отпечаток HEX, верхний регистр
    std::string sha256;
    unsigned flags = kTrustSignatures;
    TrustSource source = TrustSource::User;
};

// Отпечаток без пробелов, разделителей и невидимых символов, верхним регистром («a1 b2:…» → «A1B2…»).
std::string normalizeThumbprint(std::string_view text);

class TrustStore {
public:
    // Каталог хранения: файл anchors.json. Нет файла — пусто; испорчен — пусто, файл переименован в
    // anchors.json.bad (журнал), исключения нет.
    static TrustStore load(const std::filesystem::path& dir);
    // Записать anchors.json (каталог создаётся); ошибка записи — std::runtime_error.
    void save(const std::filesystem::path& dir) const;

    // Добавить якорь (DER или PEM одного сертификата). Уже есть — false, флаги не меняются. Не сертификат —
    // OsslError.
    bool add(std::span<const std::uint8_t> cert, unsigned flags = kTrustSignatures,
             TrustSource source = TrustSource::User);
    // Импорт сертификатов из байтов файла: .cer/.crt/.pem (DER или PEM, в т. ч. несколько), .p7b/.p7c
    // (PKCS#7 только сертификаты, DER или PEM), .fdf (обмен сертификатами Acrobat). Число новых якорей; в
    // данных нет сертификатов — 0.
    int import(std::span<const std::uint8_t> bytes, unsigned flags = kTrustSignatures);
    // То же из файла (путь UTF-16); нет файла — std::runtime_error.
    int importFile(const std::filesystem::path& file, unsigned flags = kTrustSignatures);

    // По отпечатку SHA-1 или SHA-256 в любом написании; nullptr — нет.
    const TrustAnchor* find(std::string_view thumbprint) const;
    bool setFlags(std::string_view thumbprint, unsigned flags);
    bool remove(std::string_view thumbprint);
    const std::vector<TrustAnchor>& anchors() const noexcept { return anchors_; }

    // Корни Windows (хранилище ROOT текущего пользователя и машины) как якоря подписей; по умолчанию — нет.
    void setUseWindowsRoots(bool on) noexcept { windowsRoots_ = on; }
    bool useWindowsRoots() const noexcept { return windowsRoots_; }

    // Якоря с любым из битов flags (и корни Windows, если включены) — новое хранилище для ChainBuilder.
    Ossl<X509_STORE> x509Store(unsigned flags) const;
    // Число якорей с любым из битов flags (без корней Windows).
    int count(unsigned flags) const;

private:
    std::vector<TrustAnchor> anchors_;
    bool windowsRoots_ = false;
};

// Сертификаты хранилища ROOT Windows (только чтение) — DER.
std::vector<Bytes> windowsRootCertificates();

}  // namespace ot::sign
