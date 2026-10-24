// Общие значения редактора текста без MuPDF (план 3.3): их видят services и ui через
// core/edit/paragraph_editor.h, не включая модель контент-потока.
#pragma once

#include <cstdint>

namespace ot::edit {

enum class Align : std::uint8_t { Left, Right, Center, Justify };

// Код выравнивания для журнала, тестов и ожиданий JSON: "left", "right", "center", "justify".
const char* alignCode(Align align) noexcept;

}  // namespace ot::edit
