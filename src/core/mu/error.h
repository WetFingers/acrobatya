// Ошибка MuPDF как исключение C++ (эталон 4.1). Код — FZ_ERROR_* (fz_caught), текст — fz_caught_message;
// вид ErrorKind — то, что видит интерфейс: FORMAT/SYNTAX → «Файл повреждён» («Открыть восстановленным»),
// UNSUPPORTED → «Не поддерживается», SYSTEM (OOM, ввод-вывод) → документ закрывается, остальные вкладки
// живут.
#pragma once

#include <stdexcept>
#include <string>

namespace ot {

enum class ErrorKind {
    None,
    NotFound,     // файла нет (проверка до MuPDF)
    Damaged,      // FZ_ERROR_FORMAT, FZ_ERROR_SYNTAX
    Unsupported,  // FZ_ERROR_UNSUPPORTED
    Limit,        // FZ_ERROR_LIMIT
    System,       // FZ_ERROR_SYSTEM: нехватка памяти, ошибка ввода-вывода
    Argument,     // FZ_ERROR_ARGUMENT: ошибка вызывающего кода
    Aborted,      // FZ_ERROR_ABORT, FZ_ERROR_TRYLATER: отмена через fz_cookie
    Other,        // FZ_ERROR_GENERIC, FZ_ERROR_LIBRARY и прочее
};

// Вид ошибки по коду FZ_ERROR_* (0 — None).
ErrorKind errorKind(int fzCode) noexcept;

class MuError : public std::runtime_error {
public:
    MuError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}

    // FZ_ERROR_SYSTEM, FZ_ERROR_LIBRARY, FZ_ERROR_ARGUMENT, FZ_ERROR_LIMIT, FZ_ERROR_UNSUPPORTED,
    // FZ_ERROR_FORMAT, FZ_ERROR_SYNTAX…
    int code() const noexcept { return code_; }
    ErrorKind kind() const noexcept { return errorKind(code_); }

    // Имя кода как у MuPDF ("format", "syntax", "limit"…) — для журнала и тестов.
    static const char* codeName(int fzCode) noexcept;

private:
    int code_;
};

}  // namespace ot
