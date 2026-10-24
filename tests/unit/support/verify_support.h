// Помощники тестов валидатора (план 4.4): файлы корпуса EU DSS (нет корпуса — тест пропускается), ожидания
// tests/fixtures/sig/verifier_expected.json, сверка подписи с ожиданием, правка байтов /Contents подписанного
// файла (испорченный DER той же длины) и /ByteRange-«дыры».
#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "core/sign/ossl.h"
#include "core/sign/pkcs12.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/http_fetcher.h"

namespace ot::test {

// <корпус>/dss/dss-pades/src/test/resources/validation/<rel>; нет файла — nullopt.
std::optional<std::filesystem::path> dssFile(const std::string& rel);
// tests/fixtures/sig/verifier_expected.json
const nlohmann::json& verifierExpectations();

// Сверить подпись с ожиданием JSON (ключи — см. about файла ожиданий); what — имя для сообщений.
void expectSignature(const ot::sign::SignatureInfo& sig, const nlohmann::json& want, const std::string& what);
// Подпись по имени поля (nullptr — нет).
const ot::sign::SignatureInfo* findSignature(const ot::sign::DocumentVerification& v,
                                             const std::string& field);

// Заменить DER подписи sigIndex (по порядку /ByteRange в файле) результатом edit — длина hex та же
// (DER длиннее дыры — std::runtime_error).
void patchContents(const std::filesystem::path& file, int sigIndex,
                   const std::function<void(ot::sign::Bytes&)>& edit);
// Заменить в файле первое вхождение from на to той же длины (нет — std::runtime_error).
void patchBytes(const std::filesystem::path& file, const std::string& from, const std::string& to);

// Службы тестовой PKI за FakeHttpFetcher (http://pki.acrobatya.test/…): OCSP — ответы фикстур по серийному
// номеру (signer-rsa, signer-ec — good, revoked — revoked, прочие — unknown), CRL и сертификаты ЦС — файлы.
std::shared_ptr<ot::net::FakeHttpFetcher> pkiResponder();
// FakeHttpFetcher, у которого любой запрос — «нет сети».
std::shared_ptr<ot::net::FakeHttpFetcher> offlineFetcher();
// Удостоверение отозванного сертификата PKI (revoked.pem, отозван 2026-06-01, keyCompromise) с цепочкой.
std::shared_ptr<const ot::sign::SigningKey> revokedIdentity();
// Якоря — самоподписанные сертификаты, встроенные в файл (CMS подписей и меток, /DSS): корпус DSS без своих
// корней (режим сверки с pyHanko).
ot::sign::TrustStore embeddedRoots(const ot::sign::DocumentVerification& v);
// Секунды Unix для даты UTC.
std::int64_t utc(int year, int month, int day);

}  // namespace ot::test
