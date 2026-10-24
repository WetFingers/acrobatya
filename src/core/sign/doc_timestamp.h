// Метка времени документа — DocTimeStamp (эталон 3.8 «LTV и B-LTA»; план 4.2, п. 2; sign-20): отдельная
// инкрементальная секция с невидимым полем /Sig «Timestamp<N>»; словарь /V — /Type /DocTimeStamp, /Filter
// /Adobe.PPKLite, /SubFilter /ETSI.RFC3161, /ByteRange, /Contents — токен RFC 3161 по хэшу /ByteRange; без
// /M, /Name, /Location, /Reason, /Reference (ETSI EN 319 142-1, 5.4); каталог — /Extensions /ESIC уровня не
// ниже 1. Повторяемо (продление B-LTA): каждая метка — своё поле и своя секция, прежние подписи не задеты.
// Несохранённое сначала записывается (как у подписи); служба недоступна — SignError TimestampFailed,
// документ возвращён к файлу. Поток — поток документа.
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

#include "core/doc/saver.h"
#include "core/sign/signer.h"
#include "core/sign/tsa.h"

namespace ot {
class Document;
}

namespace ot::sign {

// Подписант MuPDF для метки документа: create_digest — хэш /ByteRange алгоритмом службы → токен.
class TimestampSigner : public PdfSigner {
public:
    explicit TimestampSigner(std::shared_ptr<TsaClient> tsa);

    const TimestampInfo& info() const noexcept { return info_; }

protected:
    Bytes contents(std::span<const std::uint8_t> digest) override;

private:
    std::shared_ptr<TsaClient> tsa_;
    TimestampInfo info_;
};

struct DocTimestampResult {
    std::string field;
    std::size_t contentsSize = 0;  // токен в /Contents
    TimestampInfo timestamp;
    SaveReport save;
};

// Добавить метку времени документа и записать инкрементом в target (пусто — файл документа). Ошибка —
// SignError.
DocTimestampResult addDocumentTimestamp(Document& doc, std::shared_ptr<TsaClient> tsa,
                                        const std::filesystem::path& target = {});

}  // namespace ot::sign
