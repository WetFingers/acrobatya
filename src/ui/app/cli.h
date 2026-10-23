// Аргументы командной строки AcroBatya.exe (эталон 3.18; этап 0 — служебные режимы).
//   --version                 версия и выход
//   --self-check              самопроверка без окна и без QPA (QCoreApplication; services/app_info.h):
//   версия,
//                             пути, плагины Qt по файлам, MuPDF, tessdata загрузкой, qpdf, переводы,
//                             ассоциация; код 0 — всё в порядке
//   --open-all <папка>        открыть все *.pdf папки с подпапками (стр. 1 в 32 × 32) и закрыть, без окна;
//                             код = число сбоев нашего кода (битый файл — отказ, не сбой;
//                             services/open_probe.h); OTTISK_OPEN_ALL_TRACE=1 — печатать каждый файл до пробы
//                             (найти файл падения)
//   --shots <имя[,имя]|all>   снимки сцен в PNG без экрана (--out <каталог>, --size WxH; эталон 6.10)
//   --shots-list              имена сцен, по одному в строке
//   --commands-list           реестр команд окна одной строкой JSON (id, title, keys, when…) — страж
//                             unit.guard.hotkeys сверяет его с таблицей эталона 6.8
//   --lang ru|en|system       язык интерфейса (эталон 5.7; иначе — настройка, иначе — системный)
//   --theme system|light|dark|contrast   тема (эталон 6.4; по умолчанию — системная, у снимков — светлая)
//   --scale 1|1.25            «Крупные элементы» (ocr-42)
//   --showcase                витрина компонентов (эталон 6.4) вместо главного окна
//   --smoke <секунды>         окно на N секунд и выход с кодом 0
//   --script <файл.json>      шаги {"cmd", "args"} над окном (services/script_runner.h,
//   ui/app/script_actions.h):
//                             снимки shot — в --out; без --data-dir — свой временный каталог данных
//   --register-notify         SHChangeNotify(SHCNE_ASSOCCHANGED) после установщика (заглушка до 9.1: только
//                             уведомление Проводника; регистрация и проверка ассоциации —
//                             services/shell_register)
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
    bool commandsList = false;
    QString shotsOut;  // каталог для PNG
    QSize shotsSize{1440, 900};
    QString theme;            // пусто — по умолчанию (окно — system, снимки — light)
    double scale = 1.0;       // 1 или 1,25
    bool scaleGiven = false;  // --scale задан (иначе — настройка «Крупные элементы»)
    QString lang;             // пусто — из настройки
    bool showcase = false;
    int smokeSeconds = 0;
    QString script;    // --script <файл.json>
    QString openAll;   // --open-all <папка>
    int page = 0;      // --page N (с 1); 0 — не задана
    QString logLevel;  // пусто — по умолчанию
    QString dataDir;   // --data-dir
    QStringList files;
    QString error;  // непустая — ошибка разбора
};

CliOptions parseCli(const QStringList& arguments);  // arguments[0] — путь программы

// Режимы без окна — проверяются по сырому argv до создания QApplication.
bool hasArg(int argc, char** argv, const char* name);
// Есть ли среди argv режим без окна (--version, --self-check, --register-notify, --shots-list, --open-all):
// он идёт на QCoreApplication без платформенного плагина Qt.
bool isConsoleMode(int argc, char** argv);

// Режимы без окна; печатают в stdout (UTF-8), код — код выхода процесса.
int runSelfCheck();
int runRegisterNotify();

struct OpenAllReport {
    int files = 0;
    int opened = 0;
    int refused = 0;    // битые, с паролем, не поддерживаются, срок вышел — так и надо отвечать
    int failures = 0;   // сбои нашего кода
    QStringList lines;  // «FAIL путь: причина», «refused путь: причина»
};
// Пробы всех *.pdf папки с подпапками по алфавиту (services/open_probe.h); trace — каждый путь в stdout до
// пробы.
OpenAllReport openAll(const QString& folder, int timeoutMs = 20000, bool trace = false);
int runOpenAll(const QString& folder);  // печать отчёта и итога; код = failures (нет папки — 1)

}  // namespace ot::ui
