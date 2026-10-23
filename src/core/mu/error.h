// Ошибка MuPDF как исключение C++. Код — FZ_ERROR_* (fz_caught), текст — fz_caught_message.
#pragma once

#include <stdexcept>
#include <string>

namespace ot {

class MuError : public std::runtime_error {
public:
    MuError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}

    // FZ_ERROR_SYSTEM, FZ_ERROR_LIBRARY, FZ_ERROR_ARGUMENT, FZ_ERROR_LIMIT, FZ_ERROR_UNSUPPORTED,
    // FZ_ERROR_FORMAT, FZ_ERROR_SYNTAX…
    int code() const noexcept { return code_; }

private:
    int code_;
};

}  // namespace ot
