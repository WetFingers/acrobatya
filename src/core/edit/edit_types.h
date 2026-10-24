// Общие значения редактора текста без MuPDF (план 3.3): их видят services и ui через
// core/edit/paragraph_editor.h, не включая модель контент-потока.
#pragma once

#include <cstdint>
#include <string>

namespace ot::edit {

enum class Align : std::uint8_t { Left, Right, Center, Justify };

// Код выравнивания для журнала, тестов и ожиданий JSON: "left", "right", "center", "justify".
const char* alignCode(Align align) noexcept;

// Запасные шрифты (edit-49: «Шрифт подстановки» в Установках → Редактирование; core/edit/font_resolver.h).
struct FallbackFonts {
    std::string family;  // выбран человеком; пусто — «автоматически» (по засечкам шрифта пробега)
    std::string sans = "Segoe UI";
    std::string serif = "Times New Roman";
    std::string mono = "Courier New";
    std::string symbol = "Segoe UI Symbol";  // символы, которых нет в запасном
};

// Сообщения о шрифтах набора (core/edit/font_resolver.h).
enum class FontNoticeKind : std::uint8_t {
    SubsetExtended,  // правило 2: подмножество дополнено системным шрифтом
    Substituted,     // правило 3 или 4 с другим семейством: «шрифт подменён на …»
    Synthesized,     // начертания нет — синтетический жирный/курсив
    MissingGlyph,    // глифа нет ни в одном шрифте
};
const char* fontNoticeCode(FontNoticeKind kind) noexcept;

// Сообщение для человека (ui переводит по коду): одно на вид, шрифт и замену, символы копятся.
struct FontNotice {
    FontNoticeKind kind = FontNoticeKind::Substituted;
    std::string font;         // шрифт пробега (/BaseFont без префикса)
    std::string replacement;  // семейство, которым набрано
    std::u32string chars;     // символы, из-за которых
};

}  // namespace ot::edit
