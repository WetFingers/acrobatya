// qpdf.exe как внешний инструмент (эталон 3.17, 4.4): «Быстрый просмотр в Интернете» — линеаризация поверх
// нашей записи (do_linear MuPDF не используется никогда), проверка структуры (qpdf --check), JSON объектов
// для тестов. qpdf — portable-программа, не библиотека (стек CLAUDE.md). Поиск:
// OTTISK_TOOLS\qpdf\bin\qpdf.exe (и OTTISK_TOOLS\qpdf.exe), рядом с программой (qpdf.exe, qpdf\bin\qpdf.exe —
// так кладёт установщик), C:\Tools\qpdf\bin\qpdf.exe. Процесс — без окна (CREATE_NO_WINDOW), вывод
// stdout+stderr, срок с остановкой.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ot {

struct ToolResult {
    int exitCode = -1;   // код процесса; −1 — не запустился, −2 — срок вышел (процесс остановлен)
    std::string output;  // stdout и stderr (UTF-8 как пишет qpdf)
    bool ok() const noexcept { return exitCode == 0; }
};

class QpdfTool {
public:
    // Путь к qpdf.exe по правилам выше; пусто — не найден.
    static std::filesystem::path find();

    explicit QpdfTool(std::filesystem::path exe = find());

    bool available() const noexcept { return !exe_.empty(); }
    const std::filesystem::path& exe() const noexcept { return exe_; }

    // qpdf --check: 0 — чисто, 2 — ошибки, 3 — предупреждения.
    ToolResult check(const std::filesystem::path& file, const std::string& password = {}) const;
    // qpdf --linearize in out (шифрование сохраняется).
    ToolResult linearize(const std::filesystem::path& in, const std::filesystem::path& out,
                         const std::string& password = {}) const;
    // qpdf --check-linearization: 0 — линеаризован без ошибок.
    ToolResult checkLinearization(const std::filesystem::path& file, const std::string& password = {}) const;
    // qpdf --json=2 [--json-key=key] — JSON в output.
    ToolResult json(const std::filesystem::path& file, const std::string& key = {},
                    const std::string& password = {}) const;

    // Запустить qpdf с аргументами (UTF-16), срок в мс.
    ToolResult run(const std::vector<std::wstring>& args, int timeoutMs) const;

private:
    std::filesystem::path exe_;
};

}  // namespace ot
