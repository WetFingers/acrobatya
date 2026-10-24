// Строки, абзацы и блоки редактора текста (эталон 3.9 «Модель», план 3.1; закрывает edit-01 — модель блоков)
// поверх ContentModel: спаны → строки (по базовой линии и углу; Ts не мешает — строки по линии без подъёма) →
// абзацы (межстрочный интервал больше 1,8 кегля или заметно больше интервала абзаца, отступ первой строки,
// выступ, маркер списка, смена кегля) с колонками (строка присоединяется к абзацу, с которым пересекается по
// X) → блоки с прямоугольником, выравниванием (по разбросу левых и правых краёв, растяжке пробелов Tw/TJ) и
// стилями пробегов (шрифт, кегль, цвет, Tr, Ts). Текст в нескольких BT/ET одного абзаца объединяется.
// Повёрнутый и вертикальный текст — по строке на блок, «только чтение»; текст формы не смешивается с текстом
// страницы; текст выключенного слоя в блоки не входит. Блок «только чтение», если хоть один его спан нельзя
// править (причина — самого важного спана).
//
// Раскладка считается в пространстве пользователя страницы (y вниз) — на странице с /Rotate абзацы те же;
// рамки и точки блоков — координаты страницы MuPDF, как у TextPage.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/edit/content_model.h"
#include "core/util/geometry.h"

namespace ot::edit {

enum class Align : std::uint8_t { Left, Right, Center, Justify };

// Код выравнивания для журнала, тестов и ожиданий JSON: "left", "right", "center", "justify".
const char* alignCode(Align align) noexcept;

// Пробег одного стиля: подряд идущие спаны блока с тем же шрифтом, кеглем, цветом, Tr и Ts.
struct StyleRun {
    int font = -1;
    float size = 0;
    std::uint32_t fill = 0xFF000000;
    int render = 0;
    float rise = 0;
    std::vector<int> spans;
    std::string text;  // UTF-8
};

struct BlockLine {
    std::vector<int> spans;  // слева направо
    RectF bbox;              // рамки видимых глифов (без пробелов), координаты страницы
    PointF start;            // начало базовой линии (первый глиф, без Ts), координаты страницы
    PointF end;              // конец последнего глифа на базовой линии
    float baseline = 0;      // y базовой линии на странице (без /Rotate — y точки start)
    float size = 0;          // кегль строки (самого длинного спана), пт страницы
    float stretch = 0;       // растяжка: средний сдвиг сверх ширины пробела (Tw, TJ) на пробел, доли кегля
    // Раскладка (пространство пользователя, y вниз): x начала и конца, базовая линия.
    float x0 = 0, x1 = 0, y = 0;
};

struct TextBlock {
    std::vector<BlockLine> lines;  // сверху вниз
    std::vector<int> spans;        // порядок чтения: строки, в строке — слева направо
    std::vector<StyleRun> runs;
    std::string text;  // строки через пробел (после мягкого или обычного дефиса — без пробела)
    Align align = Align::Left;
    float firstIndent = 0;  // отступ первой строки от левого края блока (раскладка)
    float leading = 0;      // межстрочный интервал (медиана), 0 — одна строка
    float fontSize = 0;
    int column = 0;  // номер колонки страницы слева направо (по пересечению блоков по X)
    bool horizontal = true;
    RectF bbox;  // координаты страницы
    ReadOnly reason = ReadOnly::None;

    bool editable() const noexcept { return reason == ReadOnly::None; }
    const RectF& boundingRect() const noexcept { return bbox; }
};

struct BlockOptions {
    float paragraphGap = 1.8f;  // интервал больше стольких кеглей — новый абзац (эталон, план 3.1)
    float leadingJump = 1.4f;   // интервал больше стольких межстрочных интервалов абзаца — новый абзац
    float lineGap = 1.5f;       // разрыв по X внутри строки больше стольких кеглей — разные строки (колонки)
};

// Блоки страницы модели в порядке чтения: колонки слева направо, в колонке — сверху вниз (горизонтальные),
// затем повёрнутые.
std::vector<TextBlock> blocksForPage(const ContentModel& model, const BlockOptions& options = {});

}  // namespace ot::edit
