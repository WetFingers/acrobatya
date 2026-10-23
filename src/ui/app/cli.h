// Аргументы командной строки AcroBatya.exe (эталон 3.18; этап 0 — служебные режимы).
//   --version                 версия и выход
//   --self-check              самопроверка без окна (QCoreApplication), код 0 — всё в порядке
//   --shots <имя[,имя]|all>   снимки сцен в PNG без экрана (--out <каталог>, --size WxH, --theme light|dark)
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
    QString theme = QStringLiteral("light");
    int smokeSeconds = 0;
    QStringList files;
    QString error;  // непустая — ошибка разбора
};

CliOptions parseCli(const QStringList& arguments);  // arguments[0] — путь программы

// Режимы без окна — проверяются по сырому argv до создания QApplication.
bool hasArg(int argc, char** argv, const char* name);

}  // namespace ot::ui
