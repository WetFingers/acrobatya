// Оболочка Windows (план 2.4; эталон 7.3, 7.4, 8.3 «Ничего живого»): единственная точка, откуда программа
// открывает что-то внешнее — адрес в браузере, файл программой по умолчанию (вложение после подтверждения),
// страницу «Параметры Windows» (ассоциации 9.1), Проводник с выделенным файлом. Прямых ShellExecute* и
// QDesktopServices::openUrl вне shell_launcher.cpp нет (страж unit.guard.boundaries): иначе offscreen-прогоны
// очереди откроют на машине человека Edge, Word и Параметры, а процессы повиснут.
//
// ShellLauncher::instance() — настоящий; тесты и сцены ставят FakeShellLauncher (записывает вызовы, ничего не
// открывает) через setInstance. Настоящий под offscreen и без QGuiApplication тоже ничего не открывает (false
// и строка в журнал) — страховка от забытого фейка. Подтверждения («Открыть ссылку…?», «Открыть вложение…?»)
// — дело вызывающего; здесь только исполнение.
#pragma once

#include <QList>
#include <QString>
#include <QUrl>

namespace ot {

class ShellLauncher {
public:
    virtual ~ShellLauncher() = default;

    // Адрес (http, https, mailto) — программой по умолчанию.
    virtual bool openUrl(const QUrl& url);
    // Локальный файл — программой, связанной с его типом.
    virtual bool openFile(const QString& path);
    // «Параметры Windows» на странице ms-settings:<page> (например, "defaultapps").
    virtual bool openSettings(const QString& page);
    // Проводник с выделенным файлом.
    virtual bool showInExplorer(const QString& path);

    // Текущий (по умолчанию — настоящий); setInstance(nullptr) — снова настоящий. Не владеет.
    static ShellLauncher& instance();
    static void setInstance(ShellLauncher* launcher);
    // Можно ли открывать внешнее: есть QGuiApplication и платформа не offscreen/minimal.
    static bool liveDesktop();
};

// Фейк для тестов и сцен: записывает вызовы, ничего не открывает, отвечает result.
class FakeShellLauncher : public ShellLauncher {
public:
    struct Call {
        QString kind;    // "openUrl", "openFile", "openSettings", "showInExplorer"
        QString target;  // адрес, путь или страница
    };

    bool openUrl(const QUrl& url) override;
    bool openFile(const QString& path) override;
    bool openSettings(const QString& page) override;
    bool showInExplorer(const QString& path) override;

    QList<Call> calls;
    bool result = true;

private:
    bool record(const QString& kind, const QString& target);
};

// Подмена на время области видимости (тесты): ставит фейк и возвращает прежний.
class ScopedShellLauncher {
public:
    explicit ScopedShellLauncher(ShellLauncher& launcher);
    ~ScopedShellLauncher();
    ScopedShellLauncher(const ScopedShellLauncher&) = delete;
    ScopedShellLauncher& operator=(const ScopedShellLauncher&) = delete;

private:
    ShellLauncher* previous_;
};

}  // namespace ot
