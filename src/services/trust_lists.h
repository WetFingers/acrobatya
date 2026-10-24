// Списки доверия — службы (эталон 3.8 «Доверие»; 4.7 trust\; 7.4; вопрос 12.4; план 4.5, п. 4; sign-31):
// разбор XML LOTL и национальных TL (ETSI TS 119 612) QXmlStreamReader-ом в модели core/sign/eutl.h, загрузка
// через net::HttpFetcher (Purpose::Signatures — работает при «Сеть: только подписи»), AATL — файл
// tl12.acrobatsecuritysettings в core/sign/aatl.h, pins подписантов LOTL — ресурс :/trust/lotl_pins.json
// (assets/trust). Кеш — <данные>\trust\{eutl,aatl}.json (сертификаты base64, флаги, подписи, когда и что
// загружено); установка — TrustStore::replaceSource и anchors.json. Поток вызова (сеть синхронная).
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "core/sign/aatl.h"
#include "core/sign/eutl.h"
#include "core/util/http_fetcher.h"

namespace ot {

inline constexpr const char* kLotlUrl = "https://ec.europa.eu/tools/lotl/eu-lotl.xml";
inline constexpr const char* kAatlUrl = "https://trustlist.adobe.com/tl12.acrobatsecuritysettings";

// Разбор XML (испорчен — std::runtime_error с местом ошибки).
sign::Lotl parseLotl(const QByteArray& xml);
sign::TrustedList parseTrustedList(const QByteArray& xml);

// Pins из ресурса программы.
sign::LotlPins lotlPins();

// LOTL и TL по сети.
class HttpTrustListSource : public sign::TrustListSource {
public:
    explicit HttpTrustListSource(net::HttpFetcher& fetcher, std::string lotlUrl = kLotlUrl);
    sign::Lotl lotl() override;
    sign::TrustedList list(const sign::ListPointer& pointer) override;

private:
    QByteArray get(const std::string& url, std::size_t limit);

    net::HttpFetcher& fetcher_;
    std::string lotlUrl_;
};

// Загруженные списки (ещё не установленные).
struct TrustListUpdate {
    QDateTime checked;  // UTC
    std::optional<sign::EutlResult> eutl;
    std::string eutlError;  // LOTL не загрузился или не разобрался
    std::optional<sign::AatlResult> aatl;
    std::string aatlError;  // файл не загрузился

    int eutlCount() const { return eutl ? static_cast<int>(eutl->certificates.size()) : 0; }
    int aatlCount() const;
    // Применять только по согласию: LOTL не pinned или просрочен, отклонённые TL.
    bool needsConsent() const { return eutl && !eutl->trustworthy(); }
    bool empty() const { return eutlCount() == 0 && aatlCount() == 0; }
};

// Загрузить EUTL и (или) AATL; ошибки — в полях результата.
TrustListUpdate downloadTrustLists(net::HttpFetcher& fetcher, bool eutl, bool aatl, std::time_t now = 0);

// Кеш списка: source — "eutl" или "aatl" (файл <dir>/<source>.json).
struct TrustListCache {
    QDateTime fetched;
    int sequence = 0;    // LOTL (EUTL)
    QString nextUpdate;  // LOTL (EUTL)
    QString signer;      // CN подписанта (AATL)
    std::vector<sign::ListedCertificate> certificates;
};
void saveTrustListCache(const QString& dir, const QString& source, const TrustListCache& cache);
std::optional<TrustListCache> loadTrustListCache(const QString& dir, const QString& source);

// Установить загруженное: кеш и якоря источников (TrustStore::replaceSource) с записью anchors.json в dir.
// Пустой или отклонённый список источника не трогает его прежние якоря. Число установленных якорей.
int installTrustLists(const TrustListUpdate& update, sign::TrustStore& store, const QString& dir);

}  // namespace ot
