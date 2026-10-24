// Настройки создания подписей (план 4.6, п. 1, 3; эталон 3.8 «Диалог подписания», 3.18 «Подписи»; sign-13 —
// внешние виды, sign-14 — причина и место): внешние виды — список с именами в signatures/appearances (JSON:
// [{name, graphic: none|name|image, image, imageOnly, text: {name, dn, reason, location, date, labels}, logo,
// language}]), вид по умолчанию — signatures/appearance (имя). Пустой или испорченный JSON — пустой список:
// «Стандартный» вид (SignatureAppearance по умолчанию) и стандартные причины даёт интерфейс (строки —
// только в ui). Ярлыки строк вида (AppearanceLabels) не хранятся — их переводы подставляет интерфейс при
// подписи.
#pragma once

#include <QJsonObject>
#include <QString>

#include <vector>

#include "core/sign/appearance.h"

namespace ot {

class Settings;

struct NamedAppearance {
    QString name;
    sign::SignatureAppearance look;
};

QJsonObject appearanceToJson(const NamedAppearance& appearance);
// Чужие значения — умолчания SignatureAppearance; без имени — пустое имя (такой вид пропускается при чтении).
NamedAppearance appearanceFromJson(const QJsonObject& object);

std::vector<NamedAppearance> loadAppearances(const Settings& settings);
void saveAppearances(Settings& settings, const std::vector<NamedAppearance>& appearances);

}  // namespace ot
