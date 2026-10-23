// Сведения о программе и её каталогах (эталон 4.7, 10.5): версия, каталог данных — только ASCII
// %LOCALAPPDATA%\AcroBatya (или OTTISK_DATA_DIR — тесты и снимки), имя экземпляра (OTTISK_INSTANCE).
#pragma once

#include <QString>
#include <QStringList>

namespace ot::app {

QString version();       // 0.N.0 из cmake/product.cmake
QString mupdfVersion();  // FZ_VERSION
QString qtVersion();     // qVersion() — версия загруженных DLL Qt
QString dataDir();       // создаётся при первом обращении
QString settingsPath();  // <каталог данных>/settings.ini (QSettings IniFormat, не реестр; Settings — 2.4b)
QString instanceName();  // "AcroBatya" или "AcroBatya-<OTTISK_INSTANCE>"

// Самопроверка без окна (--self-check): строки «ключ: значение» и итог; true — всё в порядке.
bool selfCheck(QStringList& lines);

}  // namespace ot::app
