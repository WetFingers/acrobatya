// Аргументы командной строки AcroBatya.exe (эталон 3.18; этап 0 — служебные режимы).
//   --version                 версия и выход
//   --self-check              самопроверка без окна (QCoreApplication), код 0 — всё в порядке
//   --shots <имя[,имя]|all>   снимки сцен в PNG без экрана (--out <каталог>, --size WxH)
//   --theme system|light|dark|contrast   тема (эталон 6.4; по умолчанию — системная, у снимков — светлая)
//   --scale 1|1.25            «Крупные элементы» (ocr-42)
//   --showcase                витрина компонентов (эталон 6.4) вместо главного окна
//   --smoke <секунды>         окно на N секунд и выход с кодом 0
//   --register-notify         SHChangeNotify(SHCNE_ASSOCCHANGED) после установщика
//   <файлы…>                  открыть
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
    QString shotsOut;   // каталог для PNG
    QSize shotsSize{1440, 900};
    QString theme;       // пусто — по умолчанию (окно — system, снимки — light)
    double scale = 1.0;  // 1 или 1,25
    bool showcase = false;
    int smokeSeconds = 0;
    QStringList files;
    QString error;  // непустая — ошибка разбора
};

CliOptions parseCli(const QStringList& arguments);  // arguments[0] — путь программы

// Режимы без окна — проверяются по сырому argv до создания QApplication.
bool hasArg(int argc, char** argv, const char* name);

}  // namespace ot::ui
