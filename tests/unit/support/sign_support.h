// Помощники тестов подписи (план 4.2): удостоверение тестовой PKI, копия неподписанной фикстуры, форма с
// полями, разбор подписей файла (словарь /V, /ByteRange, /Contents без дополнения, /Lock, /Extensions /ESIC)
// и проверка CMS по /ByteRange файла с корнем PKI.
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/sign/ossl.h"
#include "core/sign/pkcs12.h"
#include "core/sign/signer.h"
#include "core/sign/tsa.h"
#include "core/util/http_fetcher.h"

namespace ot::test {

// Удостоверение PKI (пароль «acrobatya»): signer-rsa.pfx, signer-ec.pfx.
std::shared_ptr<const ot::sign::SigningKey> pkiIdentity(const char* pfx = "signer-rsa.pfx");
// То же удостоверение как сертификат «хранилища Windows» (план 4.3): PFXImportCertStore в памяти процесса
// (PKCS12_NO_PERSIST_KEY | PKCS12_ALWAYS_CNG_KSP), без окон — хранилища человека не трогаются.
ot::sign::WindowsCert windowsIdentity(const char* pfx = "signer-rsa.pfx");

// Служба времени PKI в процессе (FakeHttpFetcher + localTsaReply); TsaClient держит fetcher ссылкой — живут
// вместе.
struct LocalTsa {
    std::shared_ptr<ot::net::FakeHttpFetcher> fetcher;
    std::shared_ptr<ot::sign::TsaClient> client;
};
LocalTsa localTsa();
std::filesystem::path copyFile(const std::filesystem::path& from, const std::filesystem::path& to);
// tests/fixtures/sig/unsigned.pdf → dir/name.
std::filesystem::path unsignedCopy(const std::filesystem::path& dir, const std::wstring& name);
// Длина DER по заголовку (0 — не SEQUENCE).
std::size_t derLength(const ot::sign::Bytes& der);

struct SigView {
    std::string field, type, filter, subFilter, reason, location, contact, name, m;
    std::string propBuildApp;
    std::vector<ot::sign::ByteRange> ranges;
    ot::sign::Bytes contents;  // DER без дополнения
    std::size_t reserved = 0;
    bool reference = false;
    std::string transformAction;  // /Reference[0] /TransformParams /Action
    std::string lockAction;       // /Lock /Action поля
    int objnum = 0;
    int ff = 0;
    float rect[4] = {0, 0, 0, 0};
};

struct DocView {
    std::vector<SigView> sigs;
    int esicLevel = -1;  // /Extensions /ESIC /ExtensionLevel, −1 — нет
    std::string esicBase;
    std::vector<std::pair<std::string, int>> otherFields;  // имя, Ff
};

// Подписи (поля /Sig с /V) и прочие поля файла, /Extensions /ESIC каталога.
DocView readSignatures(const std::filesystem::path& file);
// CMS_verify с CAdES и цепочкой до корня PKI по /ByteRange файла; нет signing-time, есть ESS v2,
// message-digest — хэш диапазонов.
void expectCmsValid(const std::filesystem::path& file, const SigView& s);
// Подпись покрывает файл до конца.
bool coversFile(const std::filesystem::path& file, const SigView& s);
// Документ с текстовым полем «Имя» и (withSigField) пустым полем подписи «Подпись заказчика» без /Lock.
std::filesystem::path makeFormFile(const std::filesystem::path& dir, bool withSigField);

}  // namespace ot::test
