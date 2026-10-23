#include "services/app_info.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QSysInfo>
#include <QtGlobal>

#include <windows.h>
#include <shlwapi.h>

#include <algorithm>
#include <exception>
#include <filesystem>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/ocr/tessdata.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/paths.h"

#pragma comment(lib, "shlwapi.lib")

namespace ot::app {

namespace {

QString native(const QString& path) { return QDir::toNativeSeparators(path); }

QString fromPath(const std::filesystem::path& p) { return QString::fromStdWString(p.wstring()); }

}  // namespace

QString version() { return QStringLiteral(OTTISK_VERSION_STR); }

QString mupdfVersion() { return QString::fromStdString(MuContext::version()); }

QString qtVersion() { return QString::fromLatin1(qVersion()); }

QString dataDir() {
    QString dir = qEnvironmentVariable("OTTISK_DATA_DIR");
    if (dir.isEmpty()) {
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        dir = local.isEmpty() ? QDir::home().filePath(QStringLiteral("AppData/Local/AcroBatya"))
                              : QDir(local).filePath(QStringLiteral("AcroBatya"));
    }
    QDir().mkpath(dir);
    return QDir::cleanPath(dir);
}

QString settingsPath() { return QDir(dataDir()).filePath(QStringLiteral("settings.ini")); }

QString instanceName() {
    const QString suffix = qEnvironmentVariable("OTTISK_INSTANCE");
    return suffix.isEmpty() ? QStringLiteral("AcroBatya") : QStringLiteral("AcroBatya-") + suffix;
}

QStringList requiredPlugins() {
    return {QStringLiteral("platforms/qwindows.dll"), QStringLiteral("platforms/qoffscreen.dll")};
}

QStringList missingPlugins(const QStringList& dirs) {
    QStringList missing;
    for (const QString& rel : requiredPlugins()) {
        const bool found = std::any_of(dirs.begin(), dirs.end(), [&](const QString& d) {
            return QFileInfo(QDir(d).filePath(rel)).isFile();
        });
        if (!found) {
            missing << rel;
        }
    }
    return missing;
}

QStringList pluginsDirs() {
    QStringList dirs = QCoreApplication::libraryPaths();
    const QString configured = QLibraryInfo::path(QLibraryInfo::PluginsPath);
    if (!dirs.contains(configured)) {
        dirs << configured;
    }
    for (QString& d : dirs) {
        d = QDir::cleanPath(d);
    }
    dirs.removeDuplicates();
    return dirs;
}

QString associatedPdfProgram() {
    wchar_t exe[MAX_PATH * 2] = {};
    DWORD size = static_cast<DWORD>(std::size(exe));
    if (FAILED(AssocQueryStringW(ASSOCF_NOTRUNCATE, ASSOCSTR_EXECUTABLE, L".pdf", L"open", exe, &size))) {
        return {};
    }
    return QDir::cleanPath(QString::fromWCharArray(exe));
}

bool selfCheck(QStringList& lines) {
    bool ok = true;
    auto check = [&](const QString& key, bool good, const QString& value) {
        lines << key + QStringLiteral(": ") + value + (good ? QString() : QStringLiteral(" — FAILED"));
        ok = ok && good;
    };
    lines << QStringLiteral("product: AcroBatya");
    lines << QStringLiteral("version: ") + version();
    lines << QStringLiteral("qt: ") + qtVersion() + QStringLiteral(" (built ") +
                 QStringLiteral(QT_VERSION_STR) + QStringLiteral(")");
    lines << QStringLiteral("mupdf: ") + mupdfVersion();
    lines << QStringLiteral("os: ") + QSysInfo::prettyProductName() + QStringLiteral(" ") +
                 QSysInfo::currentCpuArchitecture();
    const QString exeDir = QCoreApplication::applicationDirPath();
    lines << QStringLiteral("exe: ") + native(QCoreApplication::applicationFilePath());

    // Каталог данных — пишется (settings.ini, state.sqlite, журналы), путь ASCII (эталон 4.7).
    {
        const QString dir = dataDir();
        QFile probe(QDir(dir).filePath(QStringLiteral(".self-check")));
        const bool writable = probe.open(QIODevice::WriteOnly) && probe.write("ok", 2) == 2;
        probe.close();
        probe.remove();
        bool ascii = true;
        for (const QChar c : dir) {
            ascii = ascii && c.unicode() < 128;
        }
        check(QStringLiteral("data"), writable,
              native(dir) + (writable ? QString() : QStringLiteral(" (not writable)")) +
                  (ascii ? QString() : QStringLiteral(" (not ASCII)")));
        lines << QStringLiteral("settings: ") + native(settingsPath()) + QStringLiteral(" (INI)");
    }

    // Платформенные плагины Qt — по файлам: --self-check сам на QCoreApplication и QPA не грузит.
    {
        const QStringList dirs = pluginsDirs();
        const QStringList missing = missingPlugins(dirs);
        QString dir;  // где лежит qwindows.dll — им Qt и возьмёт
        for (const QString& d : dirs) {
            if (dir.isEmpty() && QFileInfo(QDir(d).filePath(requiredPlugins().constFirst())).isFile()) {
                dir = d;
            }
        }
        if (dir.isEmpty()) {
            dir = dirs.join(QLatin1Char(';'));
        }
        const QString detail =
            missing.isEmpty() ? QString(QStringLiteral(" (") + requiredPlugins().join(QStringLiteral(", ")) +
                                        QLatin1Char(')'))
                              : QString(QStringLiteral(" missing ") + missing.join(QStringLiteral(", ")));
        check(QStringLiteral("plugins"), missing.isEmpty(), native(dir) + detail);
    }

    // MuPDF: пустая страница A4 своим корнем контекста.
    try {
        MuContext mu;
        auto doc = Document::createBlank(mu, 1);
        const Bitmap bmp = doc->renderPage(0, {});
        const bool white = !bmp.empty() && bmp.pixels.front() == 255 && bmp.pixels.back() == 255;
        check(QStringLiteral("render"), white && bmp.width == 595 && bmp.height == 842,
              QStringLiteral("%1x%2").arg(bmp.width).arg(bmp.height));
    } catch (const std::exception& e) {
        check(QStringLiteral("render"), false, QString::fromUtf8(e.what()));
    }

    // tessdata — загрузкой языка (rus), как будет работать OCR.
    {
        const ocr::TessdataProbe t = ocr::probeTessdata(ocr::findTessdata(exeDir.toStdWString()), "rus");
        QStringList langs;
        for (const std::string& l : t.languages) {
            langs << QString::fromStdString(l);
        }
        check(QStringLiteral("tessdata"), t.ok,
              t.ok ? native(fromPath(t.dir)) + QStringLiteral(" (") + langs.join(QLatin1Char('+')) +
                         QLatin1Char(')')
                   : QString::fromStdString(t.error));
    }

    // qpdf.exe — линеаризация и проверки (эталон 4.4): найден и отвечает.
    {
        const QpdfTool qpdf;
        QString answer;
        bool good = false;
        if (qpdf.available()) {
            const ToolResult r = qpdf.run({L"--version"}, 15000);
            good = r.exitCode == 0;
            answer = QString::fromStdString(r.output).section(QLatin1Char('\n'), 0, 0).trimmed();
        }
        check(QStringLiteral("qpdf"), good,
              qpdf.available()
                  ? native(fromPath(qpdf.exe())) + QStringLiteral(" (") + answer + QLatin1Char(')')
                  : QStringLiteral("qpdf.exe not found"));
    }

    // Переводы интерфейса — ресурсом :/i18n (qt_add_translations у AcroBatya).
    {
        QStringList missing;
        for (const char* lang : {"ru", "en"}) {
            const QString qm = QStringLiteral(":/i18n/ottisk_%1.qm").arg(QLatin1String(lang));
            if (!QFile::exists(qm)) {
                missing << qm;
            }
        }
        check(QStringLiteral("translations"), missing.isEmpty(),
              missing.isEmpty() ? QStringLiteral("ru, en")
                                : QStringLiteral("missing ") + missing.join(QStringLiteral(", ")));
    }

    // Ассоциация .pdf — сведения, не ошибка (по умолчанию — выбор человека в Параметрах Windows).
    {
        const QString program = associatedPdfProgram();
        const bool ours =
            !program.isEmpty() && QFileInfo(program).canonicalFilePath().compare(
                                      QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath(),
                                      Qt::CaseInsensitive) == 0;
        lines << QStringLiteral("association: ") +
                     (program.isEmpty()
                          ? QStringLiteral("none")
                          : native(program) + (ours ? QStringLiteral(" (this program)") : QString()));
    }

    lines << (ok ? QStringLiteral("result: ok") : QStringLiteral("result: FAILED"));
    return ok;
}

}  // namespace ot::app
