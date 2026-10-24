// «Пустая правка» страницы — метрика стабильности редактора (эталон 3.9 «Стабильность», 11 риск 1; план 3.1):
// модель страницы → блоки → каждый редактируемый абзац пересобран без изменений
// (ContentModel::rebuildParagraph) → потоки записаны в документ (ContentModel::apply, операция журнала) →
// fz_stext до и после равны по тексту и рамкам (±0,5 pt), рендер до и после — в порогах imgcmp
// (FuzzyThreshold по умолчанию) → undo возвращает потоки байт в байт. Страница без редактируемых блоков
// ничего не пишет и считается стабильной. Поток документа.
#pragma once

#include <map>
#include <string>

#include "core/render/image_compare.h"

namespace ot {
class Document;
}

namespace ot::edit {

struct EmptyEditOptions {
    float dpi = 72.0f;          // растр сравнения
    float boxTolerance = 0.5f;  // рамки символов fz_stext до/после, пт
    bool checkUndo = true;      // отменить правку и сверить потоки с исходными
};

struct EmptyEditReport {
    int page = 0;
    int blocks = 0;    // всех блоков страницы
    int editable = 0;  // из них редактируемых
    int rebuilt = 0;   // пересобрано
    int ops = 0;       // заменённых операторов показа
    int streams = 0;   // записанных потоков
    bool textSame = true;
    bool boxesSame = true;
    float maxBoxDelta = 0;
    bool renderSame = true;
    CompareResult render;
    bool undoSame = true;
    std::map<std::string, int> readOnly;  // код причины → блоков «только чтение»
    std::string problem;                  // первая найденная разница (журнал, тесты)

    bool stable() const noexcept { return textSame && boxesSame && renderSame && undoSame; }
};

// Пустая правка страницы page. Ошибки MuPDF — MuError (страница не загружается и т. п.).
EmptyEditReport emptyEditPage(Document& doc, int page, const EmptyEditOptions& options = {});

}  // namespace ot::edit
