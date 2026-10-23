// Слои — необязательное содержимое /OCProperties (план 2.4; эталон 3.1 «Навигация»; view-15): строки панели
// «Слои» по конфигурации /D (порядок /Order, вложенность, группы-подписи, выключенные /OFF, заблокированные
// /Locked), включение и выключение («глаз»), «Сбросить к исходному» (снова /D). Состояние слоёв — состояние
// просмотра, а не правка файла: в журнал не идёт и не сохраняется (как в Acrobat без «Сохранить видимость»).
// Рендер и текстовый слой зависят от состояния — после смены display lists страницы строятся заново, а в ключ
// тайла идёт stateHash (TileKey::layersHash). Печать и экспорт по /Print и /Export — сессия 2.6.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ot {

class Document;

namespace layers {

enum class UiKind { Label, Checkbox, Radio };

struct LayerUi {
    int index = 0;     // номер строки (pdf_layer_config_ui) — для toggle/setOn
    std::string text;  // имя слоя или подпись группы (UTF-8)
    int depth = 0;     // вложенность по /Order
    UiKind kind = UiKind::Checkbox;
    bool on = false;
    bool locked = false;
};

// Есть ли слои (OCG) у документа. Не PDF — нет.
bool hasLayers(Document& doc);
// Строки панели в порядке /Order текущей конфигурации.
std::vector<LayerUi> list(Document& doc);
// Переключить / включить / выключить строку; заблокированная и подпись группы не меняются (false).
bool toggle(Document& doc, int index);
bool setOn(Document& doc, int index, bool on);
// Вернуть состояние конфигурации /D («Сбросить к исходному»).
void reset(Document& doc);
// Отпечаток видимости всех слоёв (FNV-1a по включённым); без слоёв — 0.
std::uint64_t stateHash(Document& doc);

}  // namespace layers
}  // namespace ot
