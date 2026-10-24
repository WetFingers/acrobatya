// Хранилище доверия проверки подписей (эталон 3.8 «Доверие (sign-30–34)», «Валидатор»; 3.14; вопрос 12.4;
// план 4.5, п. 5 — хранилище целиком: импорт из подписи и хранилища Windows ROOT/CA по согласию, экспорт
// .cer/.p7c/.fdf, корни Windows по назначениям (подписи / сертифицированные документы, как iMSStoreTrusted);
// план 4.4, п. 2; sign-30 — ядро, sign-32): якоря — свои (импорт .cer DER/PEM, .p7b/.p7c, .fdf Acrobat), кеш
// EUTL и AATL (replaceSource по источнику, 4.5), корни Windows — опция, по умолчанию выключена (как
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

struct SignatureInfo;
enum class CertFormat;

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
    std::string label;  // подпись в списке (EUTL — «DE: поставщик — служба»), пусто — субъект
};

// Сертификат из списка доверия (EUTL, AATL) — для TrustStore::replaceSource и кеша списков.
struct ListedCertificate {
    Bytes der;
    unsigned flags = kTrustSignatures;
    std::string label;
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
             TrustSource source = TrustSource::User, std::string label = {});
    // Заменить якоря списка (EUTL, AATL) новым набором: прежние якоря источника уходят, сертификат, который
    // уже есть с другим источником (свой), остаётся своим. Число якорей источника после замены.
    int replaceSource(TrustSource source, const std::vector<ListedCertificate>& certs);
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

    // Корни Windows (хранилище ROOT текущего пользователя и машины) как якоря — для назначений flags (подписи
    // и (или) сертифицированные документы; Acrobat «Trust ALL root certificates in the Windows Certificate
    // Store for»); по умолчанию — нет. Не копируются в хранилище — читаются при сборке X509_STORE.
    void setWindowsRootsFlags(unsigned flags) noexcept { windowsRootsFlags_ = flags; }
    unsigned windowsRootsFlags() const noexcept { return windowsRootsFlags_; }
    void setUseWindowsRoots(bool on) noexcept { windowsRootsFlags_ = on ? kTrustSignatures : 0u; }
    bool useWindowsRoots() const noexcept { return windowsRootsFlags_ != 0; }

    // Импорт из хранилища Windows (только чтение; зовётся по явному согласию человека): ROOT и (или) CA
    // текущего пользователя и машины — якоря источника Windows с флагами flags. Число новых.
    int importWindows(bool root, bool ca, unsigned flags = kTrustSignatures);

    // Экспорт якоря: .cer (DER), .p7c, .fdf (identity.h exportCertificate); нет такого — пусто.
    Bytes exportAnchor(std::string_view thumbprint, CertFormat format) const;
    // Все якоря одним .p7c (PKCS#7 только сертификаты).
    Bytes exportAll() const;

    // Якоря с любым из битов flags (и корни Windows, если включены) — новое хранилище для ChainBuilder.
    Ossl<X509_STORE> x509Store(unsigned flags) const;
    // Число якорей с любым из битов flags (без корней Windows).
    int count(unsigned flags) const;

private:
    std::vector<TrustAnchor> anchors_;
    unsigned windowsRootsFlags_ = 0;
};

// Системные хранилища Windows: ROOT — корневые ЦС, CA — промежуточные.
enum class WindowsStore { Root, Ca };
// Сертификаты хранилища текущего пользователя и машины (только чтение) — DER без повторов.
std::vector<Bytes> windowsStoreCertificates(WindowsStore store);
// Сертификаты хранилища ROOT Windows (только чтение) — DER.
inline std::vector<Bytes> windowsRootCertificates() { return windowsStoreCertificates(WindowsStore::Root); }

// «Доверять» из подписи (Certificate Viewer → «Доверие» → «Добавить в доверенные», sign-34): сертификат с
// отпечатком thumbprint из построенной цепочки или CMS подписи; пусто — верх цепочки (корень), без цепочки —
// самоподписанный из CMS, иначе подписант. true — добавлен (уже был или нет такого — false).
bool trustFromSignature(TrustStore& store, const SignatureInfo& sig, unsigned flags,
                        std::string_view thumbprint = {});

}  // namespace ot::sign
