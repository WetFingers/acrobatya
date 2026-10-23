#include "services/i18n.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QLocale>
#include <QTranslator>

#include <memory>

#include <spdlog/spdlog.h>

#include "services/app_info.h"
#include "services/settings.h"

namespace ot::i18n {

namespace {

struct State {
    std::unique_ptr<QTranslator> app;
    std::unique_ptr<QTranslator>
        qt;  // qtbase отдельно — только если в нашем .qm его нет (сборка без ресурсов)
    QString language = QStringLiteral("ru");
};

State& state() {
    static State s;
    return s;
}

}  // namespace

QStringList available() { return {QStringLiteral("ru"), QStringLiteral("en")}; }

QString resolve(const QString& requested) {
    const QString r = requested.trimmed().toLower();
    if (available().contains(r)) {
        return r;
    }
    // «Системный»: русский для русской Windows, иначе английский.
    return QLocale::system().language() == QLocale::Russian ? QStringLiteral("ru") : QStringLiteral("en");
}

QString fromSettings(const QString& iniPath) {
    const QString path =
        iniPath.isEmpty() ? QDir(app::dataDir()).filePath(QStringLiteral("settings.ini")) : iniPath;
    if (!QFile::exists(path)) {
        return QStringLiteral("system");
    }
    return Settings(path).get(settings::kLanguage);
}

bool install(const QString& language) {
    State& s = state();
    const QString lang = resolve(language);
    for (auto* t : {s.app.get(), s.qt.get()}) {
        if (t != nullptr) {
            QCoreApplication::removeTranslator(t);
        }
    }
    s.app = std::make_unique<QTranslator>();
    s.qt.reset();
    const bool loaded = s.app->load(QStringLiteral("ottisk_") + lang, QStringLiteral(":/i18n"));
    if (loaded) {
        QCoreApplication::installTranslator(s.app.get());
    } else {
        spdlog::warn("i18n: ottisk_{}.qm not found in resources — interface stays in source language",
                     lang.toStdString());
        // Хотя бы стандартные строки Qt: рядом с программой (translations/) или из Qt.
        s.qt = std::make_unique<QTranslator>();
        const QString qtbase = QStringLiteral("qtbase_") + lang;
        if (s.qt->load(qtbase, QCoreApplication::applicationDirPath() + QStringLiteral("/translations")) ||
            s.qt->load(qtbase, QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
            QCoreApplication::installTranslator(s.qt.get());
        }
    }
    s.language = lang;
    QLocale::setDefault(lang == QLatin1String("ru") ? QLocale(QLocale::Russian, QLocale::Russia)
                                                    : QLocale(QLocale::English, QLocale::UnitedStates));
    return loaded;
}

QString current() { return state().language; }

}  // namespace ot::i18n
