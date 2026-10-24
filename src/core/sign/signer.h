// Подписание PDF — PAdES B-B (эталон 3.8 «Механика», 4.4, 4.6; план 4.2, п. 1; sign-12 — поставить
// подпись, sign-14 — причина, место, контакт, sign-15 — «Заблокировать документ после подписи», sign-18 —
// ETSI.CAdES.detached, forms-17 — поле /Sig, ядро). Граница с MuPDF (эталон 4.6): MuPDF ставит виджет /Sig
// (pdf_create_signature_widget или существующее пустое поле), плейсхолдер /Contents и /ByteRange, внешний вид
// и пишет инкрементальную секцию с патчем диапазонов; наше — подписант PdfSigner (pdf_pkcs7_signer) и патч
// словаря /V до записи.
//
// Подписант: max_digest_size — константа 16 384 (резерв /Contents, 32 768 hex; без «сухого прогона» — со
// смарт-картой это лишний PIN), create_digest(in, digest, len) — хэш потока /ByteRange → contents(хэш) → DER;
// ошибка — 0 (MuPDF бросает «signer provided no signature digest»), причина — error(). AcroBatyaSigner —
// CMS CAdES-B (CmsBuilder::buildFromDigest), для B-T (sign-19, п. 2) — метка времени по хэшу значения подписи
// (RFC 3161 прил. A) unsigned-атрибутом id-aa-timeStampToken; служба недоступна — спросить (колбэк
// withoutTimestamp: да — подпись B-B, нет — SignError TimestampFailed).
//
// Словарь /V после pdf_sign_signature_with_appearance и до записи: /SubFilter → ETSI.CAdES.detached (или
// adbe.pkcs7.detached — «совместимость»), /Reason, /Location, /ContactInfo, /Name, /Prop_Build; /M ставит
// MuPDF; /Reference (FieldMDP) — только при /Lock поля; каталог — /Extensions /ESIC <</BaseVersion /1.7
// /ExtensionLevel 2>> только для CAdES (и /Version /1.7, если файл старше). Порядок ключей /ByteRange →
// /Contents → /Filter не меняется: по нему MuPDF находит диапазоны в записанном файле.
//
// signDocument: несохранённые правки, новый или восстановленный файл — сначала обычная запись по правилам
// Saver в target (документ переходит на него); затем подпись одной операцией журнала и Saver::Incremental в
// target через fz_output на _wfopen (подпись считается по файлу — fz_stream_from_output). Сбой записи или
// подписанта — документ возвращается к записанному файлу (Saver::revert), файл цели не тронут, SignError.
// Поток — поток документа.
#pragma once

#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>

#include "core/doc/saver.h"
#include "core/sign/appearance.h"
#include "core/sign/cms.h"
#include "core/sign/pkcs12.h"
#include "core/sign/tsa.h"
#include "core/util/geometry.h"

struct pdf_pkcs7_signer;

namespace ot {
class Document;
}

namespace ot::sign {

enum class SubFilter {
    CadesDetached,  // ETSI.CAdES.detached — PAdES (по умолчанию, вопрос 12.3)
    Pkcs7Detached,  // adbe.pkcs7.detached — «совместимость» со старыми проверяющими
};
const char* subFilterName(SubFilter subFilter) noexcept;

// Где подпись. Поле: существующее пустое /Sig с этим полным именем (UTF-8) — подписать его (страница и
// рамка — его); нет такого — новое поле с этим именем; пусто — новое «Signature<N>».
struct SignaturePlacement {
    std::string field;
    int page = 0;  // страница нового поля
    RectF rect;  // рамка нового поля в координатах страницы MuPDF; пустая — невидимая подпись /Rect [0 0 0 0]
};

struct SignRequest {
    std::shared_ptr<const SigningKey> identity;  // ключ, сертификат и цепочка (обязательно)
    std::string reason;                          // /Reason (UTF-8; пусто — ключа нет)
    std::string location;                        // /Location
    std::string contact;                         // /ContactInfo
    std::string name;                            // /Name и имя во внешнем виде; пусто — CN сертификата
    SubFilter subFilter = SubFilter::CadesDetached;
    SignaturePlacement placement;
    // Внешний вид видимой подписи (п. 3, appearance.h); данные — имя, DN сертификата, причина, место, /M.
    SignatureAppearance appearance;
    // «Заблокировать документ после подписи» (sign-15): /Lock <</Type /SigFieldLock /Action /All>> на поле до
    // подписания — все поля формы (и сама подпись) становятся ReadOnly, /Reference FieldMDP в подписи.
    // false — /Lock поля остаётся, как его поставил автор формы (у нового поля его нет).
    bool lock = false;
    // B-T: служба времени (nullptr — B-B). Метка по хэшу подписи; токен — в CMS, резерв общий (16 КиБ).
    std::shared_ptr<TsaClient> timestamp;
    // Служба времени не ответила или ответ не прошёл проверку: true — подписать без метки (B-B), false или
    // пусто — отказ (SignError TimestampFailed). Зовётся в потоке документа посреди записи.
    std::function<bool(const TsaError&)> withoutTimestamp;
    DigestAlgorithm digest = DigestAlgorithm::Sha256;
    std::int64_t signingTime = 0;  // /M, секунды Unix (UTC); 0 — сейчас
};

enum class SignStatus {
    NotPdf,
    NotAllowed,         // права документа не разрешают заполнять формы и подписывать
    NoIdentity,         // нет удостоверения
    NotSignatureField,  // поле с таким именем есть, но не /Sig
    FieldSigned,        // поле уже подписано
    FieldReadOnly,      // поле только для чтения (заблокировано прежней подписью)
    BadPlacement,       // нет такой страницы
    NoTarget,           // документ без файла и цель не задана
    CmsFailed,          // OpenSSL при сборке CMS
    TimestampFailed,    // служба времени, причина — tsaStatus()
    TooLarge,           // CMS больше резерва /Contents
    BadAppearance,      // картинка или логотип внешнего вида не читаются
    SaveFailed,         // запись (предварительная или с подписью), причина — saveError()
};

class SignError : public std::runtime_error {
public:
    SignError(SignStatus status, const std::string& message, SaveError save = SaveError::None,
              TsaStatus tsa = TsaStatus::Ok)
        : std::runtime_error(message), status_(status), save_(save), tsa_(tsa) {}
    SignStatus status() const noexcept { return status_; }
    SaveError saveError() const noexcept { return save_; }
    TsaStatus tsaStatus() const noexcept { return tsa_; }

private:
    SignStatus status_;
    SaveError save_;
    TsaStatus tsa_;
};

struct SignResult {
    std::string field;  // полное имя поля подписи
    int page = 0;
    RectF rect;  // рамка на странице (пусто — невидимая)
    bool visible = false;
    std::size_t contentsSize = 0;  // DER в /Contents
    bool timestamped = false;      // B-T: метка в подписи
    TimestampInfo timestamp;       // её сведения
    std::string timestampError;    // служба не ответила, подписано без метки (withoutTimestamp → true)
    SaveReport save;               // запись с подписью
};

// Подписант MuPDF поверх функции «хэш /ByteRange → DER для /Contents». Создаётся std::make_shared (объект
// MuPDF держит его, пока жива незавершённая подпись документа).
class PdfSigner : public std::enable_shared_from_this<PdfSigner> {
public:
    static constexpr std::size_t kMaxContentsSize = 16384;

    explicit PdfSigner(DigestAlgorithm digest = DigestAlgorithm::Sha256);
    virtual ~PdfSigner();
    PdfSigner(const PdfSigner&) = delete;
    PdfSigner& operator=(const PdfSigner&) = delete;

    // Новая ссылка pdf_pkcs7_signer для вызовов MuPDF (освобождает pdf_drop_signer); держит this.
    pdf_pkcs7_signer* newMuSigner();
    // Ошибка последнего create_digest (исключение contents или чтения потока); пусто — не было.
    std::exception_ptr error() const { return error_; }
    std::size_t contentsSize() const noexcept { return contentsSize_; }
    DigestAlgorithm digest() const noexcept { return digest_; }

    // То, что делает create_digest: хэш данных → DER /Contents (не больше kMaxContentsSize, иначе
    // SignError TooLarge). Ошибка — исключение.
    Bytes sign(std::span<const std::uint8_t> digest);

protected:
    virtual Bytes contents(std::span<const std::uint8_t> digest) = 0;
    // CN для pdf_pkcs7_distinguished_name (предпросмотр MuPDF); пусто — нет.
    virtual std::string signingName() const { return {}; }

private:
    friend struct MuSignerAccess;
    DigestAlgorithm digest_;
    std::exception_ptr error_;
    std::size_t contentsSize_ = 0;
};

// CMS CAdES-B по удостоверению; с tsa — B-T.
class AcroBatyaSigner : public PdfSigner {
public:
    AcroBatyaSigner(std::shared_ptr<const SigningKey> identity,
                    DigestAlgorithm digest = DigestAlgorithm::Sha256,
                    std::shared_ptr<TsaClient> tsa = nullptr,
                    std::function<bool(const TsaError&)> withoutTimestamp = {});

    bool timestamped() const noexcept { return timestamped_; }
    const TimestampInfo& timestampInfo() const noexcept { return timestampInfo_; }
    const std::string& timestampError() const noexcept { return timestampError_; }

protected:
    Bytes contents(std::span<const std::uint8_t> digest) override;
    std::string signingName() const override;

private:
    std::shared_ptr<const SigningKey> identity_;
    std::shared_ptr<TsaClient> tsa_;
    std::function<bool(const TsaError&)> withoutTimestamp_;
    bool timestamped_ = false;
    TimestampInfo timestampInfo_;
    std::string timestampError_;
};

// Подписать документ и записать в target (пусто — файл документа). Ошибка — SignError.
SignResult signDocument(Document& doc, const SignRequest& request, const std::filesystem::path& target = {});

// Свободное имя нового поля подписи: «Signature1», «Signature2»…
std::string nextSignatureFieldName(Document& doc);

}  // namespace ot::sign
