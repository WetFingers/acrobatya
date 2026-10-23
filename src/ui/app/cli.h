// Аргументы командной строки AcroBatya.exe (эталон 3.18; этап 0 — служебные режимы).
//   --version                 версия и выход
//   --self-check              самопроверка без окна (QCoreApplication), код 0 — всё в порядке
//   --shots <имя[,имя]|all>   снимки сцен в PNG без экрана (--out <каталог>, --size WxH; эталон 6.10)
//   --shots-list              имена сцен, по одному в строке
//   --lang ru|en|system       язык интерфейса (эталон 5.7; иначе — настройка, иначе — системный)
//   --theme system|light|dark|contrast   тема (эталон 6.4; по умолчанию — системная, у снимков — светлая)
//   --scale 1|1.25            «Крупные элементы» (ocr-42)
//   --showcase                витрина компонентов (эталон 6.4) вместо главного окна
//   --smoke <секунды>         окно на N секунд и выход с кодом 0
//   --register-notify         SHChangeNotify(SHCNE_ASSOCCHANGED) после установщика
//   --page <N>                открыть файлы на странице N (с 1)
//   --log trace|debug|info|warn|error|off   уровень журнала
//   --data-dir <папка>        каталог данных вместо %LOCALAPPDATA%\AcroBatya (как OTTISK_DATA_DIR)
//   <файлы…>                  открыть (второй экземпляр передаёт их первому — services/single_instance.h)
#pragma once

#include <QSize>
#include <QString>
#include <QStringList>

namespace ot::ui {

struct CliOptions {
    bool version = false;
    bool selfCheck = false;
    bool registerNotify = false;
    QStringList shots;  // имена сцен; "all" — все
    bool shotsList = false;
    QString shotsOut;  // каталог для PNG
    QSize shotsSize{1440, 900};
    QString theme;       // пусто — по умолчанию (окно — system, снимки — light)
    double scale = 1.0;  // 1 или 1,25
    QString lang;        // пусто — из настройки
    bool showcase = false;
    int smokeSeconds = 0;
    int page = 0;      // --page N (с 1); 0 — не задана
    QString logLevel;  // пусто — по умолчанию
    QString dataDir;   // --data-dir
    QStringList files;
    QString error;  // непустая — ошибка разбора
};

CliOptions parseCli(const QStringList& arguments);  // arguments[0] — путь программы

// Режимы без окна — проверяются по сырому argv до создания QApplication.
bool hasArg(int argc, char** argv, const char* name);

}  // namespace ot::ui
