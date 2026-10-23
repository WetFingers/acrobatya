// Язык интерфейса (эталон 5.7, основа cloud-60): переводы ottisk_<язык>.qm встроены ресурсом :/i18n
// (qt_add_translations с MERGE_QT_TRANSLATIONS — внутри и qtbase_<язык>: стандартные кнопки и диалоги Qt).
// Исходные строки интерфейса — русские в tr(); английский — ottisk_en.ts (сессия 9.2). Выбор: --lang, иначе
// настройка language/ui в settings.ini, иначе язык системы; смена во время работы — LanguageChange всем
// окнам.
#pragma once

#include <QString>
#include <QStringList>

namespace ot::i18n {

// Ключ настройки в settings.ini: system | ru | en (сессия 2.4b переносит его в settings_keys.h).
inline constexpr char kLanguageKey[] = "language/ui";

QStringList available();                            // "ru", "en"
QString resolve(const QString& requested);          // ru | en | system | пусто → ru | en
QString fromSettings(const QString& iniPath = {});  // по умолчанию — <каталог данных>/settings.ini
// Поставить переводчик языка (прежний снимается) и локаль по умолчанию; false — .qm не найден (строки —
// русские).
bool install(const QString& language);
QString current();

}  // namespace ot::i18n
