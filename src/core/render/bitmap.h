// Растр в памяти без Qt: RGB (n = 3) или серый (n = 1), строки подряд с шагом stride.
// Ввод и вывод PNG — через MuPDF (пути — std::filesystem::path, кириллица допустима).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ot {

class MuContext;

struct Bitmap {
    int width = 0;
    int height = 0;
    int components = 3;  // 3 — RGB, 1 — серый
    int stride = 0;      // байт на строку
    std::vector<std::uint8_t> pixels;

    bool empty() const noexcept { return width <= 0 || height <= 0 || pixels.empty(); }
    const std::uint8_t* row(int y) const {
        return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(stride);
    }
    std::uint8_t* row(int y) { return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(stride); }
};

// PNG → RGB-растр (альфа отбрасывается на белом фоне). Бросает MuError / runtime_error.
Bitmap loadPng(MuContext& mu, const std::filesystem::path& path);
// RGB- или серый растр → байты PNG.
std::string encodePng(MuContext& mu, const Bitmap& bitmap);
void savePng(MuContext& mu, const Bitmap& bitmap, const std::filesystem::path& path);

}  // namespace ot
