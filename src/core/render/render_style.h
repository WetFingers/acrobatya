// Как рисовать страницу (план 2.5b; эталон 3.1 «Сглаживание и качество», 3.16 «Замена цветов»; view-36,
// view-26, ocr-38, pro-47) — значения без MuPDF: их носят TileRequest (services) и вид страниц (ui), рисует
// renderDisplayList (renderer.h), перекрашивает recolor_device.h.
//   * сглаживание текста и векторов — fz_set_text_aa_level / fz_set_graphics_aa_level, картинок — иначе
//     FZ_DONT_INTERPOLATE_IMAGES; «утолщать тонкие линии» — fz_set_graphics_min_line_width 1 px;
//   * симуляция наложения красок — растр с fz_separations, только у страниц, где overprint есть
//     (DisplayList::usesOverprint); режим «Никогда / Всегда / Только для PDF/X» решает simulateOverprint;
//   * замена цветов: текст и векторы — по яркости между парой «текст / фон» (чёрное → текст, белое → фон),
//     бумага — цветом фона, картинки не трогаются; «Изменять только чёрный текст и графику» (onlyBlack),
//     «без векторной графики» (lineArt = false); «Инвертировать всё» — инверсия растра, картинки тоже.
// Пара цветов — из настроек и токенов ui (ночь, высокая контрастность, свои); ядро получает их числами.
#pragma once

#include <cstdint>
#include <string_view>

namespace ot {

enum class RecolorMode : std::uint8_t { None = 0, Replace = 1, InvertAll = 2 };

struct Recolor {
    RecolorMode mode = RecolorMode::None;
    std::uint32_t text = 0x000000;        // 0xRRGGBB — цвет текста (и чёрного)
    std::uint32_t background = 0xFFFFFF;  // 0xRRGGBB — цвет фона (и белого, и бумаги)
    bool lineArt = true;                  // «Изменять цвет и векторной графики»
    bool onlyBlack = false;               // «Изменять только чёрный текст и графику»

    bool active() const noexcept { return mode != RecolorMode::None; }
    bool operator==(const Recolor&) const = default;
};

// Цвет rgb (0–1) после замены (только Replace; иначе — как был).
void recolorRgb(const Recolor& rc, const float rgb[3], float out[3]) noexcept;

// Предпросмотр наложения красок (Установки → Просмотр страницы): «Никогда», «Всегда», «Только для PDF/X»
// (OutputIntents /S /GTS_PDFX, Document::isPdfX) — по умолчанию.
enum class OverprintPreview { Never, Always, PdfX };
bool simulateOverprint(OverprintPreview mode, bool pdfx) noexcept;
// Значение ключа pageDisplay/overprintPreview: "never", "always", "pdfx"; прочее — PdfX.
OverprintPreview overprintPreviewFromName(std::string_view name) noexcept;

struct RenderStyle {
    int textAa = -1;      // 0–8 бит; −1 — как у контекста
    int graphicsAa = -1;  // 0–8 бит; −1 — как у контекста
    bool smoothImages = true;
    bool thinLines = false;
    bool overprint = false;  // симулировать наложение (если страница его использует)
    Recolor recolor;

    // Все поля в 64 битах без потерь — поле style ключа тайла (tiles.h). Стиль по умолчанию — 0; у
    // RecolorMode::None цвета не входят.
    std::uint64_t key() const noexcept;
    bool operator==(const RenderStyle&) const = default;
};

}  // namespace ot
