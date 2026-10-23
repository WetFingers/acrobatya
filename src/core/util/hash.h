// SHA-256 (OpenSSL EVP) — ключи автосохранения и сверка исходного файла (эталон 4.5).
#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace ot::hash {

// SHA-256 байтов — 64 строчные шестнадцатеричные цифры.
std::string sha256Hex(std::string_view bytes);
// SHA-256 файла потоком (путь UTF-16); нет файла — std::runtime_error.
std::string sha256File(const std::filesystem::path& path);

}  // namespace ot::hash
