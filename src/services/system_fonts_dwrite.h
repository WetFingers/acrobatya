// Системные шрифты через DirectWrite (план 3.2, п. 2; эталон 3.9 «FontResolver», 5.1 — Windows API в
// services): реализация edit::SystemFonts поверх системной коллекции шрифтов (IDWriteFactory3: шрифты Windows
// и установленные пользователем). Семейство — FindFamilyName + GetFirstMatchingFont (вес, курсив; грани нет —
// DWRITE_FONT_SIMULATIONS → simulatedBold/simulatedItalic), имя грани — IDWriteFontSet по PostScript-имени,
// затем по полному; путь к файлу — IDWriteLocalFontFileLoader, грань коллекции — IDWriteFontFace::GetIndex.
//
// Фабрика общая (DWRITE_FACTORY_TYPE_SHARED): CoInitialize ей не нужен, объекты свободнопоточные; вызовы
// всё равно идут под своим mutex — резолвер шрифтов зовёт их из потока документа.
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/edit/system_fonts.h"

namespace ot {

class DWriteSystemFonts final : public edit::SystemFonts {
public:
    DWriteSystemFonts();
    ~DWriteSystemFonts() override;
    DWriteSystemFonts(const DWriteSystemFonts&) = delete;
    DWriteSystemFonts& operator=(const DWriteSystemFonts&) = delete;

    // DirectWrite создан (Windows 10 и новее).
    bool available() const noexcept;

    std::optional<edit::SystemFace> face(std::string_view family, int weight, bool italic) override;
    std::optional<edit::SystemFace> byName(std::string_view name) override;
    std::vector<std::string> families() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
    std::mutex mutex_;
};

// Системные шрифты процесса (DirectWrite, создаётся при первом вызове).
edit::SystemFonts& systemFonts();

}  // namespace ot
