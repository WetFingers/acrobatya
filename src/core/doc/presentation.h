// Показ страниц в полноэкранном режиме (план 2.4; эталон 3.1 «Режимы»; view-22): переход /Trans и время
// показа /Dur страницы — как их читает MuPDF (pdf_page_presentation: /S, /D — длительность, по умолчанию 1 с,
// /Dm, /M, /Di), но по словарю страницы, без её загрузки: полный экран берёт переходы всех страниц сразу. Не
// PDF — без переходов. Запись переходов («Упорядочить → Переходы») — сессия 7.1.
#pragma once

#include <string_view>
#include <vector>

namespace ot {

class Document;

namespace presentation {

enum class TransitionType {
    None,
    Split,
    Blinds,
    Box,
    Wipe,
    Dissolve,
    Glitter,
    Fly,
    Push,
    Cover,
    Uncover,
    Fade
};

struct PageTransition {
    TransitionType type = TransitionType::None;
    float duration = 1.0f;   // /D, секунды перехода
    bool vertical = true;    // /Dm: V (по умолчанию) / H — Split, Blinds
    bool outwards = true;    // /M: O (по умолчанию) / I — Split, Box
    int direction = 0;       // /Di, градусы: 0 — слева направо, 90 — снизу вверх, 270 — сверху вниз
    float pageDuration = 0;  // /Dur — сколько показывать страницу, секунды; 0 — нет
};

std::string_view transitionName(TransitionType type) noexcept;

PageTransition pageTransition(Document& doc, int page);
// Переходы всех страниц по порядку.
std::vector<PageTransition> transitions(Document& doc);

}  // namespace presentation
}  // namespace ot
