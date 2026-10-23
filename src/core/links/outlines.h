// Закладки документа (план 2.4; эталон 3.1 «Навигация», 6.1 «Правая панель»; view-11): чтение уровнями — для
// ленивого дерева панели «Закладки» (первый уровень PDF32000_2008 — быстро, дочерние — по раскрытию).
// Обход — итератор MuPDF (fz_new_outline_iterator): он читает /Outlines по месту, без построения всего дерева
// (fz_load_outline разбирал бы тысячи закладок спецификации сразу). Узел задаётся путём — номерами от корня.
// Наружу — значения: заголовок, действие (назначение разрешено, как у ссылок: страница, точка, масштаб;
// внешний адрес — Uri, другой файл — GoToR), стиль /F (жирный, курсив), цвет /C, «открыта» (/Count > 0) и
// есть ли дети. Запись закладок — сессия 7.5.
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/links/links.h"

namespace ot {

class Document;

namespace outline {

struct Item {
    std::string title;         // UTF-8; пустая — как в файле
    links::LinkAction action;  // GoTo — страница и вид; Uri / GoToR — решает окно; нет назначения — page −1
    bool bold = false;         // /F бит 2
    bool italic = false;       // /F бит 1
    std::optional<std::array<std::uint8_t, 3>> color;  // /C (RGB 0–255); чёрный и без /C — нет (цвет темы)
    bool open = false;                                 // /Count > 0 — раскрыта при открытии
    bool hasChildren = false;
    std::vector<int> path;  // номера от корня: {2, 0} — первая дочерняя третьей закладки верхнего уровня
};

// Есть ли у документа закладки.
bool hasOutline(Document& doc);

// Дочерние закладки узла path (пусто — верхний уровень) по порядку файла. Нет такого узла — пусто. Ошибка
// разбора — MuError.
std::vector<Item> children(Document& doc, const std::vector<int>& path = {});

}  // namespace outline
}  // namespace ot
