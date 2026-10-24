// Автообновление списков доверия (эталон 3.8 «Доверие» — «обновление раз в две недели через
// TrustUpdater»; 4.7 trust\; план 4.5, п. 4; sign-31): не чаще раза в 14 дней (время последней попытки —
// trust/listsCheckedAt; попытка засчитывается и при неудаче — сеть не дёргается на каждом запуске), EUTL и
// AATL — по ключам trust/eutl и trust/aatl. «Спрашивать перед установкой» (trust/listsAskBeforeInstall, по
// умолчанию да) — загруженное ждёт согласия (сигнал updateAvailable, install/discard); без вопроса —
// устанавливается сразу, кроме списков, которым нужна отдельная проверка (LOTL не pinned или просрочен,
// отклонённые TL), — они ждут согласия всегда. Установка — services/trust_lists.h installTrustLists в каталог
// доверия (<данные>\trust). Часы подменяются (тесты). Поток вызова (сеть синхронная — звать из фоновой
// задачи).
#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

#include "services/trust_lists.h"

namespace ot {

class Settings;

class TrustUpdater : public QObject {
    Q_OBJECT
public:
    static constexpr int kIntervalDays = 14;

    enum class Outcome {
        NotDue,     // прошло меньше 14 дней
        Disabled,   // EUTL и AATL выключены
        Failed,     // ничего не загрузилось
        Pending,    // загружено, ждёт согласия
        Installed,  // установлено без вопроса
    };

    // settings и fetcher живут дольше объекта; trustDir — каталог доверия (anchors.json, кеш списков).
    TrustUpdater(Settings& settings, net::HttpFetcher& fetcher, QString trustDir, QObject* parent = nullptr);

    void setClock(std::function<QDateTime()> clock) { clock_ = std::move(clock); }
    QDateTime now() const;
    QDateTime lastCheck() const;
    bool due() const;

    // Проверить, если пора (force — сейчас).
    Outcome check(bool force = false);

    bool hasPending() const { return pending_.has_value(); }
    const TrustListUpdate& pending() const { return *pending_; }
    // Установить ожидающее (согласие человека); число якорей. Нет ожидающего — 0.
    int install();
    void discard() { pending_.reset(); }

signals:
    void updateAvailable(int eutl, int aatl, bool needsConsent);
    void installed(int anchors);

private:
    int installUpdate(const TrustListUpdate& update);

    Settings& settings_;
    net::HttpFetcher& fetcher_;
    QString dir_;
    std::function<QDateTime()> clock_;
    std::optional<TrustListUpdate> pending_;
};

const char* trustUpdaterOutcomeName(TrustUpdater::Outcome outcome);

}  // namespace ot
