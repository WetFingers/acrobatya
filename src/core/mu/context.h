// MuContext — владелец fz_context с общими замками (эталон 4.1, tech-facts 1.2).
// Правила MuPDF: один контекст — один поток одновременно; все контексты одного процесса делят один
// fz_locks_context; контекст для другого потока — clone() (общие allocator, store, locks).
// В MuPDF 1.28.5 fz_locks_context — три поля {user, lock, unlock}, FZ_LOCK_MAX = 3 нерекурсивных мьютекса.
#pragma once

#include <memory>
#include <string>

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class MuContext {
public:
    // Новый корневой контекст: замки, кэш FZ_STORE_DEFAULT, зарегистрированные обработчики документов.
    MuContext();
    ~MuContext();

    MuContext(const MuContext&) = delete;
    MuContext& operator=(const MuContext&) = delete;

    fz_context* get() const noexcept { return ctx_; }

    // Клон для другого потока: общий кэш и замки, свой стек исключений.
    std::unique_ptr<MuContext> clone() const;

    // Версия MuPDF, с которой собрана программа (FZ_VERSION).
    static std::string version();

private:
    struct Locks;
    MuContext(fz_context* ctx, std::shared_ptr<Locks> locks);

    std::shared_ptr<Locks> locks_;
    fz_context* ctx_ = nullptr;
};

}  // namespace ot
