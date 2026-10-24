// Признак «похоже на скан» (план 3.4, п. 5; эталон 3.15: «страница = одна картинка ≥ 90 % площади и нет
// текста»; основа ocr-08 — полоса «Похоже на скан — распознать текст?»). Страница проигрывается в своё
// устройство MuPDF: текст — любой показ (и невидимый Tr 3 — слой OCR уже есть, распознавать нечего), картинки
// — рамки fill_image / fill_image_mask (единичный квадрат × CTM) в пределах страницы. Скан — текста нет, а
// самая большая картинка закрывает не меньше 90 % площади страницы. Всё — в потоке документа.
#pragma once

namespace ot {
class Document;
}

namespace ot::ocr {

inline constexpr float kScanCoverage = 0.9f;

struct ScanCheck {
    bool scan = false;     // текста нет, картинка ≥ 90 %
    bool hasText = false;  // хоть один глиф (в том числе невидимый)
    int images = 0;        // показов картинок на странице
    float coverage = 0;    // доля площади страницы под самой большой картинкой (0…1)
};

// Страница page документа doc; не PDF и ошибка рендера — не скан.
ScanCheck checkScan(const Document& doc, int page);
bool isScan(const Document& doc, int page);

}  // namespace ot::ocr
