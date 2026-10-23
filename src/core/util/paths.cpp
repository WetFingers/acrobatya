#include "core/util/paths.h"

#include <windows.h>

#include <fstream>
#include <stdexcept>
#include <system_error>

namespace ot::paths {

std::wstring widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), n);
    return out;
}

std::string narrow(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int n = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0,
                                      nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

std::wstring toShortPath(const std::wstring& path) {
    const DWORD n = GetShortPathNameW(path.c_str(), nullptr, 0);
    if (n == 0) {
        return path;
    }
    std::wstring out(n, L'\0');
    const DWORD written = GetShortPathNameW(path.c_str(), out.data(), n);
    if (written == 0 || written >= n) {
        return path;
    }
    out.resize(written);
    return out;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open " + toUtf8(path));
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFile(const std::filesystem::path& path, std::string_view data) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("cannot write " + toUtf8(path));
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::runtime_error("write failed " + toUtf8(path));
    }
}

}  // namespace ot::paths
