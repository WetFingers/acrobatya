// Зонд устойчивости для engine.* (эталон 9.2, 9.5): открыть файл → страница 1 в 32 × 32 → закрыть.
// Ни одно исключение не выходит наружу: результат — флаги и DocError. Ожидания корпуса — поле "expect"
// записи <корпус>/index.json (tools/fetch_corpus.py): open, password:<p>, broken-ok, sig:…, data.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"

namespace ot::test {

struct ProbeResult {
    bool opened = false;         // OpenResult.ok (с паролем, если задан)
    bool needsPassword = false;  // пароль не задан или не подошёл
    bool repaired = false;
    bool rendered = false;  // страница 1 нарисована в 32 × 32
    int pages = 0;
    DocError error;  // первая ошибка (открытие или рендер)
    double ms = 0;   // время зонда целиком
};

ProbeResult probeFile(const std::filesystem::path& path, const std::string& password = {});

// Коды, допустимые для битого файла: FZ_ERROR_FORMAT, SYNTAX, LIMIT, UNSUPPORTED (план 1.1).
bool isBrokenFileCode(int fzCode) noexcept;

// Журнал MuPDF на время прогона тысяч битых файлов — только ошибки выше порога (по умолчанию — ничего).
class QuietLog {
public:
    explicit QuietLog(spdlog::level::level_enum level = spdlog::level::off) : saved_(spdlog::get_level()) {
        spdlog::set_level(level);
    }
    ~QuietLog() { spdlog::set_level(saved_); }
    QuietLog(const QuietLog&) = delete;
    QuietLog& operator=(const QuietLog&) = delete;

private:
    spdlog::level::level_enum saved_;
};

// Каталог корпусов: OTTISK_CORPUS или %LOCALAPPDATA%\ottisk\corpus.
std::filesystem::path corpusRoot();

}  // namespace ot::test
