#include "core/render/render_style.h"

#include <algorithm>

namespace ot {

namespace {

// Порог «чёрного» и «белого» для onlyBlack (доля 0–1 по каждому каналу).
constexpr float kBlackMax = 0.12f;
constexpr float kWhiteMin = 0.92f;

void unpack(std::uint32_t rgb, float out[3]) noexcept {
    out[0] = static_cast<float>((rgb >> 16) & 0xFF) / 255.0f;
    out[1] = static_cast<float>((rgb >> 8) & 0xFF) / 255.0f;
    out[2] = static_cast<float>(rgb & 0xFF) / 255.0f;
}

std::uint64_t aaBits(int aa) noexcept {
    return aa < 0 ? 0u : static_cast<std::uint64_t>(std::min(aa, 8) + 1);
}

}  // namespace

void recolorRgb(const Recolor& rc, const float rgb[3], float out[3]) noexcept {
    if (rc.mode != RecolorMode::Replace) {
        std::copy(rgb, rgb + 3, out);
        return;
    }
    float text[3];
    float back[3];
    unpack(rc.text, text);
    unpack(rc.background, back);
    if (rc.onlyBlack) {
        const float hi = std::max({rgb[0], rgb[1], rgb[2]});
        const float lo = std::min({rgb[0], rgb[1], rgb[2]});
        const float* to = hi <= kBlackMax ? text : lo >= kWhiteMin ? back : rgb;
        std::copy(to, to + 3, out);
        return;
    }
    const float luma = std::clamp(0.299f * rgb[0] + 0.587f * rgb[1] + 0.114f * rgb[2], 0.0f, 1.0f);
    for (int i = 0; i < 3; ++i) {
        out[i] = text[i] + (back[i] - text[i]) * luma;
    }
}

bool simulateOverprint(OverprintPreview mode, bool pdfx) noexcept {
    switch (mode) {
        case OverprintPreview::Always:
            return true;
        case OverprintPreview::PdfX:
            return pdfx;
        case OverprintPreview::Never:
            break;
    }
    return false;
}

OverprintPreview overprintPreviewFromName(std::string_view name) noexcept {
    if (name == "always") {
        return OverprintPreview::Always;
    }
    if (name == "never") {
        return OverprintPreview::Never;
    }
    return OverprintPreview::PdfX;
}

std::uint64_t RenderStyle::key() const noexcept {
    std::uint64_t k = static_cast<std::uint64_t>(recolor.mode) & 0x3u;
    if (recolor.mode == RecolorMode::Replace) {
        k |= static_cast<std::uint64_t>(recolor.text & 0xFFFFFFu) << 2;
        k |= static_cast<std::uint64_t>(recolor.background & 0xFFFFFFu) << 26;
        k |= static_cast<std::uint64_t>(!recolor.lineArt) << 50;
        k |= static_cast<std::uint64_t>(recolor.onlyBlack) << 51;
    }
    k |= aaBits(textAa) << 52;
    k |= aaBits(graphicsAa) << 56;
    k |= static_cast<std::uint64_t>(!smoothImages) << 60;
    k |= static_cast<std::uint64_t>(thinLines) << 61;
    k |= static_cast<std::uint64_t>(overprint) << 62;
    return k;
}

}  // namespace ot
