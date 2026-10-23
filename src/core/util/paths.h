// Единственная точка конверсий путей и строк (эталон 5.6): UTF-8 ↔ UTF-16 через WinAPI,
// std::filesystem::path — только из std::wstring (конструктор из std::string трактует байты в ACP 1252).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ot::paths {

std::wstring widen(std::string_view utf8);
std::string narrow(std::wstring_view wide);

inline std::filesystem::path fromUtf8(std::string_view utf8) { return std::filesystem::path(widen(utf8)); }
inline std::string toUtf8(const std::filesystem::path& p) { return narrow(p.native()); }

// Короткий путь 8.3 (GetShortPathNameW) — для библиотек с узким fopen (Tesseract) и профиля LibreOffice.
// Если короткие имена выключены на томе, возвращает исходный путь.
std::wstring toShortPath(const std::wstring& path);

// Прочитать файл целиком (путь — std::filesystem::path, без ACP).
std::string readFile(const std::filesystem::path& path);
// Записать файл целиком, создав каталоги.
void writeFile(const std::filesystem::path& path, std::string_view data);

}  // namespace ot::paths
