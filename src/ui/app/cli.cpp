#include "ui/app/cli.h"

#include <cstring>

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

}  // namespace ot::ui
