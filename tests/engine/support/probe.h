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
    DocError error;         // первая ошибка (открытие или рендер)
    bool timedOut = false;  // рендер прерван по сроку (error — Aborted)
    double ms = 0;          // время зонда целиком
};

// Рендер страницы 1 — со сроком OTTISK_PROBE_RENDER_TIMEOUT_S (по умолчанию 15 с, под ASan 45 с; 0 — без
// срока): сторожевой поток ставит fz_cookie.abort. Файлы-фаззы бывают «медленными» и в самом MuPDF (`mutool
// draw` на mupdf-fuzz/02af972f… — минуты в FreeType на гигантских глифах); в программе такой рендер снимает
// отмена.
ProbeResult probeFile(const std::filesystem::path& path, const std::string& password = {});

// Коды, допустимые для битого файла: FZ_ERROR_FORMAT, SYNTAX, LIMIT, UNSUPPORTED (план 1.1), а также
// LIBRARY (декодеры JPEG/JPX/JBIG2 на битой картинке) и GENERIC (испорченное содержимое страницы, например
// «CTM CHANGE IN CLIP TEXT!!!») — так отвечает сам MuPDF (`mutool draw`) на файлах-фаззах корпуса.
// Ошибки нашего кода (код 0), ввода-вывода (SYSTEM) и вызова (ARGUMENT) сюда не входят.
bool isBrokenFileCode(int fzCode) noexcept;

// Нехватка памяти: FZ_ERROR_SYSTEM «malloc (…) failed» — под пределом engine_tests (support/memory_cap.cpp).
bool isMemoryFailure(const DocError& e) noexcept;

// Переменная окружения задана, не пуста и не «0».
bool envFlag(const char* name);

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

// Каталог корпусов: OTTISK_CORPUS или C:\Tools\ottisk\corpus.
std::filesystem::path corpusRoot();

// Файл записи индекса корпуса: "repo:<путь>" — от корня исходников; иначе <corpusRoot()>/<rel>, если он есть
// (корпус могли перенести — абсолютный "path" индекса указывает на старое место), иначе "path" как есть.
std::filesystem::path corpusFile(const std::string& path, const std::string& rel);

}  // namespace ot::test
