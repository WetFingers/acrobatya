// Какие страницы печатать (план 2.6; эталон 3.3; view-37): «Все», «Текущая», «Страницы» — диапазон
// «1-3, 7, 12-» с логическими номерами /PageLabels, «Выделенные» (миниатюры, view-10; страницы закладки,
// view-11), затем «Чётные или нечётные» и «Обратный порядок». Чистая логика без MuPDF: подписи страниц —
// pages::pageLabels, разбор одного номера — pages::parsePageInput («iii», «3», «(3)» — физическая).
//
// Диапазон: части через «,» или «;» (пробелы вокруг не важны), часть — «N», «N-M», «N-» (до последней),
// «-M» (с первой); N > M — по убыванию. Сначала часть целиком как одна страница (подписи вида «A-1» с
// дефисом), иначе — разрез по дефису (первый, при котором обе стороны — страницы). Порядок и повторы —
// как в строке («3, 1, 1» печатает три страницы). Пустые части («1,») пропускаются; пустая строка, чужая
// страница или мусор — ошибка (nullopt).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ot::print {

enum class PageRange { All, Current, Custom, Selected };
// По физическому номеру с 1: нечётные — 1, 3, 5…, чётные — 2, 4, 6….
enum class PageParity { Both, Odd, Even };

struct PageSetOptions {
    PageRange range = PageRange::All;
    int current = 0;            // текущая страница с нуля (Current)
    std::string custom;         // «1-3, 7, 12-» (Custom)
    std::vector<int> selected;  // страницы с нуля (Selected) — как пришли (миниатюры — по возрастанию)
    PageParity parity = PageParity::Both;
    bool reverse = false;
    bool useLabels = true;  // логические номера в диапазоне (pageDisplay/useLogicalPageNumbers)
};

// Разобрать диапазон → страницы с нуля в порядке строки; nullopt — ошибка.
std::optional<std::vector<int>> parsePageRange(const std::vector<std::string>& labels, std::string_view text,
                                               int pageCount, bool useLabels = true);

// Страницы к печати по выбору; nullopt — диапазон не разобран. Пустой список — печатать нечего (кнопка
// «Печать» серая). Чужие номера Current/Selected отбрасываются.
std::optional<std::vector<int>> resolvePages(const PageSetOptions& options, int pageCount,
                                             const std::vector<std::string>& labels = {});

// Диапазон для поля «Страницы» по списку (выделенные миниатюры → «1-3, 7»): подряд идущие — через дефис,
// номера физические с 1 (или подписи, если labels не пусты и useLabels).
std::string formatPageRange(const std::vector<int>& pages, const std::vector<std::string>& labels = {},
                            bool useLabels = false);

}  // namespace ot::print
