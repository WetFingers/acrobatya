// MuContext — владелец fz_context с общими замками (эталон 4.1, tech-facts 1.2).
// Правила MuPDF: один контекст — один поток одновременно; все контексты одного процесса делят один
// fz_locks_context; контекст для другого потока — clone() (общие allocator, store, locks).
// В MuPDF 1.28.5 fz_locks_context — три поля {user, lock, unlock}, FZ_LOCK_MAX = 3 нерекурсивных мьютекса.
//
// global() — один корневой контекст на процесс (FZ_STORE_DEFAULT = 256 МиБ, обработчики документов);
// документы и потоки рендера работают в его клонах. Предупреждения и ошибки MuPDF идут в spdlog
// (логгер по умолчанию, префикс «mupdf: »); клоны наследуют колбэки; колбэки не бросают.
#pragma once

#include <memory>
#include <string>

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class MuContext {
public:
    // Новый корневой контекст: замки, кэш FZ_STORE_DEFAULT, обработчики документов, журнал в spdlog.
    // В программе — только global(); отдельные корни — для тестов и инструментов.
    MuContext();
    ~MuContext();

    MuContext(const MuContext&) = delete;
    MuContext& operator=(const MuContext&) = delete;

    // Корневой контекст процесса (создаётся при первом вызове, потокобезопасно).
    static MuContext& global();

    fz_context* get() const noexcept { return ctx_; }

    // Клон для другого потока: общий кэш и замки, свой стек исключений, те же колбэки и настройки
    // (AA, ICC) на момент клонирования.
    std::unique_ptr<MuContext> clone() const;

    // Клон global() для текущего потока (thread_local, создаётся при первом вызове в потоке, отдаётся при
    // выходе потока): потоки RenderPool рисуют им display lists, им же отдаются fz_display_list и прочие
    // общие объекты там, где разрушается последняя ссылка.
    static MuContext& forThisThread();

    // «Мало памяти»: сжать общий store MuPDF (картинки, шрифты, тайлы MuPDF) до percent % его предела
    // (fz_shrink_store; то, что сейчас держат задачи, не трогается). true — уложились. Любой поток: store
    // общий, под замком.
    bool shrinkStore(unsigned int percent) const noexcept;

    // Версия MuPDF, с которой собрана программа (FZ_VERSION).
    static std::string version();

private:
    struct Locks;
    MuContext(fz_context* ctx, std::shared_ptr<Locks> locks);

    std::shared_ptr<Locks> locks_;
    fz_context* ctx_ = nullptr;
};

}  // namespace ot
