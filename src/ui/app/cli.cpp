#include "ui/app/cli.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <windows.h>
#include <shlobj.h>

#include "services/app_info.h"
#include "services/open_probe.h"

#include "ui/theme/theme.h"

namespace ot::ui {

CliOptions parseCli(const QStringList& arguments) {
    CliOptions o;
    for (int i = 1; i < arguments.size(); ++i) {
        const QString& a = arguments.at(i);
        auto next = [&](QString& target) {
            if (i + 1 >= arguments.size()) {
                o.error = QStringLiteral("missing value for ") + a;
                return false;
            }
            target = arguments.at(++i);
            return true;
        };
        if (a == QLatin1String("--version")) {
            o.version = true;
        } else if (a == QLatin1String("--self-check")) {
            o.selfCheck = true;
        } else if (a == QLatin1String("--register-notify")) {
            o.registerNotify = true;
        } else if (a == QLatin1String("--shots")) {
            QString v;
            if (!next(v)) break;
            o.shots = v.split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (a == QLatin1String("--shots-list")) {
            o.shotsList = true;
        } else if (a == QLatin1String("--commands-list")) {
            o.commandsList = true;
        } else if (a == QLatin1String("--lang")) {
            if (!next(o.lang)) break;
            if (o.lang != QLatin1String("ru") && o.lang != QLatin1String("en") &&
                o.lang != QLatin1String("system")) {
                o.error = QStringLiteral("bad --lang ") + o.lang + QStringLiteral(" (ru|en|system)");
            }
        } else if (a == QLatin1String("--out")) {
            if (!next(o.shotsOut)) break;
        } else if (a == QLatin1String("--size")) {
            QString v;
            if (!next(v)) break;
            const QStringList wh = v.split(QLatin1Char('x'));
            if (wh.size() == 2 && wh[0].toInt() > 0 && wh[1].toInt() > 0) {
                o.shotsSize = QSize(wh[0].toInt(), wh[1].toInt());
            } else {
                o.error = QStringLiteral("bad --size ") + v;
            }
        } else if (a == QLatin1String("--theme")) {
            if (!next(o.theme)) break;
            if (!parseThemeChoice(o.theme)) {
                o.error = QStringLiteral("bad --theme ") + o.theme +
                          QStringLiteral(" (system|light|dark|contrast)");
            }
        } else if (a == QLatin1String("--scale")) {
            QString v;
            if (!next(v)) break;
            bool ok = false;
            o.scale = v.toDouble(&ok);
            o.scaleGiven = true;
            if (!ok || o.scale < 1.0 || o.scale > 2.0) {
                o.error = QStringLiteral("bad --scale ") + v;
            }
        } else if (a == QLatin1String("--showcase")) {
            o.showcase = true;
        } else if (a == QLatin1String("--smoke")) {
            QString v;
            if (!next(v)) break;
            o.smokeSeconds = v.toInt();
            if (o.smokeSeconds <= 0) {
                o.error = QStringLiteral("bad --smoke ") + v;
            }
        } else if (a == QLatin1String("--open-all")) {
            if (!next(o.openAll)) break;
        } else if (a == QLatin1String("--script")) {
            if (!next(o.script)) break;
        } else if (a == QLatin1String("--page")) {
            QString v;
            if (!next(v)) break;
            o.page = v.toInt();
            if (o.page <= 0) {
                o.error = QStringLiteral("bad --page ") + v;
            }
        } else if (a == QLatin1String("--log")) {
            if (!next(o.logLevel)) break;
            static const QStringList levels = {QStringLiteral("trace"), QStringLiteral("debug"),
                                               QStringLiteral("info"),  QStringLiteral("warn"),
                                               QStringLiteral("error"), QStringLiteral("off")};
            if (!levels.contains(o.logLevel)) {
                o.error = QStringLiteral("bad --log ") + o.logLevel + QStringLiteral(" (") +
                          levels.join(QLatin1Char('|')) + QLatin1Char(')');
            }
        } else if (a == QLatin1String("--data-dir")) {
            if (!next(o.dataDir)) break;
        } else if (a.startsWith(QLatin1String("--"))) {
            o.error = QStringLiteral("unknown option ") + a;
        } else if (!a.startsWith(QLatin1Char('-'))) {
            o.files << a;
        }
    }
    return o;
}

bool hasArg(int argc, char** argv, const char* name) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

bool isConsoleMode(int argc, char** argv) {
    for (const char* mode :
         {"--version", "--self-check", "--register-notify", "--shots-list", "--open-all"}) {
        if (hasArg(argc, argv, mode)) {
            return true;
        }
    }
    return false;
}

namespace {

void printLine(const QString& s) {
    const QByteArray utf8 = s.toUtf8();
    std::fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

}  // namespace

int runSelfCheck() {
    QStringList lines;
    const bool ok = app::selfCheck(lines);
    for (const QString& line : lines) {
        printLine(line);
    }
    return ok ? 0 : 1;
}

int runRegisterNotify() {
    // Проводник перечитывает ассоциации после установщика; ProgID и Capabilities пишет сам установщик.
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return 0;
}

OpenAllReport openAll(const QString& folder, int timeoutMs, bool trace) {
    OpenAllReport report;
    QStringList files;
    QDirIterator it(folder, {QStringLiteral("*.pdf")}, QDir::Files | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        files << it.next();
    }
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    for (const QString& file : files) {
        if (trace) {
            printLine(QStringLiteral("probe ") + QDir::toNativeSeparators(file));
        }
        const OpenProbe p = probeOpen(file, timeoutMs);
        ++report.files;
        switch (p.result) {
            case OpenProbe::Result::Opened:
                ++report.opened;
                if (!p.reason.isEmpty()) {  // открылся, но страница 1 битая
                    report.lines << QStringLiteral("refused ") + QDir::toNativeSeparators(file) +
                                        QStringLiteral(": ") + p.reason;
                }
                break;
            case OpenProbe::Result::Refused:
                ++report.refused;
                report.lines << QStringLiteral("refused ") + QDir::toNativeSeparators(file) +
                                    QStringLiteral(": ") + p.reason;
                break;
            case OpenProbe::Result::Failed:
                ++report.failures;
                report.lines << QStringLiteral("FAIL ") + QDir::toNativeSeparators(file) +
                                    QStringLiteral(": ") + p.reason;
                break;
        }
    }
    return report;
}

int runOpenAll(const QString& folder) {
    if (!QFileInfo(folder).isDir()) {
        printLine(QStringLiteral("open-all: no such folder ") + QDir::toNativeSeparators(folder));
        return 1;
    }
    const bool trace = qEnvironmentVariable("OTTISK_OPEN_ALL_TRACE") == QLatin1String("1");
    const OpenAllReport r = openAll(folder, 20000, trace);
    for (const QString& line : r.lines) {
        printLine(line);
    }
    printLine(QStringLiteral("open-all: %1 files, opened %2, refused %3, failures %4")
                  .arg(r.files)
                  .arg(r.opened)
                  .arg(r.refused)
                  .arg(r.failures));
    return r.failures;
}

}  // namespace ot::ui
