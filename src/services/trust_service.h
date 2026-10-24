// Хранилище доверия программы (план 4.6; эталон 3.8 «Доверие», 4.7 trust\; sign-30, sign-34): TrustStore
// каталога <данные>\trust (anchors.json; кеш списков EUTL/AATL — там же) — один на процесс. Проверка подписей
// и LTV берут снимок (snapshot — неизменяемый, живёт в задаче сколько нужно), правки — «Добавить в
// доверенные», импорт, флаги, удаление, корни Windows, установка списков — edit(fn) копией под замком: запись
// anchors.json, замена снимка, сигнал changed (проверка подписей открытых документов повторяется). Каталог
// данных сменился (тесты меняют OTTISK_DATA_DIR) — хранилище перечитывается. Поток: любой; changed — из
// потока правки.
#pragma once

#include <QObject>
#include <QString>

#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>

#include "core/sign/trust_store.h"

namespace ot {

class TrustService : public QObject {
    Q_OBJECT
public:
    static TrustService& shared();

    // <данные>\trust
    QString dir();
    std::shared_ptr<const sign::TrustStore> snapshot();

    // Правка копии хранилища: fn(TrustStore&) → anchors.json → новый снимок → changed. Исключение fn или
    // записи — хранилище как было, исключение наружу.
    template <class F>
    auto edit(F&& fn) -> std::invoke_result_t<F&, sign::TrustStore&>;

    // Перечитать anchors.json (списки установил кто-то мимо службы).
    void reload();

signals:
    void changed();

private:
    TrustService() = default;
    void ensureLoaded();  // под mutex_
    void commit(std::shared_ptr<sign::TrustStore> next);

    std::mutex mutex_;
    QString dir_;
    std::shared_ptr<const sign::TrustStore> store_;
};

template <class F>
auto TrustService::edit(F&& fn) -> std::invoke_result_t<F&, sign::TrustStore&> {
    using R = std::invoke_result_t<F&, sign::TrustStore&>;
    std::shared_ptr<sign::TrustStore> next;
    {
        const std::lock_guard lock(mutex_);
        ensureLoaded();
        next = std::make_shared<sign::TrustStore>(*store_);
    }
    if constexpr (std::is_void_v<R>) {
        fn(*next);
        commit(std::move(next));
    } else {
        R result = fn(*next);
        commit(std::move(next));
        return result;
    }
}

}  // namespace ot
