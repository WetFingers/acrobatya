// Удостоверения программы (план 4.2, п. 4; эталон 3.8, 4.7): sign::IdentityStore над state.sqlite (таблица
// identities, миграция 5) и <данные>\ids\, запомненные пароли — DPAPI (services/dpapi.h). Запросы — под
// замком StateDb::mutex: with(fn) держит его на время fn. Диалоги «Удостоверения» и подписания — 4.6.
#pragma once

#include <QString>

#include <mutex>
#include <type_traits>
#include <utility>

#include "core/sign/identity.h"
#include "services/dpapi.h"

namespace ot {

class StateDb;

class Identities {
public:
    // Хранилище процесса: StateDb::shared(), app::dataDir()/ids.
    static Identities& shared();

    Identities(StateDb& db, const QString& idsDir);
    Identities(const Identities&) = delete;
    Identities& operator=(const Identities&) = delete;

    template <class F>
    auto with(F&& fn) -> std::invoke_result_t<F&, sign::IdentityStore&> {
        const std::lock_guard lock(mutex());
        return fn(store_);
    }

private:
    std::mutex& mutex();

    StateDb& db_;
    DpapiProtector protector_;
    sign::IdentityStore store_;
};

}  // namespace ot
