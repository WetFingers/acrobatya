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
QString settingsPath();  // <каталог данных>/settings.ini (Settings — services/settings.h, не реестр)
QString instanceName();  // "AcroBatya" или "AcroBatya-<OTTISK_INSTANCE>"

// Плагины Qt — по файлам (--self-check без QPA): каталоги поиска самого Qt (QCoreApplication::libraryPaths()
// — plugins/ по qt.conf установленной копии и каталог программы, куда сборка кладёт platforms/), обязательные
// — platforms/qwindows.dll и platforms/qoffscreen.dll; missingPlugins — чего нет ни в одном из dirs.
QStringList pluginsDirs();
QStringList requiredPlugins();
QStringList missingPlugins(const QStringList& dirs);
// Программа, которой Windows открывает .pdf (AssocQueryString), пусто — нет; сведения для --self-check.
QString associatedPdfProgram();

// Самопроверка без окна (--self-check, эталон 3.18; tests/build/install_test.py): строки «ключ: значение» —
// version, qt, mupdf, os, exe, data (пишется, ASCII), settings, plugins (по файлам), render (MuPDF), tessdata
// (загрузка rus), qpdf (--version), translations (:/i18n), association (сведения) — и итог «result: ok». true
// — все обязательные проверки прошли. Работает на QCoreApplication: QT_QPA_PLATFORM ей безразлична.
bool selfCheck(QStringList& lines);

}  // namespace ot::app
