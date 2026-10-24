// Adobe Approved Trust List — AATL (эталон 3.8 «Доверие»; 1.4 sign-31 — «попытка разбора с честным отказом»;
// вопрос 12.4; план 4.5, п. 4). tl12.acrobatsecuritysettings (https://trustlist.adobe.com/) — PDF,
// подписанный Adobe, с вложенным SecuritySettings.xml; формат не задокументирован — разбор «по факту»: все
// потоки PDF, в тексте — элементы <Identity> с <Certificate> (base64 X.509) и <Trust> (<Root>,
// <CertifiedDocuments>, <DynamicContent>, <JavaScript> → флаги хранилища доверия). «Похоже на список» — не
// меньше kAatlMinCertificates сертификатов; иначе — NotATrustList («не удалось — импортируйте вручную»).
// Подпись файла проверяется на целостность (без доверия: корень Adobe не наш якорь) — испорчена —
// DamagedSignature.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "core/sign/trust_store.h"

namespace ot::sign {

inline constexpr int kAatlMinCertificates = 100;

enum class AatlStatus {
    Ok,
    NotPdf,            // не открылся как PDF
    NotATrustList,     // сертификатов меньше kAatlMinCertificates — формат не распознан
    DamagedSignature,  // подпись списка не цела
};
const char* aatlStatusName(AatlStatus status);

struct AatlResult {
    AatlStatus status = AatlStatus::NotATrustList;
    std::vector<ListedCertificate> certificates;  // без повторов
    int identities = 0;                           // элементов <Identity> (или найденных <Certificate>)
    int skipped = 0;                              // не base64 или не X.509
    bool signedList = false;                      // в файле есть подпись
    bool signatureIntact = false;
    std::string signer;  // CN подписанта списка
    std::string detail;  // английский — журнал
};

AatlResult parseAatl(std::string bytes);
AatlResult parseAatlFile(const std::filesystem::path& file);

}  // namespace ot::sign
