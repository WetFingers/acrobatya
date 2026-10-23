// Логические номера страниц /PageLabels (план 2.4 — чтение и поле страницы строки состояния; запись — 7.1;
// эталон 3.1 «Навигация», 6.1; view-08): подписи всех страниц («i», «ii», «1»…, «A-1») и разбор ввода поля
// страницы — «iv» или «3»: сначала логический номер (точно, затем без учёта регистра латиницы — римские
// цифры), иначе число 1…N — физическая страница; «(3)» — всегда физическая (так Acrobat показывает её рядом:
// «iii (3 из 12)»).
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ot {

class Document;

namespace pages {

// Подписи всех страниц по порядку; у документа нет /PageLabels (или не PDF) — пусто: номера 1, 2, 3…
std::vector<std::string> pageLabels(Document& doc);

// Подпись страницы page (с нуля): логическая, если есть, иначе номер.
std::string pageLabel(const std::vector<std::string>& labels, int page);

// Ввод поля страницы → страница с нуля; nullopt — такой нет. useLabels = false — только физические номера.
std::optional<int> parsePageInput(const std::vector<std::string>& labels, std::string_view input,
                                  int pageCount, bool useLabels = true);

}  // namespace pages
}  // namespace ot
