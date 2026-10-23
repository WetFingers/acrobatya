// Именованные назначения (план 2.4; эталон 3.1 «Навигация», 3.13; view-50): панель «Назначения» — список и
// переход. Источники — дерево /Names /Dests (PDF 1.2+) и словарь /Dests каталога (PDF 1.1); одно имя в обоих
// — одна строка (как у MuPDF при переходе: сначала /Dests каталога). Назначение разрешается так же, как у
// ссылок (fz_resolve_link_dest: страница, вид, точка, масштаб). Переименование и запись — сессия 7.5.
#pragma once

#include <string>
#include <vector>

#include "core/links/links.h"

namespace ot {

class Document;

namespace dests {

struct Destination {
    std::string name;          // имя (UTF-8 — как байты имени в файле)
    links::LinkAction action;  // GoTo: page (−1 — не разрешилось), fit, point, zoom
};

// Все именованные назначения, по имени (байтово). Не PDF — пусто.
std::vector<Destination> list(Document& doc);

}  // namespace dests
}  // namespace ot
